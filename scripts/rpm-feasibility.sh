#!/bin/bash
#
# rpm-feasibility.sh - 统计一批 Fedora RPM 在 OkraLinux 上的可用性。
#
# 回答一个问题：这批 rpm 里，有多大比例在 Okra 上真的能用？
#
# 判据三条：
#   1. 架构。noarch 不依赖 libc 符号版本，可直接用。
#   2. DT_NEEDED。所需动态库在 Okra sysroot 里是否存在。
#   3. 符号版本。ELF 需要的 GLIBC_ / GLIBCXX_ / CXXABI_ 上限，是否超过
#      Okra 那份 glibc 和 libstdc++ 的导出上限。
#
# 运行环境：构建宿主机。需要 bash、rpm、rpm2cpio、cpio、objdump、find、
# grep、sed、sort、od、awk、mktemp。
# 不要在 Base OS 里跑：Base OS 没有 GNU grep，也没有 sed。
#
# 用法：
#   ./rpm-feasibility.sh -s /path/to/OKRALINUX samples/*.rpm
#   ./rpm-feasibility.sh -s ../OKRALINUX -o report -v *.rpm
#
# 产物：
#   arch.txt             架构分布
#   needed.txt           每个 ELF 的 DT_NEEDED 与 sysroot 命中情况
#   symbols.txt          每个 ELF 的符号版本需求
#   verdict.txt          每个 RPM 的结论
#   summary.txt          汇总比例与判读
#   sysroot-sonames.txt  sysroot 里的 .so 清单

set -u

ScriptName=$(basename "$0")
SysrootPath=""
OutputDir="rpm-feasibility-out"
MaxPayloadMb=800
KeepExtract=0
Verbose=0
InputFiles=()

Usage()
{
	cat <<'UsageEnd'
rpm-feasibility.sh - 统计 Fedora RPM 在 OkraLinux 上的可用性

选项：
  -s <dir>     Okra sysroot 路径，即构建盘上的 OKRALINUX/。必填。
  -o <dir>     输出目录，默认 rpm-feasibility-out
  -m <mb>      单个 rpm 解包体积上限，默认 800，超过则跳过
  -k           保留解包出来的临时目录，便于人工复核
  -v           打印每个文件的处理过程
  -h           显示本帮助

参数：
  一个或多个 .rpm 文件路径。支持通配。

例子：
  ./rpm-feasibility.sh -s ../OKRALINUX samples/*.rpm
  ./rpm-feasibility.sh -s /path/to/OKRALINUX -o /tmp/report -v cmake*.rpm
UsageEnd
}

LogInfo()
{
	printf '[info] %s\n' "$*"
}

LogWarn()
{
	printf '[warn] %s\n' "$*" >&2
}

LogDie()
{
	printf '[fail] %s\n' "$*" >&2
	exit 1
}

LogVerbose()
{
	if [ "$Verbose" -eq 1 ]; then
		printf '[verb] %s\n' "$*"
	fi
}

ParseArgs()
{
	while [ $# -gt 0 ]; do
		case "$1" in
			-s)
				[ $# -ge 2 ] || LogDie "-s 需要一个参数"
				SysrootPath="$2"
				shift 2
				;;
			-o)
				[ $# -ge 2 ] || LogDie "-o 需要一个参数"
				OutputDir="$2"
				shift 2
				;;
			-m)
				[ $# -ge 2 ] || LogDie "-m 需要一个参数"
				MaxPayloadMb="$2"
				shift 2
				;;
			-k)
				KeepExtract=1
				shift
				;;
			-v)
				Verbose=1
				shift
				;;
			-h|--help)
				Usage
				exit 0
				;;
			-*)
				LogDie "未知选项 $1"
				;;
			*)
				InputFiles+=("$1")
				shift
				;;
		esac
	done

	[ -n "$SysrootPath" ] || LogDie "缺少 -s <sysroot>。用 -h 看帮助。"
	[ ${#InputFiles[@]} -gt 0 ] || LogDie "没有给出 .rpm 文件。用 -h 看帮助。"
	[ -d "$SysrootPath" ] || LogDie "sysroot 不是目录：$SysrootPath"
}

CheckTools()
{
	local Tool
	for Tool in rpm rpm2cpio cpio objdump find grep sed sort od awk mktemp; do
		if ! command -v "$Tool" >/dev/null 2>&1; then
			LogDie "缺少工具：$Tool"
		fi
	done
}

# PercentOf() - 算百分比，一位小数。
# @Part: 分子。
# @Whole: 分母。
# Return: 打印百分比数值，不带百分号。分母为 0 时打印 0.0。
PercentOf()
{
	local Part="$1"
	local Whole="$2"
	if [ "$Whole" -eq 0 ]; then
		printf '0.0\n'
		return
	fi
	awk -v P="$Part" -v W="$Whole" 'BEGIN { printf "%.1f\n", P * 100 / W }'
}

# IsElf() - 判断一个文件是不是 ELF。
# @Path: 文件路径。
# Return: 0 表示是 ELF。
IsElf()
{
	local Path="$1"
	local Magic
	Magic=$(od -An -N4 -tx1 "$Path" 2>/dev/null | tr -d ' \n')
	[ "$Magic" = "7f454c46" ]
}

# FindElvesIn() - 列出一棵目录树里所有 ELF。
# @Root: 目录路径。
# Return: 每行一个路径。
FindElvesIn()
{
	local Root="$1"
	local Candidate
	while IFS= read -r -d '' Candidate; do
		if IsElf "$Candidate"; then
			printf '%s\n' "$Candidate"
		fi
	done < <(find "$Root" -type f -print0 2>/dev/null)
}

# ListNeededLibraries() - 列出一个 ELF 的 DT_NEEDED。
# @Elf: ELF 文件路径。
# Return: 每行一个 soname。
ListNeededLibraries()
{
	local Elf="$1"
	objdump -p "$Elf" 2>/dev/null \
		| awk '$1 == "NEEDED" { print $2 }' \
		| sort -u
}

# ExtractVersionFamilies() - 从一个 ELF 里抽出符号版本标签。
# @Elf: ELF 文件路径。
# @UndefinedOnly: 1 表示只看未定义的，即它需要的；0 表示看全部导出。
# Return: 每行一个版本标签，如 GLIBC_2.38。
ExtractVersionFamilies()
{
	local Elf="$1"
	local UndefinedOnly="$2"
	if [ "$UndefinedOnly" -eq 1 ]; then
		objdump -T "$Elf" 2>/dev/null \
			| grep '\*UND\*' \
			| grep -o 'GLIBC_[0-9][0-9.]*\|GLIBCXX_[0-9][0-9.]*\|CXXABI_[0-9][0-9.]*'
	else
		objdump -T "$Elf" 2>/dev/null \
			| grep -o 'GLIBC_[0-9][0-9.]*\|GLIBCXX_[0-9][0-9.]*\|CXXABI_[0-9][0-9.]*'
	fi
}

# MaxOfFamily() - 从标准输入的一批版本标签里，取某一族的最大值。
# @Family: 族名，如 GLIBC。不带下划线。
# Return: 打印最大标签，如 GLIBC_2.38。没有则打印空行。
MaxOfFamily()
{
	local Family="$1"
	grep "^${Family}_" | sort -V -u | tail -1
}

# IsNewerThan() - 判断版本标签是否比基准新。
# @Wanted: 需要的标签，如 GLIBC_2.38。
# @Baseline: 基准标签，如 GLIBC_2.34。
# Return: 0 表示 Wanted 更新。
IsNewerThan()
{
	local Wanted="$1"
	local Baseline="$2"
	local Winner
	Winner=$(printf '%s\n%s\n' "$Wanted" "$Baseline" | sort -V -u | tail -1)
	[ "$Winner" = "$Wanted" ] && [ "$Wanted" != "$Baseline" ]
}

# PayloadSizeMb() - 估算一个 rpm 装好后的体积，单位 MB。
# @RpmFile: rpm 路径。
# Return: 打印整数。
PayloadSizeMb()
{
	local RpmFile="$1"
	local Bytes
	Bytes=$(rpm -qp --qf '%{SIZE}' "$RpmFile" 2>/dev/null)
	case "$Bytes" in
		''|*[!0-9]*)
			printf '0\n'
			;;
		*)
			printf '%s\n' $(( Bytes / 1024 / 1024 ))
			;;
	esac
}

# RpmField() - 读一个 rpm 的某个标签。
# @RpmFile: rpm 路径。
# @Field: 标签名，如 NAME。
# Return: 打印值。
RpmField()
{
	local RpmFile="$1"
	local Field="$2"
	rpm -qp --qf "%{$Field}" "$RpmFile" 2>/dev/null
}

# ExtractRpm() - 把一个 rpm 解到目标目录。
# @RpmFile: rpm 路径。
# @TargetDir: 目标目录，必须已存在。
# Return: 0 成功，1 失败。
ExtractRpm()
{
	local RpmFile="$1"
	local TargetDir="$2"
	( cd "$TargetDir" && rpm2cpio "$RpmFile" | cpio -idmu --quiet --no-absolute-filenames ) 2>/dev/null
}

# AppendVerdict() - 写一行结论。
# @Name @Arch @BaseName @Verdict @Reason
AppendVerdict()
{
	printf '%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" >> "$OutputDir/verdict.txt"
}

# ScanOneRpm() - 处理一个 rpm。
# @RpmFile: rpm 路径。
# @TempRoot: 临时目录根。
# Return: 0 已处理，1 跳过。
ScanOneRpm()
{
	local RpmFile="$1"
	local TempRoot="$2"

	local BaseName Name Arch SizeMb
	BaseName=$(basename "$RpmFile")
	Name=$(RpmField "$RpmFile" NAME)
	Arch=$(RpmField "$RpmFile" ARCH)

	if [ -z "$Name" ]; then
		AppendVerdict "$BaseName" '?' '?' '失败' '读不出 rpm header'
		return 0
	fi

	printf '%s\t%s\n' "$Arch" "$Name" >> "$OutputDir/arch.txt"

	SizeMb=$(PayloadSizeMb "$RpmFile")
	if [ "$SizeMb" -gt "$MaxPayloadMb" ]; then
		AppendVerdict "$Name" "$Arch" "$BaseName" '跳过' \
			"体积 ${SizeMb}MB 超过上限 ${MaxPayloadMb}MB"
		return 0
	fi

	local WorkDir="$TempRoot/$Name-$Arch"
	rm -rf "$WorkDir"
	mkdir -p "$WorkDir"

	if ! ExtractRpm "$RpmFile" "$WorkDir"; then
		AppendVerdict "$Name" "$Arch" "$BaseName" '失败' '解包失败'
		rm -rf "$WorkDir"
		return 0
	fi

	if [ "$Arch" = "noarch" ]; then
		AppendVerdict "$Name" "$Arch" "$BaseName" '高' 'noarch，无 libc 符号版本依赖'
		[ "$KeepExtract" -eq 0 ] && rm -rf "$WorkDir"
		return 0
	fi

	local ElfList
	ElfList=$(FindElvesIn "$WorkDir")

	if [ -z "$ElfList" ]; then
		AppendVerdict "$Name" "$Arch" "$BaseName" '高' '无 ELF，纯数据'
		[ "$KeepExtract" -eq 0 ] && rm -rf "$WorkDir"
		return 0
	fi

	local MissingCount=0 TooNewCount=0 ElfCount=0
	local WorstMissing="" WorstTooNew="" WorstNeed=""

	local Elf
	while IFS= read -r Elf; do
		[ -n "$Elf" ] || continue
		ElfCount=$(( ElfCount + 1 ))
		local Relative
		Relative=${Elf#"$WorkDir"}

		local Needed
		while IFS= read -r Needed; do
			[ -n "$Needed" ] || continue
			if ! grep -qxF -- "$Needed" "$SysrootSonameFile"; then
				printf '%s\t%s\t%s\t缺失\n' "$Name" "$Relative" "$Needed" \
					>> "$OutputDir/needed.txt"
				MissingCount=$(( MissingCount + 1 ))
				[ -z "$WorstMissing" ] && WorstMissing="$Needed"
			fi
		done < <(ListNeededLibraries "$Elf")

		local Required
		Required=$(ExtractVersionFamilies "$Elf" 1)
		[ -n "$Required" ] || continue

		local Family Wanted Baseline
		for Family in GLIBC GLIBCXX CXXABI; do
			Wanted=$(printf '%s\n' "$Required" | MaxOfFamily "$Family")
			[ -n "$Wanted" ] || continue
			case "$Family" in
				GLIBC)   Baseline="$OkraGlibcMax" ;;
				GLIBCXX) Baseline="$OkraGlibcxxMax" ;;
				CXXABI)  Baseline="$OkraCxxabiMax" ;;
				*)       Baseline="" ;;
			esac
			printf '%s\t%s\t%s\t需要 %s\tOkra 上限 %s\n' \
				"$Name" "$Relative" "$Family" "$Wanted" "${Baseline:-未知}" \
				>> "$OutputDir/symbols.txt"
			[ -z "$WorstNeed" ] && WorstNeed="$Wanted"
			if [ -n "$Baseline" ] && IsNewerThan "$Wanted" "$Baseline"; then
				TooNewCount=$(( TooNewCount + 1 ))
				[ -z "$WorstTooNew" ] && WorstTooNew="$Wanted"
			fi
		done
	done < <(printf '%s\n' "$ElfList")

	local Verdict Reason
	if [ "$MissingCount" -gt 0 ] && [ "$TooNewCount" -gt 0 ]; then
		Verdict='低'
		Reason="${MissingCount} 个库缺失（如 ${WorstMissing}），符号版本 ${WorstTooNew} 过新"
	elif [ "$MissingCount" -gt 0 ]; then
		Verdict='低'
		Reason="${MissingCount} 个库缺失（如 ${WorstMissing}）"
	elif [ "$TooNewCount" -gt 0 ]; then
		Verdict='中'
		Reason="库齐全，但符号版本 ${WorstTooNew} 超过 Okra 上限"
	else
		Verdict='高'
		Reason="${ElfCount} 个 ELF，依赖全部命中"
	fi

	AppendVerdict "$Name" "$Arch" "$BaseName" "$Verdict" "$Reason"

	[ "$KeepExtract" -eq 0 ] && rm -rf "$WorkDir"
	return 0
}

# BuildSysrootSonameList() - 把 sysroot 里的 .so 清单写到文件。
# Return: 0 成功。设置 SysrootSonameFile。
BuildSysrootSonameList()
{
	SysrootSonameFile="$OutputDir/sysroot-sonames.txt"
	local Item
	: > "$SysrootSonameFile"
	while IFS= read -r -d '' Item; do
		basename "$Item"
	done < <(find "$SysrootPath" \( -type f -o -type l \) -name '*.so*' -print0 2>/dev/null) \
		| sort -u > "$SysrootSonameFile"
	LogInfo "sysroot 里有 $(wc -l < "$SysrootSonameFile") 个 .so 文件"
}

# FindSysrootLibrary() - 在 sysroot 里找一个 soname 的文件。
# @Soname: 例如 libc.so.6。
# Return: 0 找到并打印路径，1 没有。
FindSysrootLibrary()
{
	local Soname="$1"
	local Found
	Found=$(find "$SysrootPath" \( -type f -o -type l \) -name "$Soname" -print -quit 2>/dev/null)
	[ -n "$Found" ] || return 1
	printf '%s\n' "$Found"
}

# BuildOkraLimits() - 读出 Okra 的符号版本上限。
# Return: 0 成功。设置 OkraGlibcMax / OkraGlibcxxMax / OkraCxxabiMax。
BuildOkraLimits()
{
	OkraGlibcMax=""
	OkraGlibcxxMax=""
	OkraCxxabiMax=""

	local Libc
	if Libc=$(FindSysrootLibrary libc.so.6); then
		OkraGlibcMax=$(ExtractVersionFamilies "$Libc" 0 | MaxOfFamily GLIBC)
		LogInfo "Okra glibc 上限：${OkraGlibcMax:-读不出}"
	else
		LogWarn "sysroot 里找不到 libc.so.6，GLIBC 版本无法对比"
	fi

	local Libstdcpp
	if Libstdcpp=$(FindSysrootLibrary libstdc++.so.6); then
		OkraGlibcxxMax=$(ExtractVersionFamilies "$Libstdcpp" 0 | MaxOfFamily GLIBCXX)
		OkraCxxabiMax=$(ExtractVersionFamilies "$Libstdcpp" 0 | MaxOfFamily CXXABI)
		LogInfo "Okra libstdc++ 上限：GLIBCXX ${OkraGlibcxxMax:-读不出}，CXXABI ${OkraCxxabiMax:-读不出}"
	else
		LogWarn "sysroot 里找不到 libstdc++.so.6，C++ 程序无法对比"
	fi
}

# WriteSummary() - 统计比例并打印。
WriteSummary()
{
	local Total High Medium Low Failed Skipped
	Total=$(wc -l < "$OutputDir/verdict.txt" 2>/dev/null)
	Total=${Total:-0}
	High=$(awk -F'\t' '$4 == "高"' "$OutputDir/verdict.txt" 2>/dev/null | wc -l)
	Medium=$(awk -F'\t' '$4 == "中"' "$OutputDir/verdict.txt" 2>/dev/null | wc -l)
	Low=$(awk -F'\t' '$4 == "低"' "$OutputDir/verdict.txt" 2>/dev/null | wc -l)
	Failed=$(awk -F'\t' '$4 == "失败"' "$OutputDir/verdict.txt" 2>/dev/null | wc -l)
	Skipped=$(awk -F'\t' '$4 == "跳过"' "$OutputDir/verdict.txt" 2>/dev/null | wc -l)

	{
		printf '总数        %s\n' "$Total"
		printf '高          %s  (%s%%)\n' "$High" "$(PercentOf "$High" "$Total")"
		printf '中          %s  (%s%%)\n' "$Medium" "$(PercentOf "$Medium" "$Total")"
		printf '低          %s  (%s%%)\n' "$Low" "$(PercentOf "$Low" "$Total")"
		printf '失败        %s\n' "$Failed"
		printf '跳过        %s\n' "$Skipped"
		printf '\n架构分布：\n'
		cut -f1 "$OutputDir/arch.txt" 2>/dev/null | sort | uniq -c | sort -rn
		printf '\n缺失最多的库：\n'
		cut -f3 "$OutputDir/needed.txt" 2>/dev/null | sort | uniq -c | sort -rn | head -20
		printf '\n符号版本需求最高的：\n'
		cut -f3,4 "$OutputDir/symbols.txt" 2>/dev/null | sort -u | head -20
		printf '\n判读：\n'
		printf '  高 占比 >= 50%%   值得做适配器\n'
		printf '  高 占比 <  20%%   改走 SRPM 重构建路线更划算\n'
		printf '  缺失榜里出现 libselinux / libmount / libcrypto，说明这些包要连依赖一起重构建\n'
	} > "$OutputDir/summary.txt"

	cat "$OutputDir/summary.txt"
}

Main()
{
	ParseArgs "$@"
	CheckTools

	mkdir -p "$OutputDir"
	local Item
	for Item in arch.txt needed.txt symbols.txt verdict.txt summary.txt; do
		rm -f "$OutputDir/$Item"
	done
	: > "$OutputDir/arch.txt"
	: > "$OutputDir/needed.txt"
	: > "$OutputDir/symbols.txt"
	: > "$OutputDir/verdict.txt"

	LogInfo "sysroot：$SysrootPath"
	LogInfo "输出：$OutputDir"

	BuildSysrootSonameList
	BuildOkraLimits

	# TempRoot 故意不用 local：EXIT trap 在 Main 返回之后才执行，
	# local 变量那时已经失效，trap 里会读到未绑定的名字。
	TempRoot=$(mktemp -d)
	trap 'rm -rf "${TempRoot:-}"' EXIT

	local Index=0 Total=${#InputFiles[@]} RpmFile
	for RpmFile in "${InputFiles[@]}"; do
		Index=$(( Index + 1 ))
		if [ ! -f "$RpmFile" ]; then
			LogWarn "跳过不存在的文件：$RpmFile"
			continue
		fi
		LogVerbose "[$Index/$Total] $RpmFile"
		ScanOneRpm "$RpmFile" "$TempRoot"
	done

	printf '\n'
	WriteSummary
	LogInfo "完成。明细在 $OutputDir/"
}

Main "$@"