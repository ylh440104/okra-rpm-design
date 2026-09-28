# RPM 适配器

RPM 支持的边界、前置条件与落地路径。合同部分见 `backend.md`。

本文是 Okra 文档总仓库的一部分。

---

## 1. 目标与非目标

### 目标

- `lunar install <某个 .rpm>` 走 Lunar 的事务、快照、回滚。
- `lunar install fedora.cmake` 从 Fedora 源解析并安装。
- 版本、依赖、脚本三个阶段全部由 `backend.md` 的五个后端承担。

### 非目标

- **不重写 dnf。** dnf 的整个代码围绕 libsolv 的 `Pool` / `Solver` / `Transaction` / `Solvable` 写。要让它"底层调 Lunar"，要么提供一套同名同语义的 libsolv，要么重写 dnf。两者成本是同一个数量级。
- **不重写 librpm 的事务。** `rpmtsRun()` 内部是 rpm 自己的依赖检查和文件冲突逻辑。
- **不把 Fedora 的二进制包当作"能在 Okra 上跑"的包。** 原因见第 3 节。

---

## 2. 与既有实验的关系

`rpm-packages` 仓库已经试过另一条路：

> OkraLinux 系统组件由 OAA 包提供，没有 rpm 数据库。`okra-system-base` 用 `Provides` 声明 bash coreutils 等能力，这样 dnf 能识别依赖，正常安装 rpm 包。

这条路成本最低，今天就能通。代价是两套数据库共存、`lunar rollback` 回不了 dnf 的账。

**本文走的是另一条：Lunar 是唯一权威。** 两者不冲突——`Provides` 方案可以继续作为"想让 dnf 自己干活"时的过渡，本文的适配器是"想让 Lunar 当唯一真源"时的目标态。

判断依据是需求，不是技术：**要不要统一回滚。**

---

## 3. 前置条件：不是符号版本，是缺库

**这一节在 2026-09-27 被实测推翻并重写过。** 原文写的是"Fedora 的 ELF 在 Okra 上跑不起来，因为 glibc 符号版本不匹配"。那是错的。完整数据见 `feasibility.md`。

### 3.1 符号版本不是障碍

`oaabi.md` 第 2 节说"Fedora 的 `libmount.so.1` 这类外来库和这份 libc 不是一套"。这话对**库**成立，但对**符号版本**不成立：

| | GLIBC 符号版本上限 |
|---|---|
| Okra 自建 glibc | **GLIBC_2.43** |
| Fedora 42 需要 | GLIBC_2.38 / 2.41 |

Okra 的 glibc 比 Fedora 需要的还新。67,343 个 Fedora 包里只有 **1 个**因符号版本失败，而且是 `GLIBC_PRIVATE`（glibc 内部符号，本来就不该被外部依赖）。

### 3.2 硬证据

用 `chroot` 隔离（无宿主库回退）跑 Fedora 42 的 aarch64 二进制：

| 二进制 | 结果 |
|---|---|
| `3proxy` | ✅ 跑起来 |
| `64tass` | ✅ 输出 `64tass Turbo Assembler Macro V1.59.3120` |
| `ninja`（C++） | ✅ 输出 `1.12.1` |

`ninja` 装库前后的对照：

```
装 libstdc++ 前：error while loading shared libraries: libstdc++.so.6
装 libstdc++ 后：1.12.1
```

### 3.3 真实可用率

全量统计（Fedora 42 Everything aarch64 的 67,343 个包）：

| 类别 | 占比 | 说明 |
|---|---|---|
| **可用** | **77.3%** | noarch 100%，aarch64 38.6% |
| 缺库 | 22.7% | 缺 glib、Qt、X11、cairo 这类 |

补上三个东西（`libresolv` 符号链接、`libgcc_s`、`libstdc++`），aarch64 可用数从 8,348 涨到 9,609。

### 3.4 真正的天花板

补完上面三个之后，缺失榜变成：

```
libglib-2.0.so.0     2346
libgobject-2.0.so.0  2077
libz.so.1            1827
libQt6Core.so.6       963
libX11.so.6          1039
libcairo.so.2         951
```

**这是完整的桌面栈。** 77.3% 大致就是"直接装 Fedora 二进制"的天花板。要突破就得走 SRPM 重构建。

### 3.5 哪些仍然成立

`oaabi.md` 第 2 节那条约束**本身没错**，只是适用范围要说清：

| 说法 | 是否成立 |
|---|---|
| 不能把 Fedora 的库写进 `DT_NEEDED` | ✅ 成立。所以缺库必须自己提供 |
| 不能 `dlopen` Fedora 的库 | ✅ 成立。同上 |
| **Fedora 的 ELF 因符号版本跑不起来** | ❌ **不成立**。见 3.1 |

换句话说：**Okra 的 glibc 能承载 Fedora 的二进制，但不能承载 Fedora 的其他库。** 缺的库要么自己编，要么用 Fedora 的（那就得接受混装）。

---

## 4. 第一步：可行性统计

**要回答的问题：一批 Fedora 的 rpm 里，有多大比例在 Okra 上真的能用？**

这个数字决定投入是否值得。如果只有 15%，结论应该是"走 SRPM 重构建路线"；如果有 60%，就值得做适配器。

工具：`scripts/rpm-feasibility.sh`（随本文一起）。纯 bash + `rpm` + `objdump`，不编译任何东西。

它统计三件事：

1. `ARCH` 分布，`noarch` 占比。
2. 每个 ELF 的 `DT_NEEDED` 里，有没有 Okra sysroot 中不存在的库。
3. 每个 ELF 需要的 `GLIBC_` 符号版本上限，和 Okra 那份 glibc 的版本表对比。

用法：

```bash
# 拿一批样本，比如从 Fedora 的 Everything 源里取
./scripts/rpm-feasibility.sh -s /path/to/OKRALINUX samples/*.rpm
```

产物：

```
arch.txt       架构分布
needed.txt     每个 ELF 的 DT_NEEDED 与 sysroot 命中情况
symbols.txt    每个 ELF 的 GLIBC 符号版本上限
verdict.txt    每个 RPM 的结论
```

### 判读

- `verdict.txt` 里 `noarch` 全绿 → 至少能覆盖纯数据包。
- `symbols.txt` 里最大需求 ≤ Okra glibc 的最大导出 → 动态程序有机会。
- `needed.txt` 里出现大量 `libselinux` / `libcrypto` / `libmount` → 这些包在 Okra 上没戏，除非连依赖一起重构建。

---

## 5. 版本比较：rpmvercmp 的边界

`Version` 现在是 `int major_; int minor_; int patch_;`。Fedora 是 EVR，且 `Version::parse("3.30.5-1.fc41")` 会把 `-1.fc41` 当成 `pre_release`，于是**判成 pre-release、排在 `3.30.5` 之前**——和 RPM 语义正好相反。

`rpmvercmp` 的规则：

- 按段切分，段与段之间是数字段和字母段的交替。
- 数字段按数值比（去掉前导零）。
- 字母段按字典序比，**字母 > 数字**。
- `~` 排在任何东西之前（`1.0~rc1 < 1.0`）。
- `^` 排在任何东西之后（`1.0^git > 1.0`）。
- 缺段视为空串，空串排在非空之前。
- 非字母非数字的分隔符**直接跳过**，不参与比较。

### 三层结构

这一段是实测之后才写对的。`rpmvercmp` 本身只做段比较，而 `rpm` 命令行（`rpm.vercmp` / `rpmdev-vercmp`）在它外面还包了两层：

```
rpm.vercmp(A, B)          ← 命令行用的就是这个，等价于 RpmEvrCmp()
  ├── 拆 epoch            ← 第一个 ':' 之前的纯数字，缺省 0
  ├── 拆 release          ← 最后一个 '-' 之后的部分
  └── rpmvercmp()         ← 只做段比较，等价于 RpmVerCmp()
```

**这三层不能混。** 实测差异：

| 用例 | `rpm.vercmp` | 纯段比较 |
|---|---|---|
| `1-3` vs `1-2-1` | `<` | `>` |
| `2-1-1` vs `2-2` | `>` | `<` |
| `1-` vs `1` | `>` | `=` |
| `2:A` vs `2:A-.` | `<` | `=` |

所以实现里要有两个函数，语义不同、名字不同，各配各的文档注释。**不要"顺手统一"它们。**

### release 的存在性也参与比较

这一条最容易漏，是差分测试抓出来的：

| 用例 | 结果 | 说明 |
|---|---|---|
| `1-` vs `1` | `>` | release 为空，但**有** `-`，仍比没有的大 |
| `a` vs `a-` | `<` | 同上反向 |
| `1-0` vs `1` | `>` | release 是 `0` |
| `a-` vs `a-.` | `=` | 两边都有 `-`，内容 `""` 与 `"."` 都归约成空 |

只比 release 的**内容**会把这四组里前三组判错。必须先把"有没有 `-`"比掉，再比内容。

### 必过用例

| 左 | 右 | 期望 | 说明 |
|---|---|---|---|
| `1.0` | `1.0` | 0 | 相等 |
| `1.0` | `1.1` | < | 数字段 |
| `1.0` | `1.0.1` | < | 缺段当空串 |
| `1.0~rc1` | `1.0` | < | `~` 排前 |
| `1.0~rc1` | `1.0~rc2` | < | `~` 后再比 |
| `1.0^git1` | `1.0` | > | `^` 排后 |
| `1.0^git1` | `1.1` | < | `^` 只压同段 |
| `1.0a` | `1.0` | > | 字母段非空 > 空 |
| `1.0` | `1.0a` | < | 同上反向 |
| `1.0` | `1.0.0` | < | 注意：与 semver 相反 |
| `2.0` | `10.0` | < | 数值比，不是字典序 |
| `1.0` | `1.0-1` | < | release 段非空 |
| `1.0-1` | `1.0-2` | < | release 数字 |
| `1:1.0` | `2.0` | > | epoch 优先 |
| `1:1.0` | `1:1.0` | 0 | epoch 相等 |
| `1.0-1.fc41` | `1.0-1.fc42` | < | dist tag |
| `0` | `0` | 0 | 全零 |
| `1.0+` | `1.0` | **0** | `+` 被跳过，不参与比较 |
| `1.0.` | `1.0` | **0** | 尾随 `.` 被跳过 |
| `3.30.5-1.fc41` | `3.30.5` | **>** | release 段让版本更大 |
| `1.10` | `1.1` | > | 数值比 |
| `1.0A` | `1.0a` | < | 大写字母小于小写 |
| `1-` | `1` | > | release 存在性 |
| `a-` | `a-.` | = | release 存在性相同，内容都归约成空 |

加粗那三行是本文初稿写错的地方。初稿把 `1.0+` 和 `1.0.` 的期望值写成了 `>`。

### 权威期望值从哪来

上表的期望值**不是手写的**，是用系统 `rpm` 生成的：

```bash
rpm --eval "%{lua: dofile('scripts/vercmp-cases.lua')}"
```

完整可复现的脚本和输出见 `scripts/vercmp-cases.lua` 与 `scripts/vercmp-expected.txt`。实现 `rpm-evr` 时把 `vercmp-expected.txt` 直接当测试向量，不要手写期望值。

### 差分测试

手写用例覆盖不了长尾。用 `tests/fuzz-cases.lua` 生成随机版本对，`rpm` 当裁判：

```bash
FUZZ_SEED=1 FUZZ_PAIRS=25000 rpm --eval "%{lua: dofile('tests/fuzz-cases.lua')}"
/tmp/fuzz_main > /tmp/fuzz-actual.txt
diff /tmp/fuzz-expected.txt /tmp/fuzz-actual.txt
```

**这个测试抓到过两个真 bug**，手写用例一个都没覆盖到：

1. 忘了拆 release 段（只拆了 epoch）。
2. release 的存在性没有参与比较。

**注意传参方式。** 种子和数量必须走环境变量 `FUZZ_SEED` / `FUZZ_PAIRS`。用 `rpm --eval "FuzzSeed=1; ..."` 这种写法**不会生效**——rpm 的 Lua 沙箱不传递外部全局变量，读到的永远是 `nil`，于是每轮都跑同一个默认种子。看起来"多轮全过"，其实是一轮重复了很多遍。这个坑本身也是被发现的：先验证了不同种子的 md5 不同，才敢相信结果。

### 交叉验证

别只信自己的实现。用系统里的 `rpm` 做对照：

```bash
rpmdev-vercmp 1.0~rc1 1.0
# 或
rpm --eval '%{lua: print(rpm.vercmp("1.0~rc1", "1.0"))}'
```

把上表的每一行都跑一遍 `rpmdev-vercmp`，结果写进测试期望值，而不是手写期望值。

---

## 6. 元数据映射

Fedora `primary.xml` / rpm header → `Object` + `ArtifactMetadata`。

| RPM 字段 | Lunar 落点 | 备注 |
|---|---|---|
| `%{NAME}` | `Object::name()` | 小写 |
| `%{EPOCH}` `%{VERSION}` `%{RELEASE}` | `Version::Raw` | 拼成 `E:V-R`，`E` 为 0 时省略 |
| 命名空间 | `fedora` | 由仓库配置决定，不写死在 rpm 里 |
| `%{ARCH}` | `ArtifactMetadata::architecture` | |
| `%{SUMMARY}` | `description` | 单行 |
| `%{DESCRIPTION}` | 详情 | 现有模型没有这个字段，可先丢 |
| `%{SIZE}` | `installed_size` | |
| `%{LICENSE}` `%{VENDOR}` | 无落点 | 现有模型没有，先丢 |
| `%{PROVIDES}` | `Capabilities()` | `Dependency{Kind=Provides}` |
| `%{REQUIRES}` | `Dependencies()` | `Dependency{Kind=Requires}` |
| `%{CONFLICTS}` | `Dependencies()` | `Dependency{Kind=Conflicts}` |
| `%{OBSOLETES}` | `Dependencies()` | `Dependency{Kind=Obsoletes}` |
| `%{RECOMMENDS}` | `Dependencies()` | `Dependency{Kind=Recommends}`，弱依赖 |
| `%{SOURCERPM}` | 无落点 | SRPM 名，调试用 |
| 文件列表 | `files` | 来自 `filelists.xml` 或 rpm header |

### 引用格式

`fedora.cmake@3.30.5-1.fc41`。

用 `fedora` 作命名空间而不是沿用 Fedora 自己的名字，理由：

- `find "fedora.*"` 和 `where repository=fedora` 都能用；
- 不和 `GNU.gcc` 这类已有命名空间撞；
- 和 `okra.system*` 那套系统对象规则（namespace 为 `okra` 且名字以 `system` 开头）不冲突。

### 组

Fedora 的组在 `comps.xml` 里。`object.h` 里已经有 `Group` 类，成员存在 `dependencies_`，天然对得上：

```
#fedora.workstation
```

`RepositoryBackend::HasCapability(IndexCapability::Groups)` 为真时才去请求 `comps.xml`。

---

## 7. 事务映射

| RPM 阶段 | Lunar 时机 | 注意 |
|---|---|---|
| `%pretrans` | 全事务开始前 | 此阶段文件系统可能还没就绪 |
| `%pre` | 该包写文件前 | |
| `%post` | 该包写文件后 | |
| `%triggerin` | 被依赖包写入后 | 依赖安装顺序，可多次 |
| `%triggerun` | 被依赖包删除后 | 可多次 |
| `%preun` | 该包删文件前 | 升级时参数是 1 |
| `%postun` | 该包删文件后 | 升级时参数是 1 |
| `%posttrans` | 全事务结束后 | |
| `%transfiletriggerin` | 文件路径命中 | 需要文件列表索引 |

执行序列：

```
所有 %pretrans
  → 逐包：%pre → 写文件 → %post → 命中的 %triggerin
  → 删除包：%preun → 删文件 → %postun → 命中的 %triggerun
所有 %posttrans
```

### scriptlet 参数

RPM 传给 scriptlet 的参数是**安装后该包的实例数**，不是 0/1：

| 动作 | 传参 |
|---|---|
| 全新安装 | `%pre` 传 1，`%post` 传 1 |
| 升级 | 新包 `%pre` 传 2，新包 `%post` 传 2，旧包 `%preun` 传 1，旧包 `%postun` 传 1 |
| 卸载 | `%preun` 传 0，`%postun` 传 0 |

这个语义必须在 `ScriptletDescriptor` 里表达清楚，否则包内的 `case "$1" in` 会走错分支。

### 失败回滚

`okrapm.md` 现在是"复制中途失败，则删掉本事务已经写入的文件，事务标成回滚，`system.db` 不更新"。

加上 scriptlet 之后，回滚要额外处理：`%post` 已经跑过的包，回滚时**应该跑 `%postun`**，否则外部副作用（注册服务、建用户）会残留。这一条 RPM 自己也不完美，但至少要明确"我们打算怎么做"，而不是默认不处理。

---

## 8. rpmdb 的归属

三个数据库：

| 数据库 | 路径 | 谁写 |
|---|---|---|
| Lunar | `/var/lib/lunar/system.db` | Lunar |
| RPM | `/var/lib/rpm/rpmdb.sqlite` | librpm |
| OPSIS | `/var/lib/okrapm/db` | OPSIS 脚本 |

`okrapm.md` 明说 OPSIS 的和 Lunar 的"是两套状态"。再加一个 RPM 的，就是三套。

三个选择：

| 方案 | 做法 | 代价 |
|---|---|---|
| **甲** | Lunar 是唯一真源，写一个 rpmdb 只读兼容层 | 要用 librpm 的写 API 反向生成 rpmdb，librpm 的 API 面很宽 |
| **乙** | Lunar 双写 rpmdb | 事务语义要桥接两套，一致性难保证 |
| **丙** | 不碰 rpmdb，让 dnf 自己管 | 回到 `rpm-packages` 那条路，Lunar 不知道 rpm 装了什么 |

**建议甲。** 但注意：写 rpmdb 需要 librpm 的 `rpmdbAdd()` 系列 API，而 `rpmdb.sqlite` 的 schema 是 RPM 4.16 之后才稳定的。先确认 Okra 上打算用的 librpm 版本，再决定。

如果短时间不做 rpmdb 兼容层，至少要保证：**Lunar 装的东西，dnf 不重复装**。做法是在 `Provides` 里声明一个能力名，比如 `okra-managed()`，然后让 dnf 的配置把它当已安装。

---

## 9. 落地阶段

| 阶段 | 内容 | 不做什么 | 成功标志 |
|---|---|---|---|
| 0 | 跑 `rpm-feasibility.sh` | 不写代码 | 有 `verdict.txt`，知道比例 |
| 1 | `VersionScheme` + `rpm-evr` | 不碰 librpm | `rpmvercmp` 用例全过 |
| 2 | `Object` 加 `Capabilities()` + `Dependency` | 不碰 librpm | 旧仓库不回归 |
| 3 | `rpm-repo` 只读原型，链 librpm 读 `primary.xml` | 不安装 | `lunar search fedora.cmake` 有结果 |
| 4 | `ArtifactBackend` 的 RPM 实现，只做 `ReadMetadata` + `ListPayload` | 不解包 | `lunar artifact inspect x.rpm` 有输出 |
| 5 | `ExtractPayload` + 事务 | 不跑 scriptlet | 能装一个 noarch 包 |
| 6 | `ScriptBackend` + 阶段排序 | — | 能装一个带 `%post` 的包 |
| 7 | rpmdb 兼容层 | — | `dnf list installed` 能看到 Lunar 装的东西 |

阶段 0 到 2 完全不依赖 librpm。阶段 3 才开始链它。

---

## 10. 评审清单

按 `oaabi.md` 第 14 节的格式，查本次新增的部分：

- [ ] 新增的 `VersionScheme` 实现有对应的 `rpmvercmp` 用例，期望值来自系统 `rpm` 而不是手写。
- [ ] 新增符号是 PascalCase，`backend.md` 里定义的接口全部是 C 类型或指针传聚合。
- [ ] `version.h`、`object.h` 里旧函数**没有**被重命名或重排。
- [ ] 新代码用 Tab 缩进，行宽 ≤120。
- [ ] 新增的对外函数有 `for_ai.md` 规定的文档注释。
- [ ] 导出面如果跨 `.so`，符合 `oaabi.md` 第 6、7、8 节；否则明确写成内置扩展。
- [ ] `backend.md` 里的五个接口有实现，或者明确标注"未实现"。
- [ ] 没有把 Fedora 的库写进任何 `DT_NEEDED`。
