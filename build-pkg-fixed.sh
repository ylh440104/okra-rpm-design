#!/bin/bash
set -u
NAME="$1"; VER="$2"; URL="$3"; shift 3
EXTRA=("$@")
WORK=~/okra/work; OAA_OUT=~/okra/oaa-out
TRIPLE=aarch64-okra-linux-gnu
OAA=~/okra/okrapm/oaatools/oaatools
mkdir -p "$WORK" "$OAA_OUT"
DEST="/tmp/${NAME}-root"
PKG="$WORK/${NAME}-pkg"
TAR="$WORK/$(basename "$URL")"
SRC="$WORK/${NAME}-src"
log(){ echo; echo "=== [$NAME-$VER] $1 ==="; }
log 下载; [ -f "$TAR" ] || wget -q -c "$URL" -O "$TAR" || exit 1
log 解压; rm -rf "$SRC"; mkdir -p "$SRC"; tar xf "$TAR" -C "$SRC" --strip-components=1
log configure; cd "$SRC"
./configure --prefix=/usr --build="$TRIPLE" "${EXTRA[@]}" > /tmp/$NAME.conf.log 2>&1 || { echo C失败; exit 1; }
log 编译; make -j6 > /tmp/$NAME.make.log 2>&1 || { echo M失败; exit 1; }
log 安装; rm -rf "$DEST"; make DESTDIR="$DEST" install > /tmp/$NAME.inst.log 2>&1 || { echo I失败; exit 1; }
log OAA组装; rm -rf "$PKG"; mkdir -p "$PKG/rootfs"
# 修正：用 tar 管道替代 cp -a，在 proot 环境下可靠地保留符号链接。
# cp -a 在 proot 里可能把符号链接复制成真实文件（见 feasibility.md 第 7 节）。
( cd "$DEST" && tar cf - . ) | ( cd "$PKG/rootfs" && tar xf - )
SIZE=$(du -sm "$PKG/rootfs" | cut -f1)
printf "name: %s\nnamespace: app\nversion: %s\ndescription: OAA pkg\narchitecture: aarch64\nmaintainer: OkraLinux\ninstalled_size: %s\ndependencies: []\nfiles:\n  - /usr/bin/%s\n" "$NAME" "$VER" "$SIZE" "$NAME" > "$PKG/meta.yaml"
log 打包; OUT="$OAA_OUT/${NAME}-${VER}-1.aarch64.oaa"
$OAA build "$PKG" -o "$OUT" && $OAA verify "$OUT" || exit 1
log 清理; rm -rf "$SRC" "$DEST" "$PKG" "$TAR"
df -h / | tail -1
log 完成: $OUT