#!/bin/bash
# 生成依赖匹配的完整矩阵，用真实 rpm 求解。
#
# 输出 /tmp/dep-full-matrix.tsv，每行四列：
#   提供者编号  提供者描述  需求者编号  结果（满足/失败）
#
# C++ 侧用 tests/dep_matrix_full_main.cpp 复现同一矩阵，然后 diff。

set -u

Work=/root/fullmx
R=$Work/RPMS/noarch
Out=/tmp/dep-full-matrix.tsv

MakeSpec()
{
	local Name="$1"
	local Line="$2"
	{
		printf 'Name: %s\n' "$Name"
		printf 'Version: 1.0\n'
		printf 'Release: 1\n'
		printf 'Summary: %s\n' "$Name"
		printf 'License: MIT\n'
		printf 'BuildArch: noarch\n'
		printf '%s\n' "$Line"
		printf '%%description\nx\n%%files\n'
	} > "$Work/SPECS/$Name.spec"
}

rm -rf "$Work"
mkdir -p "$Work/BUILD" "$Work/RPMS" "$Work/SOURCES" "$Work/SPECS" "$Work/SRPMS"

# 提供者：9 个
ProvidesLines=(
	'Provides: virt = 1.0'
	'Provides: virt = 1.0-1'
	'Provides: virt = 1.0-2'
	'Provides: virt = 1.0-'
	'Provides: virt = 1'
	'Provides: virt = 1:1.0'
	'Provides: virt = 1:1.0-1'
	'Provides: virt = 2.0'
	'Provides: virt'
)

# 需求者：14 个
RequiresLines=(
	'Requires: virt = 1.0'
	'Requires: virt = 1.0-1'
	'Requires: virt = 1.0-2'
	'Requires: virt = 1.0-'
	'Requires: virt = 1'
	'Requires: virt = 1:1.0'
	'Requires: virt >= 1.0'
	'Requires: virt > 1.0'
	'Requires: virt < 1.0'
	'Requires: virt <= 1.0'
	'Requires: virt >= 1.0-1'
	'Requires: virt > 1.0-1'
	'Requires: virt < 1.0-2'
	'Requires: virt <= 1.0-1'
)

for Index in "${!ProvidesLines[@]}"; do
	MakeSpec "P$Index" "${ProvidesLines[$Index]}"
done
for Index in "${!RequiresLines[@]}"; do
	MakeSpec "R$Index" "${RequiresLines[$Index]}"
done

for Spec in "$Work"/SPECS/*.spec; do
	rpmbuild --define "_topdir $Work" -bb "$Spec" >/dev/null 2>&1
done

: > "$Out"

ProvidesIndex=0
while [ "$ProvidesIndex" -lt "${#ProvidesLines[@]}" ]; do
	rm -rf /root/fmxdb
	mkdir -p /root/fmxdb
	rpm -i --dbpath /root/fmxdb --nodeps --noscripts --justdb \
		"$R/P$ProvidesIndex-1.0-1.noarch.rpm" 2>/dev/null

	ProvidesText=$(rpm -q --dbpath /root/fmxdb --provides "P$ProvidesIndex" 2>/dev/null \
		| grep '^virt' | tr -d '\n')
	[ -n "$ProvidesText" ] || ProvidesText='virt'

	RequiresIndex=0
	while [ "$RequiresIndex" -lt "${#RequiresLines[@]}" ]; do
		if rpm -i --test --dbpath /root/fmxdb \
			"$R/R$RequiresIndex-1.0-1.noarch.rpm" >/dev/null 2>&1; then
			Verdict='满足'
		else
			Verdict='失败'
		fi
		printf 'P%s\t%s\tR%s\t%s\n' "$ProvidesIndex" "$ProvidesText" \
			"$RequiresIndex" "$Verdict" >> "$Out"
		RequiresIndex=$((RequiresIndex + 1))
	done
	ProvidesIndex=$((ProvidesIndex + 1))
done

echo "共 $(wc -l < "$Out") 组"
echo
printf '%-18s' 'Provides \ Req'
RequiresIndex=0
while [ "$RequiresIndex" -lt "${#RequiresLines[@]}" ]; do
	printf '%-5s' "R$RequiresIndex"
	RequiresIndex=$((RequiresIndex + 1))
done
echo

ProvidesIndex=0
while [ "$ProvidesIndex" -lt "${#ProvidesLines[@]}" ]; do
	ProvidesText=$(awk -F'\t' -v P="P$ProvidesIndex" '$1 == P { print $2; exit }' "$Out")
	printf '%-18s' "$ProvidesText"
	RequiresIndex=0
	while [ "$RequiresIndex" -lt "${#RequiresLines[@]}" ]; do
		Verdict=$(awk -F'\t' -v P="P$ProvidesIndex" -v R="R$RequiresIndex" \
			'$1 == P && $3 == R { print $4 }' "$Out")
		printf '%-5s' "$Verdict"
		RequiresIndex=$((RequiresIndex + 1))
	done
	echo
	ProvidesIndex=$((ProvidesIndex + 1))
done

echo
echo '需求者含义：'
RequiresIndex=0
while [ "$RequiresIndex" -lt "${#RequiresLines[@]}" ]; do
	printf '  R%-3s %s\n' "$RequiresIndex" "${RequiresLines[$RequiresIndex]}"
	RequiresIndex=$((RequiresIndex + 1))
done