#!/bin/bash
# 摸清 RPM 依赖匹配的 release 规则。
# 期望值来源：真实 rpm 依赖求解。

set -u

Work=/root/rl3
R=$Work/RPMS/noarch

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

MakeSpec C0 'Provides: virt = 2.0'
MakeSpec C1 'Provides: virt = 2.0-5'
MakeSpec D0 'Requires: virt >= 1.0-1'
MakeSpec D1 'Requires: virt < 1.0-1'
MakeSpec D2 'Requires: virt = 1.0-1'
MakeSpec D3 'Requires: virt >= 3.0-1'
MakeSpec D4 'Requires: virt <= 3.0-1'
MakeSpec D5 'Requires: virt > 3.0-1'
MakeSpec D6 'Requires: virt = 1.0'

for Spec in "$Work"/SPECS/*.spec; do
	rpmbuild --define "_topdir $Work" -bb "$Spec" >/dev/null 2>&1
done

echo "产物 $(ls "$R" | wc -l) 个"
echo

printf '%-20s' 'Provides \ Requires'
for D in D0 D1 D2 D3 D4 D5 D6; do
	printf '%-6s' "$D"
done
echo

for C in C0 C1; do
	rm -rf /root/r3db
	mkdir -p /root/r3db
	rpm -i --dbpath /root/r3db --nodeps --noscripts --justdb "$R/$C-1.0-1.noarch.rpm" 2>/dev/null
	Prov=$(rpm -q --dbpath /root/r3db --provides "$C" 2>/dev/null | grep '^virt' | tr -d '\n')
	printf '%-20s' "$Prov"
	for D in D0 D1 D2 D3 D4 D5 D6; do
		if rpm -i --test --dbpath /root/r3db "$R/$D-1.0-1.noarch.rpm" >/dev/null 2>&1; then
			printf '%-6s' '满足'
		else
			printf '%-6s' '失败'
		fi
	done
	echo
done

echo
echo '需求含义：D0 >=1.0-1  D1 <1.0-1  D2 =1.0-1  D3 >=3.0-1  D4 <=3.0-1  D5 >3.0-1  D6 =1.0'