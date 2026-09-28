#!/bin/bash
# 摸清 RPM 依赖匹配的 epoch 规则，以及确认 release 通配与比较符无关。
# 期望值来源：真实 rpm 依赖求解。

set -u

Work=/root/ep
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

# 提供者
MakeSpec E0 'Provides: virt = 1:2.0'
MakeSpec E1 'Provides: virt = 2.0'
# 需求者
MakeSpec F0 'Requires: virt = 2.0'
MakeSpec F1 'Requires: virt = 1:2.0'
MakeSpec F2 'Requires: virt >= 2.0'
MakeSpec F3 'Requires: virt >= 1:2.0'
MakeSpec F4 'Requires: virt < 2.0'
MakeSpec F5 'Requires: virt <= 2.0'
MakeSpec F6 'Requires: virt > 2.0'

for Spec in "$Work"/SPECS/*.spec; do
	rpmbuild --define "_topdir $Work" -bb "$Spec" >/dev/null 2>&1
done

echo "产物 $(ls "$R" | wc -l) 个"
echo

printf '%-22s' 'Provides \ Requires'
for F in F0 F1 F2 F3 F4 F5 F6; do
	printf '%-6s' "$F"
done
echo

for E in E0 E1; do
	rm -rf /root/epdb
	mkdir -p /root/epdb
	rpm -i --dbpath /root/epdb --nodeps --noscripts --justdb "$R/$E-1.0-1.noarch.rpm" 2>/dev/null
	Prov=$(rpm -q --dbpath /root/epdb --provides "$E" 2>/dev/null | grep '^virt' | tr -d '\n')
	printf '%-22s' "$Prov"
	for F in F0 F1 F2 F3 F4 F5 F6; do
		if rpm -i --test --dbpath /root/epdb "$R/$F-1.0-1.noarch.rpm" >/dev/null 2>&1; then
			printf '%-6s' '满足'
		else
			printf '%-6s' '失败'
		fi
	done
	echo
done

echo
echo '需求含义：F0 =2.0  F1 =1:2.0  F2 >=2.0  F3 >=1:2.0  F4 <2.0  F5 <=2.0  F6 >2.0'