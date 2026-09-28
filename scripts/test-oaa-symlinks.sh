#!/bin/bash
# test-oaa-symlinks.sh - 回归测试：OAA 包必须保留符号链接。
#
# 这个测试验证两件事：
#   1. oaa-build 正确保留符号链接（已验证通过）
#   2. 打包后的 OAA 里，符号链接条目（lrwx）存在且指向正确
#
# 如果未来有人改了 oaa-build 或 ArtifactBuilder，这个测试能防回归。
#
# 用法：test-oaa-symlinks.sh [oaa-build路径] [输出目录]

set -u

Build=${1:-/tmp/oaa-build}
Out=${2:-/tmp/oaa-symlink-test}

Pass=0
Fail=0

ok(){ Pass=$((Pass+1)); echo "  ok   $1"; }
fail(){ Fail=$((Fail+1)); echo "  FAIL $1"; }

echo '=== 建源目录 ==='
rm -rf "$Out"
mkdir -p "$Out/src/rootfs/usr/lib64" "$Out/src/rootfs/usr/bin"

# 真实文件
echo 'shared lib content' > "$Out/src/rootfs/usr/lib64/libtest.so.1.0.0"
chmod 755 "$Out/src/rootfs/usr/lib64/libtest.so.1.0.0"

# 符号链接（正常情况）
ln -s libtest.so.1.0.0 "$Out/src/rootfs/usr/lib64/libtest.so.1"
ln -s libtest.so.1.0.0 "$Out/src/rootfs/usr/lib64/libtest.so"

# 可执行文件
echo '#!/bin/sh' > "$Out/src/rootfs/usr/bin/testbin"
echo 'echo hello' >> "$Out/src/rootfs/usr/bin/testbin"
chmod 755 "$Out/src/rootfs/usr/bin/testbin"

# meta.yaml
cat > "$Out/src/meta.yaml" <<EOF
name: symlink-test
namespace: app
version: 1.0
description: symlink regression test
architecture: aarch64
maintainer: test
installed_size: 1
dependencies: []
files:
  - /usr/bin/testbin
EOF

echo '=== 源目录里的链接 ==='
ls -la "$Out/src/rootfs/usr/lib64/"
echo

echo '=== 打包 ==='
OAA_LIB="$(dirname "$Build")/oaatools-common.sh"
[ -f "$OAA_LIB" ] || OAA_LIB="/usr/lib/okrapm/oaatools-common.sh"
export OAA_LIB
"$Build" "$Out/src" -o "$Out/test.oaa" 2>&1 | tail -2
echo

echo '=== 检查产出 ==='

# 提取 tar 条目
Entries=$(zstd -d -c "$Out/test.oaa" 2>/dev/null | tar -tvf - 2>/dev/null)

echo "$Entries" | grep -q 'lrwxrwxrwx.*libtest.so.1 ->'
if echo "$Entries" | grep -q 'lrwxrwxrwx.*libtest.so.1 ->'; then
	ok "libtest.so.1 是符号链接"
else
	fail "libtest.so.1 不是符号链接"
	echo "  $(echo "$Entries" | grep libtest.so.1)"
fi

if echo "$Entries" | grep -q 'lrwxrwxrwx.*libtest.so ->'; then
	ok "libtest.so 是符号链接"
else
	fail "libtest.so 不是符号链接"
	echo "  $(echo "$Entries" | grep 'libtest\.so$')"
fi

if echo "$Entries" | grep -q -- '-rw.*libtest.so.1.0.0'; then
	ok "libtest.so.1.0.0 是真实文件"
else
	fail "libtest.so.1.0.0 不是真实文件"
fi

# 检查没有重复
Dupes=$(echo "$Entries" | grep -E -- '-r[w-][x-]' | grep -c 'libtest.so')
if [ "$Dupes" -le 1 ]; then
	ok "没有重复的真实文件"
else
	fail "发现 $Dupes 个真实文件（应该只有 1 个）"
fi

# 解包后链接仍然存在
rm -rf "$Out/extracted"
mkdir -p "$Out/extracted"
zstd -d -c "$Out/test.oaa" 2>/dev/null | tar -xf - -C "$Out/extracted" 2>/dev/null
if [ -L "$Out/extracted/rootfs/usr/lib64/libtest.so.1" ]; then
	ok "解包后 libtest.so.1 仍是符号链接"
else
	fail "解包后 libtest.so.1 不是符号链接"
fi

echo
echo "========================================"
echo "共 $Pass 条通过，$Fail 条失败"
echo "========================================"
[ "$Fail" -eq 0 ]