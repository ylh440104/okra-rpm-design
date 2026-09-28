#!/usr/bin/env python3
# analyze-incremental2.py - 修正版增量分析。
#
# 用正确的 soname 提取（第一个 '(' 之前），按"补上某几个库能解锁多少包"排序。
#
# 用法：analyze-incremental2.py <primary.xml> <okra-rootfs>

import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import Counter

NS = {
	'common': 'http://linux.duke.edu/metadata/common',
	'rpm': 'http://linux.duke.edu/metadata/rpm',
}

VIRTUAL_PREFIXES = (
	'rpmlib(', 'config(', 'pkgconfig(', 'perl(', 'ruby(', 'php(', 'java',
	'libreoffice(', 'tex(', 'font(', 'application(', 'shared-library(',
	'module(', 'bundled(', 'idris(', 'ocaml(', 'mono(', 'nodejs(',
	'osgi(', 'namespace(', 'python', 'ghc-', 'wine-', 'kmod(',
)

REQUIRE_PATTERN = re.compile(
	r'^(?P<soname>[^()]+\.so(?:\.\d+)*)\((?P<symbol>[^()]*)\)\((?P<bits>\d+bit)\)$')


def IsVirtual(Name):
	if not Name:
		return True
	if Name.startswith('/'):
		return True
	for Prefix in VIRTUAL_PREFIXES:
		if Name.startswith(Prefix):
			return True
	Head = Name.split('(')[0]
	if '(' in Name and not Head.endswith('.so') and not re.search(r'\.so\.\d+$', Head):
		return True
	return False


def ParseRequirement(Name):
	if IsVirtual(Name):
		return None
	Match = REQUIRE_PATTERN.match(Name)
	if Match:
		return (Match.group('soname'), Match.group('symbol'))
	if re.search(r'\.so(\.\d+)*$', Name):
		return (Name, '')
	return None


def ScanOkra(sysroot):
	Libraries = {}
	for Root, Dirs, Files in os.walk(sysroot):
		for Name in Files:
			if '.so' not in Name:
				continue
			Path = os.path.join(Root, Name)
			if os.path.islink(Path):
				continue
			try:
				with open(Path, 'rb') as Handle:
					if Handle.read(4) != b'\x7fELF':
						continue
			except OSError:
				continue
			try:
				Output = subprocess.run(['objdump', '-T', Path],
				                        capture_output=True, text=True, timeout=60).stdout
			except (OSError, subprocess.SubprocessError):
				Output = ''
			Symbols = set(re.findall(
				r'\b((?:GLIBC|GLIBCXX|CXXABI|GCC|GOMP)_[0-9][0-9.]*)\b', Output))
			if Name in Libraries:
				Libraries[Name] |= Symbols
			else:
				Libraries[Name] = Symbols
	return Libraries


def main():
	if len(sys.argv) < 3:
		print('用法: analyze-incremental2.py <primary.xml> <okra-rootfs>')
		return 1

	OkraLibraries = ScanOkra(sys.argv[2])
	Root = ET.parse(sys.argv[1]).getroot()

	Packages = []
	AllMissing = Counter()

	for Package in Root.findall('common:package', NS):
		ArchElement = Package.find('common:arch', NS)
		if (ArchElement.text if ArchElement is not None else '?') == 'noarch':
			continue
		NameElement = Package.find('common:name', NS)
		Name = NameElement.text if NameElement is not None else '?'
		FormatElement = Package.find('common:format', NS)

		Missing = set()
		if FormatElement is not None:
			RequiresElement = FormatElement.find('rpm:requires', NS)
			if RequiresElement is not None:
				for Entry in RequiresElement.findall('rpm:entry', NS):
					Parsed = ParseRequirement(Entry.get('name'))
					if Parsed is None:
						continue
					Soname, Symbol = Parsed
					if Soname not in OkraLibraries:
						Missing.add(Soname)
					elif Symbol and Symbol not in OkraLibraries[Soname]:
						Missing.add(f'{Soname}({Symbol})')

		if Missing:
			Packages.append((Name, frozenset(Missing)))
			for Item in Missing:
				AllMissing[Item] += 1

	Total = len(Packages)
	Base = 8348          # 已可用的 aarch64 包
	Grand = Base + Total

	print('=' * 74)
	print('修正版增量分析')
	print('=' * 74)
	print()
	print(f'缺东西的 aarch64 包：{Total}')
	print(f'已可用的 aarch64 包：{Base}')
	print(f'aarch64 合计：{Grand}')
	print()

	# 只缺一两个库的包，最值得先补
	print('按"缺几个库"分布：')
	CountBySize = Counter(len(Missing) for _, Missing in Packages)
	for Size in sorted(CountBySize):
		Count = CountBySize[Size]
		print(f'  缺 {Size:>2} 个库：{Count:>6} 个包')
	print()

	# 关键组合：只缺 gcc 运行时
	GccRuntime = {'libgcc_s.so.1', 'libstdc++.so.6'}
	OnlyGccRuntime = sum(1 for _, Missing in Packages if Missing <= GccRuntime)
	print(f'只缺 gcc 运行时（libgcc_s + libstdc++）的包：{OnlyGccRuntime}')
	print()

	# 贪心累积
	print('贪心累积（每次补当前解锁最多的库）：')
	print(f'  {"轮":>3} {"库":<40} {"本轮解锁":>8} {"累计可用":>8} {"占 aarch64":>10}')

	Remaining = list(Packages)
	Chosen = []
	LibRank = [Item for Item, _ in AllMissing.most_common()]

	while Remaining and len(Chosen) < 20:
		BestLib = None
		BestGain = 0
		ChosenSet = set(Chosen)
		for Lib in LibRank:
			if Lib in ChosenSet:
				continue
			Gain = sum(1 for _, Missing in Remaining
			           if Missing <= ChosenSet | {Lib})
			if Gain > BestGain:
				BestGain = Gain
				BestLib = Lib
		if BestLib is None or BestGain == 0:
			break

		Chosen.append(BestLib)
		ChosenSet = set(Chosen)
		Unlocked = 0
		NewRemaining = []
		for Name, Missing in Remaining:
			if Missing <= ChosenSet:
				Unlocked += 1
			else:
				NewRemaining.append((Name, Missing))
		Remaining = NewRemaining

		Available = Base + (Total - len(Remaining))
		print(f'  {len(Chosen):>3} {BestLib:<40} {Unlocked:>8} {Available:>8} '
		      f'{Available * 100.0 / Grand:>9.1f}%')

	print()
	print('=' * 74)
	print('结论')
	print('=' * 74)
	print()
	print(f'补上 gcc 运行时两个库，aarch64 可用数从 {Base} 到 {Base + OnlyGccRuntime}')
	print(f'  即 {Base * 100.0 / Grand:.1f}% -> {(Base + OnlyGccRuntime) * 100.0 / Grand:.1f}%')
	print()
	print('这是投入产出比最高的一步：Okra 已经有 gcc 16.2.0 包，')
	print('libgcc_s 和 libstdc++ 本来就该随它一起装。')
	print()
	print('剩下的包缺的是完整桌面栈（glib、Qt、X11、cairo、zlib…），')
	print('要连依赖一起重构建，不是补几个库能解决的。')
	print('=' * 74)

	return 0


if __name__ == '__main__':
	sys.exit(main())