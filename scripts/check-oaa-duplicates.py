#!/usr/bin/env python3
# check-oaa-duplicates.py - 检查 OAA 包里的重复文件。
#
# 读 tar 归档，按内容哈希分组，找出被存了多遍的同一份文件。
#
# 为什么要查这个：
#   OAA 是 tar 归档。如果打包时把符号链接（libfoo.so.6 -> libfoo.so.6.0.36）
#   当成真实文件抄了一遍，包体积会成倍膨胀，装到系统上也会占几倍空间。
#   调用 ldconfig 时还会把同一个库注册多次。
#
# 用法：check-oaa-duplicates.py <包路径.oaa> [更多包...]

import collections
import hashlib
import sys
import tarfile


def Scan(path):
	"""扫一个 tar 归档，返回 (重复组, 符号链接, 文件数, 总字节)。"""
	ByHash = collections.defaultdict(list)
	Links = []
	Total = 0
	TotalBytes = 0

	# OAA 可能是 zstd 压缩的 tar，也可能是裸 tar。
	# 先试 tarfile 自动检测，失败则用 zstd 解压管道。
	try:
		Archive = tarfile.open(path, 'r:*')
	except Exception:
		# zstd 压缩的 tar
		import subprocess
		import tempfile
		# 直接用管道解压
		Proc = subprocess.Popen(
			['zstd', '-d', '-c', path],
			stdout=subprocess.PIPE)
		Archive = tarfile.open(fileobj=Proc.stdout, mode='r|')

	try:
		for Member in Archive:
			if not Member.isfile():
				if Member.issym() or Member.islnk():
					Links.append((Member.name, Member.linkname))
				continue
			Total += 1
			TotalBytes += Member.size

			# 只对大文件算哈希，小文件重复无所谓
			if Member.size < 4096:
				continue

			Handle = Archive.extractfile(Member)
			if Handle is None:
				continue
			Digest = hashlib.sha256()
			while True:
				Chunk = Handle.read(1024 * 1024)
				if not Chunk:
					break
				Digest.update(Chunk)
			ByHash[Digest.hexdigest()].append((Member.name, Member.size))
	finally:
		Archive.close()

	return ByHash, Links, Total, TotalBytes


def Report(path):
	print('=' * 74)
	print('检查 ' + path)
	print('=' * 74)

	ByHash, Links, Total, TotalBytes = Scan(path)

	print('  文件数：' + str(Total))
	print('  解包后总字节：{:,}（{:.1f} MB）'.format(TotalBytes, TotalBytes / 1024 / 1024))
	print('  符号链接数：' + str(len(Links)))
	print()

	Waste = 0
	Groups = 0
	Rows = []

	for Digest, Members in ByHash.items():
		if len(Members) < 2:
			continue
		Groups += 1
		DuplicateBytes = sum(Size for _, Size in Members[1:])
		Waste += DuplicateBytes
		Rows.append((DuplicateBytes, Members))

	if not Rows:
		print('  没有发现重复的大文件。')
		print()
		print('  这说明打包时正确识别了硬链接/符号链接。')
		return

	Rows.sort(key=lambda Item: -Item[0])

	print('  重复组数：' + str(Groups))
	print('  浪费字节：{:,}（{:.1f} MB）'.format(Waste, Waste / 1024 / 1024))
	print('  占比：{:.1f}%'.format(Waste * 100.0 / TotalBytes))
	print()
	print('  重复最多的前 15 组：')
	for DuplicateBytes, Members in Rows[:15]:
		Size = Members[0][1]
		print('    同一份 {:>12,} 字节 × {} 遍（浪费 {:>12,}）'.format(
			Size, len(Members), DuplicateBytes))
		for Name, _ in Members[:4]:
			print('      ' + Name)
		if len(Members) > 4:
			print('      …… 还有 {} 个'.format(len(Members) - 4))
		print()

	if Links:
		print('  符号链接（正常）：')
		for Name, Target in Links[:10]:
			print('    ' + Name + ' -> ' + Target)
	else:
		print('  归档里没有任何符号链接。')
		print('  这是个信号：打包工具把链接当成了真实文件。')


def main():
	if len(sys.argv) < 2:
		print('用法: check-oaa-duplicates.py <包.oaa> [更多包...]')
		return 1
	for Path in sys.argv[1:]:
		Report(Path)
		print()
	return 0


if __name__ == '__main__':
	sys.exit(main())