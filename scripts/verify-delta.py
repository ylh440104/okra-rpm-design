#!/usr/bin/env python3
# verify-delta.py - 严格测量"补几个库"的收益。
#
# 只扫一次 objdump（慢），然后用两套库集合分别评估（快）。
# 这样 before/after 用的是同一份扫描结果，差异只来自库集合，可信。
#
# 为什么不用 analyze-correct.py 跑两遍：
#   跑两遍要扫两次 rootfs（几百次 objdump），慢，而且两次扫描之间
#   rootfs 如果变了（比如中途装了库），before 的数字就不可比。
#
# 用法：verify-delta.py <primary.xml> <okra-rootfs> [库名...]

import os
import sys
import xml.etree.ElementTree as ET

ScriptDir = os.path.dirname(os.path.abspath(__file__))
CorePath = os.path.join(ScriptDir, 'analyze-correct.py')

# 复用 analyze-correct.py 的扫描和解析，不重复实现。
# 注意：analyze-correct.py 的 main 在 __main__ 保护里，exec 时不会跑。
Source = open(CorePath).read().replace("if __name__ == '__main__':", "if False:")
exec(Source)

NS = {
	'common': 'http://linux.duke.edu/metadata/common',
	'rpm': 'http://linux.duke.edu/metadata/rpm',
}


def CollectRequirements(xml_path):
	"""
	收集每个 aarch64 包的依赖列表。
	返回 [(包名, [(soname, symbol), ...]), ...]。
	symbol 为空串表示无符号版本约束。
	"""
	Root = ET.parse(xml_path).getroot()
	Packages = []

	for Package in Root.findall('common:package', NS):
		ArchElement = Package.find('common:arch', NS)
		if (ArchElement.text if ArchElement is not None else '?') == 'noarch':
			continue

		NameElement = Package.find('common:name', NS)
		Name = NameElement.text if NameElement is not None else '?'

		Required = []
		FormatElement = Package.find('common:format', NS)
		if FormatElement is not None:
			RequiresElement = FormatElement.find('rpm:requires', NS)
			if RequiresElement is not None:
				for Entry in RequiresElement.findall('rpm:entry', NS):
					Parsed = ParseRequirement(Entry.get('name'))
					if Parsed is not None:
						Required.append(Parsed)

		Packages.append((Name, Required))

	return Packages


def Evaluate(Packages, Libraries):
	"""
	判定逻辑与 analyze-correct.py 完全一致：
	  soname 必须在库里，且（若指定了）符号版本也必须在。
	返回可直接用的包数。
	"""
	Available = 0
	for _, Required in Packages:
		Ok = True
		for Soname, Symbol in Required:
			if Soname not in Libraries:
				Ok = False
				break
			if Symbol and Symbol not in Libraries[Soname]:
				Ok = False
				break
		if Ok:
			Available += 1
	return Available


def main():
	if len(sys.argv) < 3:
		print('用法: verify-delta.py <primary.xml> <okra-rootfs> [库名...]')
		return 1

	XmlPath = sys.argv[1]
	RootPath = sys.argv[2]
	Added = set(sys.argv[3:])

	print('扫 Okra rootfs（只扫一次）……')
	Full = ScanOkraLibraries(RootPath)
	print(f'  {len(Full)} 个库')

	if not Added:
		print()
		print('没有指定要测的库，只报告当前状态。')
		Packages = CollectRequirements(XmlPath)
		High = Evaluate(Packages, Full)
		Noarch = 42440
		Grand = Noarch + len(Packages)
		print(f'  aarch64 可用 {High} / {len(Packages)}')
		print(f'  总可用 {Noarch + High} / {Grand} = '
		      f'{(Noarch + High) * 100.0 / Grand:.1f}%')
		return 0

	Missing = Added - set(Full)
	if Missing:
		print()
		print(f'警告：rootfs 里找不到 {sorted(Missing)}')
		print('  这些库的贡献会被算成 0。')

	print()
	print('解析 primary.xml……')
	Packages = CollectRequirements(XmlPath)
	print(f'  {len(Packages)} 个 aarch64 包')
	print()

	Before = {Key: Value for Key, Value in Full.items() if Key not in Added}

	HighBefore = Evaluate(Packages, Before)
	HighAfter = Evaluate(Packages, Full)
	Delta = HighAfter - HighBefore

	Noarch = 42440
	Grand = Noarch + len(Packages)

	print('=' * 74)
	print('前后对比')
	print('=' * 74)
	print()
	print(f'{"":<10}{"aarch64 可用":>14}{"缺库":>10}{"总可用":>12}{"总占比":>11}')
	print(f'{"补之前":<10}{HighBefore:>14}{len(Packages) - HighBefore:>10}'
	      f'{Noarch + HighBefore:>12}{(Noarch + HighBefore) * 100.0 / Grand:>10.1f}%')
	print(f'{"补之后":<10}{HighAfter:>14}{len(Packages) - HighAfter:>10}'
	      f'{Noarch + HighAfter:>12}{(Noarch + HighAfter) * 100.0 / Grand:>10.1f}%')
	print(f'{"增量":<10}{Delta:>+14}{-Delta:>+10}{Delta:>+12}'
	      f'{Delta * 100.0 / Grand:>+10.1f}%')
	print()

	print('每个库单独的贡献：')
	for Lib in sorted(Added):
		Without = {Key: Value for Key, Value in Full.items() if Key != Lib}
		Contribution = HighAfter - Evaluate(Packages, Without)
		print(f'  {Lib:<24} {Contribution:>5} 个包')
	print()
	print(f'（单贡献之和通常大于总增量，因为很多包同时需要多个库）')
	print(f'三个一起：{Delta} 个包')
	print()
	print('=' * 74)

	return 0


if __name__ == '__main__':
	sys.exit(main())