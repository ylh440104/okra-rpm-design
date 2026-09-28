# 验证记录

本文记录 `scripts/` 下两个工具在真实环境里的验证结果。写在这里是为了让后来的人知道**哪些结论是实测的，哪些是推断的**。

---

## 1. 验证环境

| 项 | 值 |
|---|---|
| 系统 | Ubuntu 24.04.1 LTS |
| 架构 | **aarch64** |
| rpm | 4.18.2 |
| rpmbuild | 4.18.2 |
| 工作区 | 就是这块构建盘：有 `/root/okra-repo/`、`/root/rpmbuild/`、`/tmp/rpm-repo/` |

**注意架构。** 这台机器是 aarch64，而 `oaabi.md` 第 2 节写的是 `x86_64-okra-linux-gnu`、`EM_X86_64`、`/lib64/ld-linux-x86-64.so.2`。同时 `bootstrap-toolkit` 的包描述写的是 "OAA package for OkraLinux **aarch64**"。

也就是说：**文档里的 OAABI 是 x86_64 的，实际构建盘是 aarch64 的。** 这两者目前不一致，值得单独确认一次是有意为之还是文档滞后。

---

## 2. rpmvercmp 期望值

工具：`scripts/vercmp-cases.lua`

生成方式：

```bash
rpm --eval "%{lua: dofile('scripts/vercmp-cases.lua')}"
```

`rpm --eval` 会把输出的换行吃掉，所以脚本直接 `io.open` 写文件，不走标准输出。

结果：32 条，见 `scripts/vercmp-expected.txt`。

### 实测修正了本文档初稿的两处错误

| 用例 | 初稿 | 实测 |
|---|---|---|
| `1.0+` vs `1.0` | `>` | **`=`** |
| `1.0.` vs `1.0` | `>` | **`=`** |

原因：`rpmvercmp` 对非字母非数字字符是**直接跳过、不参与比较**，不是"比较剩下的段"，也不是按 ASCII 值比。

### 实测确认了核心结论

```
3.30.5-1.fc41      3.30.5             >
```

release 段让版本**更大**。而现有 `Version::parse("3.30.5-1.fc41")` 会把 `-1.fc41` 读成 `pre_release`，判成**更小**。方向正好相反。这是 `VersionScheme` 必须做的直接证据。

### 其他实测确认的边界

```
1.0                1.0.0              <     # 与 semver 相反
1.0~rc1            1.0                <     # 波浪号排前
1.0^git1           1.0                >     # 脱字符排后
1.0^git1           1.1                <     # 脱字符只压同段
1.0~rc1^git        1.0~rc1            >     # 两个混用
1:1.0              2.0                >     # epoch 优先
1.01               1.1                =     # 前导零
1.0A               1.0a               <     # 大写字母小于小写
```

### 无法测试的

空串。`rpm.vercmp("")` 报 `bad argument #1 to 'vercmp' (invalid version)`——Lua 绑定会先校验参数。空串的比较行为要对着 librpm 的 `rpmvercmp.c` 单独确认，不能靠猜。

---
## 3. rpm-feasibility.sh

### 3.1 语法与单测

```bash
bash -n scripts/rpm-feasibility.sh     # 通过
```

`MaxOfFamily` 和 `IsNewerThan` 两个函数单独测了 8 条：

```
  ok   GLIBC_2.38     vs GLIBC_2.39     -> notnewer
  ok   GLIBC_2.39     vs GLIBC_2.38     -> newer
  ok   GLIBC_2.34     vs GLIBC_2.39     -> notnewer
  ok   GLIBC_2.40     vs GLIBC_2.39     -> newer
  ok   GLIBC_2.9      vs GLIBC_2.39     -> notnewer
  ok   GLIBCXX_3.4.33 vs GLIBCXX_3.4.30 -> newer
  ok   CXXABI_1.3.15  vs CXXABI_1.3.13  -> newer
  ok   GLIBC_2.39     vs GLIBC_2.39     -> notnewer
```

其中 `GLIBC_2.9 vs GLIBC_2.39 -> notnewer` 是关键一条：朴素 `sort` 会把 `2.9` 排到 `2.39` 之后，判成"过新"。必须用 `sort -V`。

### 3.2 端到端：四个判定分支

样本用一个真实 RPM，里面塞了 `ls` 和 `rpm` 两个真 ELF，所以 `DT_NEEDED` 和符号版本两条路径都会走到。

```bash
rpmbuild --define '_topdir /root/rpmtest' -bb /root/rpmtest/SPECS/elftest.spec
# 产出 /root/rpmtest/RPMS/aarch64/elftest-1.0-1.aarch64.rpm
```

| 场景 | sysroot | 实测结论 | 分支 |
|---|---|---|---|
| A | 只有 3 个库 | `低`，缺失 `librpm.so.9` 等 6 个 | ✅ 库缺失 |
| B | 22 个库，齐全 | `高`，`GLIBC_2.38` 未超上限 | ✅ 全部命中 |
| C | 21 个库 + 假 libc（上限 2.30） | `中`，`符号版本 GLIBC_2.38 超过 Okra 上限` | ✅ 过新 |

场景 C 的假 libc 是现编的：

```bash
cat > /tmp/oldlibc/vscript.map <<'EOF'
GLIBC_2.17 { global: stub_symbol; local: *; };
GLIBC_2.30 { } GLIBC_2.17;
EOF
gcc -shared -fPIC -o /tmp/oldlibc/libc.so.6 /tmp/oldlibc/stub.c \
    -Wl,--version-script=/tmp/oldlibc/vscript.map
```

三个分支都按预期输出，说明判定逻辑没有死角。

### 3.3 noarch 分支

`/root/okra-repo/` 里三个包（`okra-installer`、两个 `okra-system-base`）和 `/tmp/test.rpm` 都是 noarch，实测全部判 `高`，理由 `noarch，无 libc 符号版本依赖`。

这一条恰好印证了 `rpm.md` 第 3 节的判断：**`rpm-packages` 仓库里现有的包全是 noarch，不是巧合，是因为只有 noarch 才天然可用。**

### 3.4 修复的 bug

首轮跑完报了：

```
./scripts/rpm-feasibility.sh: line 1: TempRoot: unbound variable
```

原因：`TempRoot` 用了 `local`，但 `EXIT` trap 在 `Main` 返回**之后**才执行，那时 `local` 已经失效。改成非 local，并把 trap 写成 `rm -rf "${TempRoot:-}"`。

这类 bug 只在 `set -u` 下暴露，而且只在**跑完之后**暴露，很容易漏。`for_ai.md` 第 7 节要求 Shell 保持该文件已有的方言，本脚本用的是 bash，`set -u` 是有意开的。

### 3.5 性能提醒

第一次跑时用 `-s /usr` 扫全量 soname，**超过 90 秒超时**。真实使用时 `-s` 应指向 `OKRALINUX/`（几万个文件量级），不要指向宿主的 `/usr`。

---

## 4. version_scheme 实现

代码在 `code/lib/okrapmlib/`。

### 4.1 单元测试

`tests/version_scheme_test.cpp`，103 条，全部通过。

分七节：rpmvercmp 向量、epoch、策略类与注册表、IsValid、反对称性与自反性、长数字段、两层语义差异。

其中反对称性那节做了 13 个值的**两两比较**（169 对），检查 `Compare(A,B)` 与 `Compare(B,A)` 是否恒反号，并检查自反性和传递性。这类性质测试能抓到单个用例抓不到的错。

长数字段那节专门防溢出：`1.18446744073709551617` 这种超过 64 位的段，实现里是**去掉前导零后按位数比**，不是 `strtoll` 转数字。

### 4.2 差分测试：抓到两个真 bug

`tests/fuzz-cases.lua` 生成随机版本对，`rpm` 算期望值，`tests/fuzz_main.cpp` 输出自己的答案，外层 `diff`。

**手写用例一个都没覆盖到这两个 bug：**

| # | bug | 现象 | 修法 |
|---|---|---|---|
| 1 | 只拆了 epoch，没拆 release | 23/4000 不一致，判定方向相反 | 在最后一个 `-` 处再拆一次 |
| 2 | release 的**存在性**没参与比较 | 19/400000 不一致 | 先比"有没有 `-`"，再比内容 |

第 2 个的细节：`2:A` vs `2:A-.` 两边 release 内容都归约成空，判成相等；但 rpm 认为右边有 `-`，所以右边大。

修完之后：**40 万对，0 差异**（16 个种子 × 25000 对）。

### 4.3 一个差点骗过自己的坑

第一轮多轮测试我用了 `rpm --eval "FuzzSeed=1; ..."` 传参，跑出"8 个种子全过"。

但验证时发现**种子 1 和种子 2 生成的 `fuzz-pairs.txt` md5 完全相同**。再测发现 rpm 的 Lua 沙箱里 `FuzzSeed` 读到的是 `nil`：

```
FuzzPairCount=nil
FuzzSeed=nil
```

也就是说那"8 个种子"其实是**同一批 4000 对跑了 8 遍**。改成走环境变量 `FUZZ_SEED` / `FUZZ_PAIRS` 之后，md5 才各不相同，也才立刻暴露出那 19 处差异。

**教训**：多轮随机测试如果不先验证"每轮输入确实不同"，跑出来的"全过"没有意义。

### 4.4 接口语义的一个坑

`RpmVerCmp` 和 `RpmEvrCmp` **不等价**，这是设计如此：

```
RpmEvrCmp(A, B)     等价于 rpm.vercmp()     ← 排序用这个
  ├── 拆 epoch
  ├── 拆 release
  └── RpmVerCmp()   只做段比较               ← 内部用
```

`RpmVerCmp("1-3", "1-2-1")` 返回 `>`，而 `rpm.vercmp("1-3", "1-2-1")` 返回 `<`。

我最初把头文件里 `RpmVerCmp` 的注释写成"规则同 librpm 的 rpmvercmp，不解析 epoch"——**这句话是错的**，会让人以为它可以替代 `rpm.vercmp`。已改成明确说明两层差异，并在测试里加了一节 `[7]` 把这个差异固化下来，防止以后有人"顺手统一"这两个函数。

---

## 5. dependency 实现

代码在 `code/lib/okrapmlib/include/okrapmlib/dependency.h` 和 `src/dependency.cpp`。

### 5.1 设计依据来自真实 rpm，不是记忆

先建了 17 个探针包（7 个 `Provides` × 10 个 `Requires`），用**独立 rpmdb** 跑真实依赖求解，让 rpm 当裁判。然后用这些数据设计模型。

`deptype` 的取值是实测出来的，rpm 4.18 上只有三个：

```
manual   spec 里手写的
auto     打包器自动生成的，例如从 ELF 的 soname 推出 libc.so.6()(64bit)
rpmlib   rpmlib(...) 形式的内部能力
```

`rpmlib(...)` 必须能过滤掉，否则解析器会去找根本不存在的"包"。

### 5.2 三个凭直觉写就会错的规则

这三个都是先写错、再被实测推翻的。

**规则一：无版本的能力满足一切约束。**

我原本按直觉写成"能力无版本不满足带版本的需求"。实测：

```
Provides: virt        满足 Requires virt / virt >= 1.0 / virt >= 2.0 / virt = 1.5
Provides: virt = 1.5  满足 virt / virt >= 1.0 / virt = 1.5，不满足 virt >= 2.0
```

按错的写，Fedora 里所有用无版本 `Provides` 的虚拟能力（`/usr/bin/sh`、`webserver` 这类）都会被判成不可满足。

**规则二：依赖匹配里 release 的通配是单向的。**

我原本写成对称通配，在 126 组矩阵里错了 4 格：

| 组合 | rpm | 对称通配（错） |
|---|---|---|
| `Provides 1.0` × `Requires > 1.0-1` | 满足 | 满足 |
| `Provides 1.0-1` × `Requires > 1.0` | **失败** | 满足 |

正确规则是：

```
需求侧无 release  → 不约束 release，只比 version
提供侧无 release  → 通配，满足任何比较符
两侧都有          → 比 release
```

**规则三：依赖匹配和版本排序对 release 的处理不同。**

`"1.0-"` 和 `"1.0"` 在**依赖匹配**里等价，在**版本排序**里不等价（`"1.0-" > "1.0"`）。

所以拆 EVR 的 `RpmSplitEvr()` 只负责拆、不归一化，由调用方决定。两个语义各有一份实现，不要合并。

### 5.3 差分测试

`tests/probe-full-matrix.sh` 建 9 个提供者 + 14 个需求者 = **126 组**，全部用真实 rpm 求解，输出 `/tmp/dep-full-matrix.tsv`。

`tests/dep_matrix_full_main.cpp` 复现同一矩阵，然后 `diff`。

结果：**126 组完全一致**。

`tests/dependency_test.cpp` 98 条单测，其中 `[8]` 一节专门锁住 release 通配的不对称性。

### 5.4 两套语义的对照表

| 输入 | 版本排序 `RpmEvrCmp` | 依赖匹配 `DependencySatisfiedBy` |
|---|---|---|
| `"1.0-"` vs `"1.0"` | `>`（有 `-` 更大） | `=`（空 release 视为无） |
| `"1.0"` vs `"1.0-1"` | `<` | 需求侧 `"1.0"` 时 `=`；提供侧 `"1.0"` 时通配 |
| `"1.0-1"` vs `"1.0-2"` | `<` | `<` |

---

## 6. 没有验证的

- **`ArtifactBackend` / `RepositoryBackend` / `ScriptBackend` 的任何实现。** 本文只验证了"判定脚本"、"版本比较"、"依赖匹配"这三件不依赖 OkraPM 其他代码的事。
- **librpm 的 C API。** 脚本用的是 `rpm` 命令行，不是 `librpm` 的 `rpmts` / `rpmReadPackageFile`。阶段 5 才会碰。**而且这台机器上 `librpm` 只有运行时（`librpm9t64`），没有 `librpm-dev`，`/usr/include/rpm/` 不存在，现在编不了 librpm 代码。**
- **`/var/lib/rpm` 的写入。** `rpm.md` 第 8 节的三个方案一个都没试。
- **`okrapm` 仓库的实际源码。** `artifact_engine.cpp`、`extension_api.cpp`、`transaction.cpp`、`resolver.cpp` 四个实现文件没有读到内容，所有关于实现的判断都是从 `include/okrapmlib/` 下的头文件反推的。
- **semver 的差分测试。** `SemverScheme` 只有手写用例（按 semver.org 2.0.0 的规则），没有和任何参考实现做过差分。
- **`VersionSchemeRegistry` 与旧 `Version` 的对接。** 新代码是独立的，还没接进 `Object` / `ObjectRef` / `ArtifactMetadata` / `system.db` 序列化。第 3 步之后才算真正落地。
- **`Object` 与 `Dependency` 的对接。** `Capabilities()` / `Dependencies()` 还没加到 `object.h`。`dependency.h` 现在是独立的。
- **rich deps。** RPM 的 `(foo if bar)` / `(foo or bar)` 语法没有建模。`Dependency` 只能表达单条带版本的约束。
- **`Conflicts` / `Obsoletes` 的求解语义。** 数据结构能装，但没有实现"两个包冲突"和"旧包被替代"的判定。
- **弱依赖的版本约束。** 实测发现 `rpm -qp --qf` 对 `RECOMMENDS` / `SUGGESTS` 只给出名字，没有配套的 flags 标签；但 `rpm -qp --recommends` 能显示 `nice >= 1.0`，说明版本是保留的，只是要用 librpm 的 `rpmds` API 取。这一点要等有 `librpm-dev` 之后才能验证。