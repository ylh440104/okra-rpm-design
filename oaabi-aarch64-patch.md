# oaabi-aarch64-patch.md

`oaabi.md` 的 aarch64 修正补丁。

实测依据：`okralinux-7.2-disk.img` 抽出的 rootfs 里，`libc.so.6` 是 `ELF 64-bit LSB shared object, ARM aarch64`，动态链接器是 `ld-linux-aarch64.so.1`，glibc 符号版本上限 `GLIBC_2.43`。`bootstrap-toolkit` 的包描述也写的是 "OAA package for OkraLinux aarch64"。

---

## 要改的地方

### 第 1 节

```
原文：架构是 x86_64。
改为：架构是 aarch64。
```

### 第 2 节（身份表）

| 字段 | 原文 | 改为 |
|---|---|---|
| OAA architecture | `x86_64` | `aarch64` |
| 目标三元组 | `x86_64-okra-linux-gnu` | `aarch64-okra-linux-gnu` |
| 机器 | `EM_X86_64（62）` | `EM_AARCH64（183）` |
| 动态链接器 | `/lib64/ld-linux-x86-64.so.2` | `/lib/ld-linux-aarch64.so.1` |
| 指令集下限 | `x86-64-baseline`（CMOV、SSE 等） | `armv8-a`（AArch64 基础指令集） |
| 指令集下限说明 | "CMOV、CMPXCHG8B、x87、FXSR、MMX、syscall、SSE、SSE2" | "AArch64 基础指令集（ARMv8-A）。不要求 NEON（虽然几乎所有 aarch64 硬件都有），不要求 SVE/SVE2" |

### 第 3 节（调用约定）

```
原文：调用约定采用 System V AMD64 psABI，数据模型是 LP64。
改为：调用约定采用 AArch64 AAPCS64（Procedure Call Standard for the Arm 64-bit Architecture），数据模型是 LP64。
```

参数寄存器表：

| | 原文 | 改为 |
|---|---|---|
| 整数参数 | RDI RSI RDX RCX R8 R9 | X0 X1 X2 X3 X4 X5 |
| 浮点参数 | XMM0 到 XMM7 | V0 到 V7（D0 到 D7） |
| 返回值 | RAX | X0 |
| 被调用方保存 | RBX RBP R12 R13 R14 R15 | X19-X28, SP, FP（X29） |
| 系统调用参数 | RDI RSI RDX R10 R8 R9 | X0 X1 X2 X3 X4 X5 |
| 系统调用号 | RAX | X8 |
| 系统调用返回 | RAX | X0 |
| 栈对齐 | RSP 16 的倍数 | SP 16 的倍数 |

### 第 4 节（数据模型）

```
原文：long double 是 16（x87 80 位，占 16 字节），对齐 16
改为：long double 是 16（IEEE 754 四精度，128 位），对齐 16
```

### 第 5 节（调用）

整段从 x86_64 寄存器改成 AArch64 寄存器。要点：
- 整数参数 X0-X7，浮点 V0-V7，返回值 X0
- 栈 16 字节对齐
- 红区不存在（AArch64 AAPCS64 没有红区概念）
- 聚合类型走指针（与 x86_64 版相同）

### 第 11 节（包）

```
原文：
  architecture: x86_64
  abi: OAABI1
  包内可执行文件的解释器是 /lib64/ld-linux-x86-64.so.2

改为：
  architecture: aarch64
  abi: OAABI1
  包内可执行文件的解释器是 /lib/ld-linux-aarch64.so.1
```

### 第 14 节（评审清单）

```
原文：含 ELF 的包写了 architecture: x86_64 和 abi: OAABI1。
改为：含 ELF 的包写了 architecture: aarch64 和 abi: OAABI1。
```

---

## 不改的地方

- `for_ai.md` 的编码规范与架构无关，不改
- OAABI 的版本号、机器名 `OAABI1`、版本标签 `OAABI_1` 不变
- `struct OaabiPlugin` 布局不变
- 第 6、7、8、9、10、12、13 节与架构无关，不改
- "C 库必须是 Okra 自己的 glibc" 这条不变
- "Fedora 的 libmount.so.1 这类外来库不能 dlopen" 这条不变（只是 `rpm.md` 里关于符号版本的推断被推翻了，见 `feasibility.md`）