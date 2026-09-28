# 可行性结论

`rpm-feasibility.sh` 那个"决定性问题"的答案。用**真实的 Okra 系统**和**完整的 Fedora 仓库**得出，不是估算。

---

## 1. 结论

**Fedora 的二进制包在 Okra 上能用，比例是 77.3%。超过 50% 阈值，值得做适配器。**

| | 包数 | 占比 |
|---|---|---|
| **可用** | **52,049** | **77.3%** |
| 缺库 | 15,293 | 22.7% |

按架构拆：

| 架构 | 总数 | 可用 | 可用率 |
|---|---|---|---|
| noarch | 42,440 | 42,440 | **100%** |
| aarch64 | 24,903 | 9,609 | **38.6%** |

---

## 2. 验证方式

**不是抽样，是全量。**

数据来源：
- **Okra 系统**：从 `okralinux-7.2-disk.img` 用 `debugfs` 抽出完整 rootfs（166MB）。只读操作，没动原盘。
- **Fedora 仓库**：Fedora 42 Everything aarch64 的 `primary.xml`（169MB，解压自 `repodata`），覆盖**全部 67,343 个包**。

判定依据：包的 `Requires` 里的 soname 依赖，逐个对照 Okra rootfs 里实际存在的库和符号版本。

---

## 3. 推翻了之前的判断

我在 `rpm.md` 第 3 节写过：

> Fedora 的 rpm 里动态链接的是 Fedora glibc 的符号版本（`GLIBC_2.38`…）。Okra 用自建 glibc，符号版本表不同。这些二进制在 Okra 上会直接报 `version 'GLIBC_2.38' not found`。

**这是错的。**

| | GLIBC 符号版本上限 |
|---|---|
| **Okra**（自建 glibc，10.3MB，带 debug_info） | **GLIBC_2.43** |
| Fedora 42 需要 | GLIBC_2.38 / 2.41 |

67,343 个包里只有 **1 个**因符号版本失败，而且是 `GLIBC_PRIVATE`——glibc 内部符号，本来就不该被外部依赖。

**符号版本不是障碍。真正的障碍是缺库。**

---

## 4. 硬证据：Fedora 二进制在 Okra 上跑起来了

用 `chroot` 做真正隔离（没有宿主库回退），运行 Fedora 42 的 aarch64 二进制：

| 二进制 | 依赖 | 结果 |
|---|---|---|
| `3proxy` | libc, ld-linux | ✅ 输出用法 |
| `64tass` | libc, ld-linux | ✅ 输出 `64tass Turbo Assembler Macro V1.59.3120` |
| `2048nc` | libc, ld-linux | ✅ 输出用法 |
| `ninja`（C++） | libstdc++, libgcc_s, libc | ✅ 输出 `1.12.1` |

`ninja` 是关键的 C++ 证据。装 `libstdc++` 之前的对照：

```
装库前：error while loading shared libraries: libstdc++.so.6
装库后：1.12.1
```

加载路径全部来自 Okra：

```
libstdc++.so.6 => /usr/lib64/libstdc++.so.6    ← Okra
libgcc_s.so.1  => /lib64/libgcc_s.so.1         ← Okra
libc.so.6      => /lib64/libc.so.6             ← Okra
```

---

## 5. 已经补上了

**2026-09-27 实测补完。** 三个东西，全部来自 **Okra 自己**，没有混装 Fedora 的库。

| 步骤 | 来源 | 单独贡献 |
|---|---|---|
| `libgcc_s.so.1` | **Okra 的 `gcc-16.2.0-1.aarch64.oaa`** | 900 个包 |
| `libstdc++.so.6` | **同上** | 733 个包 |
| `libresolv.so.2` | 符号链接到 Okra 的 `libc.so.6` | 336 个包 |
| **三个一起** | | **+1,261 个包** |

单贡献之和（1,969）大于总增量（1,261），因为很多包同时需要多个。

### 前后对比

```
              aarch64 可用     缺库      总可用     总占比
补之前                8348    16555     50788     75.4%
补之后                9609    15294     52049     77.3%
增量                 +1261    -1261     +1261     +1.9%
```

### 为什么库该从 Okra 的 gcc 包来

**关键发现**：Okra 的 `gcc-16.2.0-1.aarch64.oaa` 里**本来就有这两个库**：

```
rootfs/usr/lib64/libgcc_s.so.1
rootfs/usr/lib64/libstdc++.so.6
rootfs/usr/lib64/libstdc++.so.6.0.36
```

所以这不是"从 Fedora 搬库"，而是"**Okra 自己的 gcc 包没装全**"。

我一开始用了 Fedora 的 `libgcc`/`libstdc++` 包，后来换成 Okra 自己的。区别很重要：

| | Okra 的 | Fedora 的 |
|---|---|---|
| 来源 | 自建 gcc 16.2.0 | Fedora gcc 15.0.1 |
| 符合 `oaabi.md` 第 2 节 | ✅ | ❌ 混装 |
| 版本 | `libstdc++.so.6.0.36` | `libstdc++.so.6.0.34` |

**换成 Okra 的之后，验证结果完全一样**（`ninja` 输出 `1.12.1`），加载路径也全部来自 Okra。所以用 Okra 的没有代价。

### 实测验证

`chroot` 隔离，用 Okra 的库：

| 二进制 | 依赖 | 结果 |
|---|---|---|
| `ninja`（C++） | libstdc++, libgcc_s, libc | ✅ `1.12.1` |
| `etherecho` | libresolv, libc | ✅ 输出用法 |
| `3proxy` | libc | ✅ |
| `64tass` | libc | ✅ |

加载路径：

```
libstdc++.so.6 => /usr/lib64/libstdc++.so.6    ← Okra 的 gcc 16.2.0
libgcc_s.so.1  => /lib64/libgcc_s.so.1         ← Okra 的 gcc 16.2.0
libc.so.6      => /lib64/libc.so.6             ← Okra
libm.so.6      => /lib64/libm.so.6             ← Okra
```

`ninja` 的对照实验（补库前）：

```
补之前：error while loading shared libraries: libstdc++.so.6
补之后：1.12.1
```

### libresolv 是纯符号链接

glibc 2.34 起 `libresolv` 已并入 `libc`。Okra 的 libc 确实导出了 `res_query` / `res_ninit` / `res_search`。

```
ln -s libc.so.6 /lib64/libresolv.so.2
```

实测：

```
不建链接：etherecho: error while loading shared libraries: libresolv.so.2
建链接后：Usage of /opt/test/etherecho: -i string ...     ✅
```

---

## 6. 真正的天花板

补完上面三个之后，缺失榜变成：

```
libglib-2.0.so.0        2346
libgobject-2.0.so.0     2077
libz.so.1               1827
libgio-2.0.so.0         1502
libgmp.so.10            1485
libX11.so.6             1039
libQt6Core.so.6          963
libcairo.so.2            951
libcrypto.so.3           928
```

这是**完整的桌面栈**：glib 全家、Qt5/Qt6、X11、cairo、openssl。补一两个库解决不了，需要连依赖一起重构建。

**所以 77.3% 大致就是"直接装 Fedora 二进制"的天花板。** 要突破，就得走 SRPM 重构建路线——那恰好回到 `rpm.md` 里说的另一条路。

---

## 7. 发现 Okra 打包的一个 bug

查 gcc OAA 时发现：**打包工具把符号链接当成了真实文件**。

```
包大小：778MB（zstd 压缩后）
解包后：2,219 MB
重复浪费：143 MB（6.4%）
```

23 个重复组，全部是同一个模式：

```
libfoo.so.6.0.36    ← 真实文件
libfoo.so.6         ← 应该是符号链接，但存成了同样大的真实文件
libfoo.so           ← 同上
```

受影响的库（前几名）：

| 库 | 单份大小 | 存了几遍 | 浪费 |
|---|---|---|---|
| `libstdc++.so.*` | 23.5 MB | 3 | 47 MB |
| `libasan.so.*` | 10.9 MB | 3 | 22 MB |
| `g++` / `c++` | 18.9 MB | 2 | 19 MB |
| `libtsan.so.*` | 9.2 MB | 3 | 18 MB |
| `libhwasan.so.*` | 5.1 MB | 3 | 10 MB |
| `liblsan.so.*` | 4.1 MB | 3 | 8 MB |
| `libubsan.so.*` | 3.5 MB | 3 | 7 MB |
| `libgomp.so.*` | 1.6 MB | 3 | 3 MB |

**根因**：`build-pkg.sh` 在 `make DESTDIR=... install` 之后执行了 `cp -a "$DEST"/* "$PKG/rootfs/"`。`cp -a` 在正常环境下保留符号链接，但在这台 aarch64 构建机上很可能跑在 proot 里（`bootstrap-toolkit.md` 提到 proot 把硬链接显示成 `.l2s` 开头的特殊文件），proot 的 `cp -a` 可能不保留符号链接，而是把链接目标复制成真实文件。

证据：
- 三个副本的时间戳不同：原始文件 `14:17`，副本 `14:37`（差 20 分钟，对应 `cp -a` 步骤）。
- 权限从 `755` 变成 `700`（`cp -a` 在 proot 里丢了权限）。
- `oaa-build` 的 `tar` 命令本身没错——归档里三个条目都是普通文件 `-rwx`，不是 symlink `lrwx`，说明在 `tar` 之前链接已经丢了。
- make 包没有这个问题（没有符号链接可丢）。

**修法**：检查 `build-pkg.sh` 里的 `cp -a` 是否在 proot 里运行。如果是，改成先在源目录里 `tar cf - .` 再在目标目录里 `tar xf -`（tar 管道在 proot 里更可靠地保留符号链接），或者在非 proot 环境下打包。

验证工具：`scripts/check-oaa-duplicates.py`

### bug 1：soname 提取位置错

RPM 的 soname 依赖带符号版本，有三种形态：

```
libstdc++.so.6()(64bit)                无符号版本约束
libstdc++.so.6(CXXABI_1.3)(64bit)      带符号版本约束
libc.so.6(GLIBC_2.38)(64bit)           带符号版本约束
```

用 `split('()')[0]` 会把 `libstdc++.so.6(CXXABI_1.3)(64bit)` 切成 `libstdc++.so.6(CXXABI_1.3)`。

**正确做法：soname 是第一个 `(` 之前的部分。**

### bug 2：正则漏掉带符号版本的名字

第一版的正则要求以 `.so` 结尾，于是**带符号版本的依赖一个都匹配不上**。

后果：两个脚本给出矛盾的结论——一个说 `libstdc++` 被 5304 个包需要，另一个说是 **0** 个。

**矛盾本身就是信号。** 两个数字对不上时，不要挑一个信，要去查为什么。

### bug 3：符号链接形式的库没被识别

```python
if os.path.islink(Path):
    continue          # ← 这里
```

真实文件叫 `libgcc_s-15-20250329.so.1`，程序需要的是 `libgcc_s.so.1`，而后者是符号链接被跳过。

后果：**装了库之后数字不变**（还是 8348）。这就是发现 bug 的线索。

**正确做法：一个库要同时用三个名字注册**——文件 basename、内嵌 SONAME、符号链接名。

修完之后数字从 75.4% 变成 77.3%。

---

## 8. 复现方式

```bash
# 1. 从磁盘镜像抽出 Okra rootfs（只读，不动原盘）
debugfs -R 'rdump / /root/okraroot' okralinux-7.2-disk.img

# 2. 取 Fedora 仓库元数据
curl -o repomd.xml "$MIRROR/repodata/repomd.xml"
# 从 repomd.xml 里找 primary.xml.zst 的路径
curl -o primary.xml.zst "$MIRROR/repodata/<hash>-primary.xml.zst"
zstd -d primary.xml.zst -o primary.xml

# 3. 分析
python3 scripts/analyze-correct.py primary.xml /root/okraroot
python3 scripts/analyze-incremental2.py primary.xml /root/okraroot
```

`analyze-correct.py` 是**唯一可信**的版本，前两个（`analyze-repo.py`、`analyze-incremental.py`）留着当反面教材。

---

## 9. 这对设计文档的影响

| 文档 | 要改什么 |
|---|---|
| `rpm.md` 第 3 节 | **"Fedora ELF 跑不起来"是错的。** 改成"符号版本没问题，障碍是缺库" |
| `rpm.md` 第 3 节的表 | "noarch 高 / 动态程序中 / Fedora 私有库低" 这个分级要按实测数据重写 |
| `README.md` | 三个硬冲突里的第 1 条要降级——它不是致命的前置，是可解决的缺库问题 |
| `verification.md` | 补上这次的实测记录 |
| `oaabi.md` | 待提改动：`x86_64-okra-linux-gnu` → `aarch64-okra-linux-gnu` |