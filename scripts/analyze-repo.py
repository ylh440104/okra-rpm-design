#!/usr/bin/env python3
# analyze-repo.py - 用 Fedora 仓库元数据分析可用性。
#
# 输入：primary.xml（来自 repodata，zstd 解压后）
#       Okra rootfs 的 .so 清单
# 输出：按类别统计的可用性报告
#
# 为什么用 primary.xml 而不是抽样下载 rpm：
#   1. 覆盖**全部**包，不是抽样。
#   2. primary.xml 的 <rpm:requires> 里包含 RPM 自动生成的 soname 依赖，
#      例如 libc.so.6()(64bit)、libstdc++.so.6()(64bit)。这正是判定需要的。
#   3. 不需要下载几十 GB 的 rpm。
#
# 判定逻辑：
#   一个包判为"高"，当且仅当它满足全部：
#     a. arch 是 noarch，或者
#     b. 它的 requires 里所有 .so 依赖都能在 Okra rootfs 找到
#   否则判"低"，并记下缺什么。

import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict

NS = {
	'common': 'http://linux.duke.edu/metadata/common',
	'rpm': 'http://linux.duke.edu/metadata/rpm',
}


def LoadOkraSonames(sysroot):
	"""扫 Okra rootfs，返回所有 .so 文件名的集合。"""
	Names = set()
	for Root, Dirs, Files in os.walk(sysroot):
		for Name in Files:
			if '.so' in Name:
				Names.add(Name)
		# 符号链接也算
		for Name in Dirs:
			if '.so' in Name:
				Names.add(Name)
	return Names


# 这些前缀的能力名不是目标系统上的库，是 RPM 自己的或虚拟能力。
# 判定时不算"缺库"。
VIRTUAL_PREFIXES = (
	'rpmlib(',
	'config(',
	'pkgconfig(',
	'perl(',
	'python',
	'ruby(',
	'php(',
	'java',
	'libreoffice(',
	'tex(',
	'font(',
	'application(',
	'shared-library(',
	'module(',
	'bundled(',
	'idris(',
	'ghc-',
	'ocaml(',
	'mono(',
	'nodejs(',
	'wine-',
	'systemd-',
	'kernel-',
	'kmod(',
	'osgi(',
	'namespace(',
)


def IsLibraryRequirement(Name):
	"""判断一个 require 名字是不是 .so 库依赖。"""
	if not Name:
		return False
	# RPM 的 soname 依赖形态：libfoo.so.1()(64bit) 或 libfoo.so.1
	Base = Name.split('()')[0]
	if Base.endswith('.so') or re.search(r'\.so(\.\d+)+$', Base):
		return True
	return False


def IsVirtual(Name):
	"""判断是不是虚拟能力，不算缺库。"""
	if not Name:
		return False
	if Name.startswith('/'):
		return True          # 文件依赖，不是库
	if '(' in Name and not Name.split('()')[0].endswith('.so'):
		# 形如 python3.13dist(foo)、perl(Foo) 这类
		for Prefix in VIRTUAL_PREFIXES:
			if Name.startswith(Prefix):
				return True
		if re.match(r'^[a-zA-Z0-9_.+-]+\(', Name) and '.so' not in Name.split('(')[0]:
			return True
	return False


def main():
	if len(sys.argv) < 3:
		print('用法: analyze-repo.py <primary.xml> <okra-rootfs>')
		return 1

	XmlPath = sys.argv[1]
	SysrootPath = sys.argv[2]

	print('加载 Okra rootfs 的 .so 清单……')
	OkraSonames = LoadOkraSonames(SysrootPath)
	print(f'  {len(OkraSonames)} 个 .so 文件名')
	print()

	print('解析 primary.xml……')
	Tree = ET.parse(XmlPath)
	Root = Tree.getroot()
	print('  完成')
	print()

	ArchCount = Counter()
	VerdictCount = Counter()
	MissingLibs = Counter()
	ReasonCount = Counter()
	ByArchVerdict = defaultdict(Counter)

	Total = 0
	SamplesLow = []
	SamplesHigh = []

	for Package in Root.findall('common:package', NS):
		Total += 1

		NameElement = Package.find('common:name', NS)
		ArchElement = Package.find('common:arch', NS)
		FormatElement = Package.find('common:format', NS)

		Name = NameElement.text if NameElement is not None else '?'
		Arch = ArchElement.text if ArchElement is not None else '?'

		ArchCount[Arch] += 1

		# 收集 requires
		Missing = []
		Requires = []
		if FormatElement is not None:
			RequiresElement = FormatElement.find('rpm:requires', NS)
			if RequiresElement is not None:
				for Entry in RequiresElement.findall('rpm:entry', NS):
					Requires.append(Entry.get('name'))

		# 判定
		if Arch == 'noarch':
			Verdict = '高'
			Reason = 'noarch'
		else:
			for Requirement in Requires:
				if not IsLibraryRequirement(Requirement):
					continue
				if IsVirtual(Requirement):
					continue
				# 取出 soname：libfoo.so.1()(64bit) -> libfoo.so.1
				Soname = Requirement.split('()')[0]
				if Soname not in OkraSonames:
					Missing.append(Soname)

			if Missing:
				Verdict = '低'
				Reason = '缺库'
				for Item in Missing:
					MissingLibs[Item] += 1
			else:
				Verdict = '高'
				Reason = '库齐全'

		VerdictCount[Verdict] += 1
		ReasonCount[Reason] += 1
		ByArchVerdict[Arch][Verdict] += 1

		if Verdict == '低' and len(SamplesLow) < 25:
			SamplesLow.append((Name, Arch, Missing[:3]))
		if Verdict == '高' and Arch != 'noarch' and len(SamplesHigh) < 15:
			SamplesHigh.append((Name, Arch))

	print('=' * 72)
	print('Fedora 仓库可用性分析')
	print('=' * 72)
	print()
	print(f'包总数：{Total}')
	print()

	print('架构分布：')
	for Arch, Count in ArchCount.most_common():
		print(f'  {Arch:<12} {Count:>7}  {Count * 100.0 / Total:>5.1f}%')
	print()

	print('判定结果：')
	for Verdict in ('高', '低'):
		Count = VerdictCount.get(Verdict, 0)
		print(f'  {Verdict:<4} {Count:>7}  {Count * 100.0 / Total:>5.1f}%')
	print()

	print('按架构看：')
	for Arch in sorted(ByArchVerdict):
		High = ByArchVerdict[Arch].get('高', 0)
		Low = ByArchVerdict[Arch].get('低', 0)
		TotalArch = High + Low
		if TotalArch == 0:
			continue
		print(f'  {Arch:<12} 高 {High:>6} ({High * 100.0 / TotalArch:>5.1f}%)   '
		      f'低 {Low:>6} ({Low * 100.0 / TotalArch:>5.1f}%)')
	print()

	print('缺失最多的库（前 25）：')
	for Lib, Count in MissingLibs.most_common(25):
		print(f'  {Lib:<44} {Count:>6}')
	print()

	print('判为"高"的非 noarch 包示例：')
	for Name, Arch in SamplesHigh:
		print(f'  {Name} ({Arch})')
	print()

	print('判为"低"的包示例：')
	for Name, Arch, Missing in SamplesLow:
		print(f'  {Name:<32} ({Arch})  缺 {", ".join(Missing)}')
	print()

	print('=' * 72)
	HighPercent = VerdictCount.get('高', 0) * 100.0 / Total
	if HighPercent >= 50:
		print(f'结论：高占比 {HighPercent:.1f}%，值得做适配器')
	elif HighPercent >= 20:
		print(f'结论：高占比 {HighPercent:.1f}%，可以做一个受限的适配器')
	else:
		print(f'结论：高占比 {HighPercent:.1f}%，改走 SRPM 重构建路线更划算')
	print('=' * 72)

	return 0


if __name__ == '__main__':
	sys.exit(main())