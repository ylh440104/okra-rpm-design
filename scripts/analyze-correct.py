#!/usr/bin/env python3
# analyze-correct.py - 正确版的仓库可用性分析。
#
# 前两版都有 bug，本文档记录正确的做法。
#
# RPM 的 soname 依赖带符号版本，形态有三种：
#
#   libstdc++.so.6()(64bit)                无符号版本约束
#   libstdc++.so.6(CXXABI_1.3)(64bit)      带符号版本约束
#   libc.so.6(GLIBC_2.38)(64bit)           带符号版本约束
#
# 所以：
#   1. soname 是**第一个 '(' 之前**的部分。
#      用 split('()')[0] 是错的：libstdc++.so.6(CXXABI_1.3)(64bit)
#      会被切成 libstdc++.so.6(CXXABI_1.3)。
#   2. 符号版本也要校验。libc.so.6 存在还不够，还得看 Okra 的 libc 是否
#      真的导出 GLIBC_2.38。这一步用 objdump -T 扫 Okra 的 .so 得到。
#
# 用法：analyze-correct.py <primary.xml> <okra-rootfs>

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
	"""返回 (soname, symbol) 或 None。symbol 为空串表示无符号版本约束。"""
	if IsVirtual(Name):
		return None
	Match = REQUIRE_PATTERN.match(Name)
	if Match:
		return (Match.group('soname'), Match.group('symbol'))
	if re.search(r'\.so(\.\d+)*$', Name):
		return (Name, '')
	return None


def ScanOkraLibraries(sysroot):
	"""
	返回 {名字: set(符号版本)}。

	**关键**：一个库要同时用三个名字注册，否则会漏。

	  真实文件   libgcc_s-15-20250329.so.1
	  符号链接   libgcc_s.so.1 -> 上面那个
	  内嵌 SONAME（通常和符号链接同名）

	程序在 DT_NEEDED 里写的是 libgcc_s.so.1，也就是符号链接名。
	如果只按文件 basename 注册，就会得到 libgcc_s-15-20250329.so.1，
	程序永远找不到——而符号链接又被 islink 跳过。

	这个 bug 让上一版的数字偏低（装了库之后可用数还是 8348 没变）。
	"""
	Libraries = {}
	Links = {}

	for Root, Dirs, Files in os.walk(sysroot):
		for Name in Files:
			if '.so' not in Name:
				continue
			Path = os.path.join(Root, Name)

			if os.path.islink(Path):
				# 先记下来，等真实文件扫完再解析
				try:
					Links[Name] = os.path.realpath(Path)
				except OSError:
					pass
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

			# 名字一：文件 basename
			Names = {Name}

			# 名字二：内嵌 SONAME
			try:
				SonameOutput = subprocess.run(
					['objdump', '-p', Path],
					capture_output=True, text=True, timeout=60).stdout
				Match = re.search(r'SONAME\s+(\S+)', SonameOutput)
				if Match:
					Names.add(Match.group(1))
			except (OSError, subprocess.SubprocessError):
				pass

			for Key in Names:
				if Key in Libraries:
					Libraries[Key] |= Symbols
				else:
					Libraries[Key] = Symbols

	# 符号链接的名字指向哪个真实文件，就把符号并过去
	for LinkName, Target in Links.items():
		TargetName = os.path.basename(Target)
		Symbols = Libraries.get(TargetName, set())
		if LinkName in Libraries:
			Libraries[LinkName] |= Symbols
		else:
			Libraries[LinkName] = set(Symbols)

	return Libraries


def main():
	if len(sys.argv) < 3:
		print('用法: analyze-correct.py <primary.xml> <okra-rootfs>')
		return 1

	print('扫 Okra rootfs 的库与符号版本……')
	OkraLibraries = ScanOkraLibraries(sys.argv[2])
	print(f'  {len(OkraLibraries)} 个库')

	LibcSymbols = OkraLibraries.get('libc.so.6', set())
	GlibcOnly = [Item for Item in LibcSymbols if Item.startswith('GLIBC_')]
	MaxGlibc = '(无)'
	if GlibcOnly:
		MaxGlibc = max(GlibcOnly,
		               key=lambda Item: tuple(int(Part) for Part in Item[6:].split('.')))
	print(f'  libc.so.6 的 GLIBC 上限：{MaxGlibc}')
	print()

	print('解析 primary.xml……')
	Root = ET.parse(sys.argv[1]).getroot()
	print('  完成')
	print()

	Total = 0
	ByArch = Counter()
	Verdict = Counter()
	ByArchVerdict = {}
	MissingLib = Counter()
	MissingSymbol = Counter()
	Reasons = Counter()

	for Package in Root.findall('common:package', NS):
		Total += 1
		NameElement = Package.find('common:name', NS)
		ArchElement = Package.find('common:arch', NS)
		FormatElement = Package.find('common:format', NS)
		Arch = ArchElement.text if ArchElement is not None else '?'
		ByArch[Arch] += 1

		if Arch == 'noarch':
			Verdict['高'] += 1
			Reasons['noarch'] += 1
			ByArchVerdict.setdefault(Arch, Counter())['高'] += 1
			continue

		MissingLibrary = set()
		MissingSymbolList = set()

		if FormatElement is not None:
			RequiresElement = FormatElement.find('rpm:requires', NS)
			if RequiresElement is not None:
				for Entry in RequiresElement.findall('rpm:entry', NS):
					Parsed = ParseRequirement(Entry.get('name'))
					if Parsed is None:
						continue
					Soname, Symbol = Parsed
					if Soname not in OkraLibraries:
						MissingLibrary.add(Soname)
						continue
					if Symbol and Symbol not in OkraLibraries[Soname]:
						MissingSymbolList.add(f'{Soname}({Symbol})')

		if MissingLibrary:
			Key = '低'
			Reasons['缺库'] += 1
			for Item in MissingLibrary:
				MissingLib[Item] += 1
		elif MissingSymbolList:
			Key = '中'
			Reasons['缺符号版本'] += 1
			for Item in MissingSymbolList:
				MissingSymbol[Item] += 1
		else:
			Key = '高'
			Reasons['齐全'] += 1

		Verdict[Key] += 1
		ByArchVerdict.setdefault(Arch, Counter())[Key] += 1

	print('=' * 72)
	print('Fedora 仓库可用性分析（修正版）')
	print('=' * 72)
	print()
	print(f'包总数：{Total}')
	print()
	print('架构分布：')
	for Arch, Count in ByArch.most_common():
		print(f'  {Arch:<12} {Count:>7}  {Count * 100.0 / Total:>5.1f}%')
	print()
	print('判定结果：')
	print('  高 = 可直接用    中 = 库在但符号版本不够    低 = 缺库')
	for Key in ('高', '中', '低'):
		Count = Verdict.get(Key, 0)
		print(f'  {Key:<3} {Count:>7}  {Count * 100.0 / Total:>5.1f}%')
	print()
	print('按架构：')
	for Arch in sorted(ByArchVerdict):
		Row = ByArchVerdict[Arch]
		Sum = sum(Row.values())
		Parts = '  '.join(f'{Key} {Row.get(Key, 0):>6}' for Key in ('高', '中', '低'))
		print(f'  {Arch:<10} {Parts}   合计 {Sum}')
	print()
	print('缺失最多的库（前 20）：')
	for Lib, Count in MissingLib.most_common(20):
		print(f'  {Lib:<40} {Count:>6}')
	print()
	print('缺失最多的符号版本（前 10）：')
	for Item, Count in MissingSymbol.most_common(10):
		print(f'  {Item:<40} {Count:>6}')
	if not MissingSymbol:
		print('  （无）')
	print()
	print('=' * 72)
	High = Verdict.get('高', 0) * 100.0 / Total
	Mid = Verdict.get('中', 0) * 100.0 / Total
	print(f'高 {High:.1f}%   中 {Mid:.1f}%   高+中 {High + Mid:.1f}%')
	if High >= 50:
		print('结论：值得做适配器')
	elif High + Mid >= 50:
		print('结论：值得做，但要先补符号版本')
	else:
		print('结论：改走 SRPM 重构建路线更划算')
	print('=' * 72)

	return 0


if __name__ == '__main__':
	sys.exit(main())