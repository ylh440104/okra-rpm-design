#!/usr/bin/env python3
# analyze-incremental.py - 增量分析：补上哪些库最划算。
#
# 读 primary.xml，统计每个 aarch64 包缺哪些 .so。然后按"补上某几个库能解锁
# 多少包"排序。这是决定投入顺序的依据。
#
# 用法：analyze-incremental.py <primary.xml> <okra-rootfs>

import os
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter

NS = {
	'common': 'http://linux.duke.edu/metadata/common',
	'rpm': 'http://linux.duke.edu/metadata/rpm',
}


def LoadSonames(sysroot):
	Names = set()
	for Root, Dirs, Files in os.walk(sysroot):
		for Name in Files:
			if '.so' in Name:
				Names.add(Name)
	return Names


def IsLibrary(Name):
	if not Name:
		return False
	Base = Name.split('()')[0]
	return Base.endswith('.so') or bool(re.search(r'\.so(\.\d+)+$', Base))


def IsVirtual(Name):
	if not Name:
		return False
	if Name.startswith('/'):
		return True
	if Name.split('()')[0].endswith('.so'):
		return False
	if '(' in Name:
		return True
	return False


def main():
	if len(sys.argv) < 3:
		print('用法: analyze-incremental.py <primary.xml> <okra-rootfs>')
		return 1

	OkraSonames = LoadSonames(sys.argv[2])
	Tree = ET.parse(sys.argv[1])
	Root = Tree.getroot()

	# 收集每个 aarch64 包的缺失库集合
	Packages = []          # [(name, frozenset(missing))]
	AllMissing = Counter()

	for Package in Root.findall('common:package', NS):
		ArchElement = Package.find('common:arch', NS)
		Arch = ArchElement.text if ArchElement is not None else '?'
		if Arch == 'noarch':
			continue

		NameElement = Package.find('common:name', NS)
		Name = NameElement.text if NameElement is not None else '?'

		FormatElement = Package.find('common:format', NS)
		Missing = set()
		if FormatElement is not None:
			RequiresElement = FormatElement.find('rpm:requires', NS)
			if RequiresElement is not None:
				for Entry in RequiresElement.findall('rpm:entry', NS):
					Requirement = Entry.get('name')
					if not IsLibrary(Requirement) or IsVirtual(Requirement):
						continue
					Soname = Requirement.split('()')[0]
					if Soname not in OkraSonames:
						Missing.add(Soname)

		if Missing:
			Packages.append((Name, frozenset(Missing)))
			for Item in Missing:
				AllMissing[Item] += 1

	Total = len(Packages)
	print('=' * 72)
	print('增量分析：补上哪些库最划算')
	print('=' * 72)
	print()
	print(f'aarch64 包总数（含缺库的）：{Total}')
	print()

	# 1. 单个库的影响
	print('单个库的影响（补上它，能让多少个包变可用）：')
	print(f'  {"库":<44} {"解锁包数":>8}')
	SingleRank = []
	for Lib, Count in AllMissing.most_common():
		# 只有那些"唯一缺这个库"的包才真正被解锁
		Exclusive = sum(1 for _, Missing in Packages if Missing == {Lib})
		SingleRank.append((Lib, Count, Exclusive))
	for Lib, Count, Exclusive in SingleRank[:20]:
		print(f'  {Lib:<44} {Exclusive:>8}   （共 {Count} 个包依赖它）')
	print()

	# 2. 贪心：逐步补库，看累积解锁多少
	print('贪心累积：每次补上当前解锁最多的库')
	print(f'  {"轮":>3} {"补上的库":<42} {"本轮解锁":>8} {"累计可用":>8} {"占比":>7}')

	Remaining = list(Packages)
	Chosen = []
	BaseTotal = 7591      # 已经可用的 aarch64 包数
	GrandTotal = BaseTotal + Total

	while Remaining and len(Chosen) < 15:
		# 找出"补上它能让最多剩余包变可用"的库
		BestLib = None
		BestGain = 0
		for Lib, _, _ in SingleRank:
			if Lib in Chosen:
				continue
			Gain = sum(1 for _, Missing in Remaining if Missing <= {Lib} | set(Chosen))
			if Gain > BestGain:
				BestGain = Gain
				BestLib = Lib
		if BestLib is None or BestGain == 0:
			break

		Chosen.append(BestLib)
		ChosenSet = set(Chosen)
		NewRemaining = []
		Unlocked = 0
		for Name, Missing in Remaining:
			if Missing <= ChosenSet:
				Unlocked += 1
			else:
				NewRemaining.append((Name, Missing))
		Remaining = NewRemaining

		Available = BaseTotal + (Total - len(Remaining))
		print(f'  {len(Chosen):>3} {BestLib:<42} {Unlocked:>8} {Available:>8} '
		      f'{Available * 100.0 / GrandTotal:>6.1f}%')

	print()

	# 3. 关键结论
	print('=' * 72)
	print('结论')
	print('=' * 72)
	print()

	Libstdcpp = AllMissing.get('libstdc++.so.6', 0)
	Libgcc = AllMissing.get('libgcc_s.so.1', 0)
	Both = sum(1 for _, Missing in Packages
	           if Missing <= {'libstdc++.so.6', 'libgcc_s.so.1'})
	print(f'libstdc++.so.6 被 {Libstdcpp} 个包需要')
	print(f'libgcc_s.so.1  被 {Libgcc} 个包需要')
	print(f'只缺这两个库的包有 {Both} 个')
	print()
	print('这两个库随 gcc 一起来。Okra 有 gcc 16.2.0 包，')
	print('补上它们是一次性动作，收益是上面这个数。')
	print()

	Rest = Total - Both
	print(f'剩下的 {Rest} 个包缺的是完整桌面栈（glib、Qt、X11、cairo…），')
	print('那不是补一两个库能解决的，需要连依赖一起重构建。')
	print('=' * 72)

	return 0


if __name__ == '__main__':
	sys.exit(main())