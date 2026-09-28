# OkraLinux 包管理后端设计

这套东西回答一个问题：**Lunar 能不能变成"包管理器界的通用事务层"，让 `.oaa`、`.rpm`、`.deb`、`.alpm` 走同一套事务、快照、回滚？**

结论：能，但有三处硬冲突要先解决，其中一处（Fedora 二进制在 Okra 上跑不起来）是决定整条路线价值的。

---

## 文件

| 文件 | 内容 |
|---|---|
| `backend.md` | 后端合同。五个可替换点：`VersionScheme` / `RepositoryBackend` / `Resolver` / `ArtifactBackend` / `ScriptBackend` |
| `rpm.md` | RPM 适配器的边界、前置条件、落地阶段、评审清单 |
| `verification.md` | **实测记录。哪些结论是跑出来的，哪些是推断的** |
| `scripts/rpm-feasibility.sh` | 判定一批 RPM 在 Okra 上能不能用 |
| `scripts/vercmp-cases.lua` | 用系统 rpm 生成 `rpmvercmp` 的权威期望值 |
| `scripts/vercmp-expected.txt` | 32 条期望值，直接当测试向量 |
| `code/lib/okrapmlib/include/okrapmlib/version_scheme.h` | `VersionScheme` 合同 + `RpmEvrScheme` / `SemverScheme` + `RpmSplitEvr` |
| `code/lib/okrapmlib/include/okrapmlib/dependency.h` | `Dependency` / `DependencyKind` / `DependencySource` + 满足性判定 |
| `code/lib/okrapmlib/include/okrapmlib/rpm_repository.h` | `RpmRepositoryBackend`：读 repomd.xml + primary.xml |
| `code/lib/okrapmlib/include/okrapmlib/dependency_serialization.h` | 依赖块的可嵌入文本格式 |
| `code/lib/okrapmlib/src/rpm_repository.cpp` | 解析 XML、Find/Search/WhatProvides |
| `code/lib/okrapmlib/tests/rpm_repository_test.cpp` | 23 条测试，用真实 67343 包 |
| `code/lib/okrapmlib/src/version_scheme.cpp` | `RpmVerCmp` / `RpmEvrCmp` / 两个策略 / 注册表 |
| `code/lib/okrapmlib/src/dependency.cpp` | 解析、格式化、内部依赖过滤、满足性判定 |
| `code/lib/okrapmlib/src/dependency_serialization.cpp` | 依赖块的序列化与解析 |
| `code/lib/okrapmlib/tests/version_scheme_test.cpp` | 103 条单元测试 |
| `code/lib/okrapmlib/tests/dependency_test.cpp` | 98 条单元测试 |
| `code/lib/okrapmlib/tests/dependency_serialization_test.cpp` | 39 条单元测试 + 2 万轮随机往返 |
| `code/lib/okrapmlib/tests/fuzz-cases.lua` | 随机版本对生成器，期望值来自系统 rpm |
| `code/lib/okrapmlib/tests/fuzz_main.cpp` | 版本比较差分测试的 C++ 侧 |
| `code/lib/okrapmlib/tests/probe-release-rule.sh` | 探针：摸清 release 规则 |
| `code/lib/okrapmlib/tests/probe-epoch-rule.sh` | 探针：摸清 epoch 规则 |
| `code/lib/okrapmlib/tests/probe-full-matrix.sh` | 生成 126 组依赖匹配矩阵 |
| `code/lib/okrapmlib/tests/dep_matrix_main.cpp` | 70 组依赖矩阵的 C++ 侧 |
| `code/lib/okrapmlib/tests/dep_matrix_full_main.cpp` | 126 组依赖矩阵的 C++ 侧 |
| `object-integration.md` | **接进 `object.h` 的步骤 + 一个未验证的阻塞问题** |

## 先跑这个

```bash
# 1. 拿到 rpmvercmp 的权威期望值（不需要任何 Okra 代码）
rpm --eval "%{lua: dofile('scripts/vercmp-cases.lua')}"
cat /tmp/vercmp-expected.txt

# 2. 判定一批 Fedora RPM 在 Okra 上的可用性
./scripts/rpm-feasibility.sh -s /path/to/OKRALINUX samples/*.rpm
cat rpm-feasibility-out/summary.txt

# 3. 单测（240 条）
cd code/lib/okrapmlib
g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/vs_test tests/version_scheme_test.cpp \
    src/version_scheme.cpp -Iinclude && /tmp/vs_test
g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/dep_test tests/dependency_test.cpp \
    src/dependency.cpp src/version_scheme.cpp -Iinclude && /tmp/dep_test
g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/ds_test tests/dependency_serialization_test.cpp \
    src/dependency_serialization.cpp src/dependency.cpp src/version_scheme.cpp \
    -Iinclude && /tmp/ds_test

# 4. 版本比较差分：400000 对，rpm 当裁判
g++ -std=c++17 -O2 -o /tmp/fuzz_main tests/fuzz_main.cpp src/version_scheme.cpp -Iinclude
for S in 1 2 3 5 7 11 42 99; do
  FUZZ_SEED=$S FUZZ_PAIRS=25000 rpm --eval "%{lua: dofile('tests/fuzz-cases.lua')}"
  /tmp/fuzz_main > /tmp/fuzz-actual.txt
  diff /tmp/fuzz-expected.txt /tmp/fuzz-actual.txt || echo "种子 $S 有差异"
done

# 5. 依赖匹配差分：126 组，rpm 当裁判
bash tests/probe-full-matrix.sh
g++ -std=c++17 -O2 -o /tmp/fm tests/dep_matrix_full_main.cpp \
    src/dependency.cpp src/version_scheme.cpp -Iinclude
/tmp/fm > /tmp/mine.txt
diff <(cut -f4 /tmp/dep-full-matrix.tsv) /tmp/mine.txt
```

第 2 步的 `summary.txt` 里的"高"占比，决定后面要不要投入：

- **≥ 50%** → 值得做适配器
- **< 20%** → 改走 SRPM 重构建路线更划算

---

## 三个硬冲突

按严重程度排。

### 1. 缺库（可解决，不是致命）

**这一条在 2026-09-27 被实测重写过。** 原文说的是"Fedora 的 ELF 因 glibc 符号版本不匹配跑不起来"——**那是错的**。

| | GLIBC 符号版本上限 |
|---|---|
| Okra 自建 glibc | **GLIBC_2.43** |
| Fedora 42 需要 | GLIBC_2.38 / 2.41 |

67,343 个 Fedora 包里只有 **1 个**因符号版本失败。**符号版本不是障碍。**

真正的障碍是缺库。全量统计结果：

| | 包数 | 占比 |
|---|---|---|
| **可用** | **52,049** | **77.3%** |
| 缺库 | 15,293 | 22.7% |

硬证据（`chroot` 隔离，无宿主库回退）：

```
ninja（Fedora 42 的 C++ 程序）装库前：error while loading shared libraries: libstdc++.so.6
                              装库后：1.12.1
```

投入产出比最高的三步，**已经实测补完**，aarch64 可用数从 8,348 涨到 9,609：

| 步骤 | 来源 | 单独贡献 |
|---|---|---|
| `libgcc_s.so.1` | **Okra 的 `gcc-16.2.0-1.aarch64.oaa`** | 900 个包 |
| `libstdc++.so.6` | **同上** | 733 个包 |
| `libresolv.so.2` | 符号链接到 Okra 的 `libc.so.6` | 336 个包 |
| **三个一起** | | **+1,261 个包** |

**关键**：Okra 的 gcc 包里**本来就有 `libgcc_s` 和 `libstdc++`**。所以这不是"从 Fedora 搬库"，而是"Okra 自己的 gcc 包没装全"。全部来自 Okra，没有混装，符合 `oaabi.md` 第 2 节。

`ninja`（Fedora 42 的 C++ 程序）的对照实验：

```
补之前：error while loading shared libraries: libstdc++.so.6
补之后：1.12.1
```

补完之后缺失榜变成 glib、Qt、X11、cairo、openssl——**完整桌面栈**，那才是真天花板。

完整数据见 `feasibility.md`。

### 2. `Version` 只认语义化版本

`version.h` 是 `int major_; int minor_; int patch_;`，而 Fedora 是 EVR（`1:3.30.5-1.fc41`）。

**实测证据**：

```
3.30.5-1.fc41      3.30.5             >     # RPM：release 让版本更大
```

而 `Version::parse("3.30.5-1.fc41")` 把 `-1.fc41` 读成 `pre_release`，判成**更小**。方向正好相反。

### 3. 依赖是裸字符串

`dependencies_` 只能表达 `GNU.make`。RPM 需要 `glibc >= 2.39`、`/usr/bin/sh`、`libstdc++.so.6()(64bit)`、`(foo if bar)`，还有 `Provides` / `Conflicts` / `Obsoletes`。

**Fedora 的依赖求解几乎全靠 Provides 虚拟能力**，没有这套就解不出依赖。

---

## 落地顺序

| 序 | 改动 | 依赖 | 可验证的标志 |
|---|---|---|---|
| 0 | 跑 `rpm-feasibility.sh` | 无 | 有 `summary.txt` |
| 1 | `VersionScheme` + `semver` + `rpm-evr` | 无 | `vercmp-expected.txt` 32 条全过 |
| 2 | `Object` 加 `Capabilities()` + `Dependency` | 无 | 旧仓库仍能 `lunar list` |
| 3 | `ArtifactBackend` 抽接口，OAA 成为第一个实现 | 2 | 装 `.oaa` 行为不变 |
| 4 | 事务加 scriptlet 阶段 | 3 | 旧包多跑一个钩子，不回归 |
| 5 | `rpm-repo` 只读原型 | 1, 2 | `lunar search fedora.cmake` 有结果 |
| 6 | RPM 的 `ArtifactBackend` | 3, 4, 5 | 能装一个 noarch 包 |
| 7 | `ScriptBackend` + 阶段排序 | 6 | 能装一个带 `%post` 的包 |
| 8 | rpmdb 兼容层 | 7 | `dnf list installed` 能看到 Lunar 装的 |

**第 0 到 2 步完全不依赖 librpm，也不依赖任何架构决策。**

---

## 三条不要做的事

1. **不要重写 dnf。** 它整个代码围绕 libsolv 的 `Pool`/`Solver`/`Transaction`/`Solvable` 写。让它"底层调 Lunar"，要么提供同名同语义的 libsolv，要么重写 dnf，成本同一个数量级。
2. **不要自己写 SAT 求解器。** `RpmResolver` 内部建 libsolv 的 pool，把 `Object` 灌进去，结果翻回 `std::vector<Operation>`。这样工作量从年降到月。
3. **不要把 Fedora 的库写进任何 `DT_NEEDED`。**

---

## 遵循的约束

- `for_ai.md`：缩进只用 Tab（宽 4）、行宽 ≤120、新标识符全 PascalCase、禁止 snake_case/camelCase/全大写下划线常量、对外函数要有那种头注释。**旧文件里的旧风格不许重排重命名。**
- `oaabi.md`：新增的跨 `.so` 导出要 C 链接、PascalCase、`Oaabi` 前缀、聚合类型走指针、失败返回负 errno、插件导出 `struct OaabiPlugin`。
- `oaabi.md` 第 12 节：只加新符号时机器名仍是 `OAABI1`；改已有宽度/寄存器用法/旧符号含义要发布新机器名。

---

## 两个待确认

1. **架构不一致。** `oaabi.md` 写的是 `x86_64-okra-linux-gnu`，实际构建盘是 **aarch64**，`bootstrap-toolkit` 的包描述也是 aarch64。是有意为之还是文档滞后？
2. **`ExtensionApi` 走哪条路。** 路线甲（C 链接门面，导出 `OaabiPlugin`）还是路线乙（内置扩展，编进 lunar）。建议**先乙后甲**：乙能立刻验证模型改得对不对，不用先解决 ABI；反过来的话会一边改 ABI 一边改模型。