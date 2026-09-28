# 后端模块

OkraPM 的后端合同。Lunar 核心只认本文的接口。OAA、RPM、DEB、ALPM 各自实现自己的部分。

本文是 Okra 文档总仓库的一部分。命名和缩进仍按 `for_ai.md`，二进制边界仍按 `oaabi.md`。

---

## 1. 现状

`okrapm/lib/okrapmlib/include/okrapmlib/extension_api.h` 里的 `ExtensionType` 已经列出八个取值：

```
ObjectType, Operation, RepositoryBackend, ArtifactBackend, Resolver, Hook, Formatter, Plugin
```

但 `ExtensionApi` 的公开注册方法只有三个：

```cpp
void register_extension(const ExtensionInfo& info);
void register_hook(HookType type, HookCallback callback);
void register_operation(const std::string& name, const std::string& description,
                        OperationHandler handler);
```

`RepositoryBackend`、`ArtifactBackend`、`Resolver` 三个枚举有名字，没有对应的注册入口。也就是说：**可替换后端这件事，枚举写了，接口没接。**

`repository.h` 的注释已经把意图写出来了：

> Repository 不应与 Package Model 强绑定。Lunar Core 只需要定义统一 Repository API：Repository → Index → Objects → Artifacts。

本文把这个意图落成可实现的合同。

---

## 2. 五个可替换点

一个包体系拆成五个独立后端，而不是一个大模块。理由：APT 和 RPM 在"索引解析"和"payload 解包"上完全不同，但"版本比较"和"事务排序"可以共用。拆成五个，复用才成立。

| 后端 | 决定什么 | 换掉它意味着 |
|---|---|---|
| `VersionScheme` | 版本字符串怎么比大小 | 能表达 `1:3.30.5-1.fc41` 这种 EVR |
| `RepositoryBackend` | 索引怎么读、怎么同步 | 能吃 `repomd.xml` / `Packages.gz` / `.db` |
| `Resolver` | 依赖怎么解、事务怎么排 | 能用 SAT，而不是只做拓扑排序 |
| `ArtifactBackend` | payload 怎么解包、怎么落盘 | 能处理 cpio+rpm header / ar+tar.xz |
| `ScriptBackend` | 包内脚本在事务的哪个点跑 | 能跑 `%pretrans` / `%posttrans` / `%triggerin` |

Lunar 核心保留：`Object`、`ObjectRef`、`Transaction`、`Snapshot`、`SystemStore`、`PipelineEngine`。这六样不因后端而变。

```
lunar (CLI)
   │
   ▼
┌──────────────────────────────────────────────┐
│ Lunar Core                                   │
│   Object / ObjectRef / Version               │
│   Transaction / Snapshot / SystemStore       │
│   PipelineEngine                             │
└───┬──────────┬──────────┬──────────┬─────────┘
    │          │          │          │
    ▼          ▼          ▼          ▼
Version    Repository  Resolver   Artifact    Script
Scheme     Backend                Backend     Backend
```

---

## 3. VersionScheme

这是最先要动的，因为 `Version` 现在是 `int major_; int minor_; int patch_;`，而 `Object`、`ObjectRef`、`ArtifactMetadata`、`system.db` 序列化全都经过它。

### 3.1 合同

```cpp
/**
 * VersionScheme - 版本比较策略。
 */
class VersionScheme
{
public:
	virtual ~VersionScheme() = default;

	/**
	 * Name() - 返回策略名。
	 *
	 * Return: 稳定的短名，如 "semver"、"rpm-evr"。
	 */
	virtual std::string Name() const = 0;

	/**
	 * Compare() - 比较两个版本字符串。
	 * @Left: 左操作数，原样字符串，不预先解析。
	 * @Right: 右操作数，原样字符串，不预先解析。
	 *
	 * Return: Left 小于 Right 返回负数，相等返回 0，大于返回正数。
	 */
	virtual int Compare(const std::string& Left, const std::string& Right) const = 0;

	/**
	 * IsValid() - 判断字符串是否属于本策略。
	 * @Raw: 待判定的版本字符串。
	 *
	 * Return: 属于返回 true。
	 */
	virtual bool IsValid(const std::string& Raw) const = 0;
};
```

### 3.2 版本对象怎么改

`Version` 从"三个整数"退化成"策略名 + 原始串 + 惰性解析结果"：

```
struct Version {
	std::string Scheme;   // "semver" / "rpm-evr" / "deb" / "alpm"
	std::string Raw;      // 原样保存，不规范化
};
```

比较走 `SchemeRegistry::Compare(Left, Right)`。相等判定用 `Raw` 逐字节比，因为 `1.0` 和 `1.0.0` 在 semver 下相等、在 rpm 下不等，`Raw` 才是身份。

### 3.3 对旧代码的约束

`for_ai.md` 第 1 节：

> 旧文件：只对新增的函数、类型、宏、全局变量遵守本文。
> 禁止为对齐风格去重排、重命名旧代码。

所以 `version.h` 里新增的 `VersionScheme`、`SchemeRegistry` 用 PascalCase，而同一文件里已有的 `major_`、`minor_`、`pre_release_`、`is_pre_release()` **保持原样，不许重命名**。评审时看到同一文件里两种命名并存，这是对的，不是遗漏。

---

## 4. RepositoryBackend

`repository.h` 里 `Repository` 已经是一个纯虚接口，形状基本可用：

```cpp
virtual std::optional<Object> find(const std::string& ns, const std::string& name) const = 0;
virtual std::vector<Object> list_objects() const = 0;
virtual Collection<Object> search(const std::string& query) const = 0;
virtual bool sync() = 0;
virtual std::optional<std::string> fetch_artifact(const Object& obj, const std::string& dest_dir = "");
```

要补三件事：

### 4.1 签名校验

Fedora 源的 GPG 校验不是可选项。接口里现在没有任何密钥概念。新增：

```cpp
/**
 * VerifyIndex() - 校验索引签名。
 * @KeyringPath: 该仓库的密钥环路径。
 *
 * Return: 成功返回 0，失败返回负 errno。
 */
virtual int VerifyIndex(const std::string& KeyringPath) = 0;

/**
 * VerifyArtifact() - 校验下载到的包。
 * @LocalPath: 本地文件路径。
 * @KeyringPath: 该仓库的密钥环路径。
 *
 * Return: 成功返回 0，失败返回负 errno。
 */
virtual int VerifyArtifact(const std::string& LocalPath, const std::string& KeyringPath) = 0;
```

### 4.2 索引能力探测

不同源的索引能力不同。RPM 源有 `comps.xml`（组）、有 `filelists`、有 `deltainfo`；OAA 源只有 `index.yaml`。让后端自报能力，前端才好决定要不要请求某一类数据：

```cpp
enum class IndexCapability {
	Groups,        // 有组定义，如 comps.xml
	FileLists,     // 能按文件路径查
	Delta,         // 有增量包
	Capabilities,  // 有 Provides / Conflicts / Obsoletes
	WeakDeps,      // 有 Recommends / Suggests
};
virtual bool HasCapability(IndexCapability Which) const = 0;
```

### 4.3 仓库类型不再写死

`repos.conf` 现在是 `name|local|url|enabled`，`repo add` 的 type 只认 local 和 remote。后端化之后，type 字段应该是**后端注册名**，不再是一个固定枚举。`lunar repo add fedora <url> rpm` 里的 `rpm` 就是 `RpmRepositoryBackend::Name()`。

旧的 `local` / `remote` 保留为内建后端名，老配置文件不用改。

---

## 5. Resolver

`resolver.h` 的注释已经说了：

> Resolver 与 CLI 分离，可替换不同解析策略。

现在只有 `topological_sort()`。RPM 需要 SAT。合同：

```cpp
/**
 * Resolve() - 把请求解成事务计划。
 * @Request: 请求类型与引用列表。
 * @Available: 候选对象集合。
 * @Installed: 已安装对象集合。
 * @OutPlan: 输出事务计划，按执行顺序排列。
 *
 * Return: 成功返回 0，失败返回负 errno。
 */
virtual int Resolve(const ResolveRequest& Request,
                    const Collection<Object>& Available,
                    const Collection<Object>& Installed,
                    std::vector<Operation>& OutPlan) = 0;
```

关键点：**Resolver 不必自己写 SAT 求解器。** `RpmResolver` 可以内部建 libsolv 的 pool，把 Lunar 的 `Object` 灌进去，求解后把结果翻回 `std::vector<Operation>`。这样"支持 Fedora 依赖"从"重写依赖求解"降级成"写一个 libsolv 适配器"。

`Operation` 的语义要扩：现在只有安装/删除，RPM 需要 `Upgrade`、`Downgrade`、`Obsolete`、`Reinstall` 四类，而且每个 `Operation` 要能挂多个 scriptlet 阶段。

---

## 6. ArtifactBackend

这是 RPM 能不能进来的关键，也是现在最窄的一层。

`okrapm.md` 记载的安装路径是：

> 解包到临时目录。里面有 `files/` 就用它做 payload，否则用 `rootfs/`。两边都没有则这次安装失败。

这个模型假设 payload 是"一棵目录树 + 一个 meta.yaml"。RPM 不是：它是 **header + cpio payload + scriptlet + rpmdb 事务**。所以 `ArtifactBackend` 的合同要描述得更抽象：

```cpp
/**
 * class ArtifactBackend - 一种包格式的解包与元数据读取。
 */
class ArtifactBackend
{
public:
	virtual ~ArtifactBackend() = default;
	virtual std::string Name() const = 0;
	virtual bool CanHandle(const std::string& LocalPath) const = 0;

	/**
	 * ReadMetadata() - 只读元数据，不解 payload。
	 * @LocalPath: 本地文件路径。
	 * @OutMetadata: 输出元数据。
	 *
	 * Return: 成功返回 0，失败返回负 errno。
	 */
	virtual int ReadMetadata(const std::string& LocalPath, ArtifactMetadata& OutMetadata) = 0;

	/**
	 * ListPayload() - 列出 payload 里的文件。
	 * @LocalPath: 本地文件路径。
	 * @OutEntries: 输出条目，含路径、权限、属主、大小、校验和。
	 *
	 * Return: 成功返回 0，失败返回负 errno。
	 */
	virtual int ListPayload(const std::string& LocalPath, std::vector<PayloadEntry>& OutEntries) = 0;

	/**
	 * ExtractPayload() - 解包到目标根。
	 * @LocalPath: 本地文件路径。
	 * @InstallRoot: 安装根。
	 * @OutWritten: 输出实际写入的路径列表，用于失败时回滚。
	 *
	 * Return: 成功返回 0，失败返回负 errno。
	 */
	virtual int ExtractPayload(const std::string& LocalPath, const std::string& InstallRoot,
	                           std::vector<std::string>& OutWritten) = 0;

	/**
	 * ListScriptlets() - 列出包内脚本及其挂载阶段。
	 * @LocalPath: 本地文件路径。
	 * @OutScriptlets: 输出脚本描述，含阶段、解释器、来源。
	 *
	 * Return: 成功返回 0，失败返回负 errno。
	 */
	virtual int ListScriptlets(const std::string& LocalPath,
	                           std::vector<ScriptletDescriptor>& OutScriptlets) = 0;
};
```

注意 `ExtractPayload` 要返回**实际写入的路径**。现有 `okrapm.md` 说的是"复制中途失败，则删掉本事务已经写入的文件"——这个信息现在是事务层自己边复制边记的，后端化之后必须由后端上报。

---

## 7. ScriptBackend

`okrapm.md` 现在写着：

> 当前提交路径不调用包内的 `scripts/pre-install`。包内脚本由 OAA 打包阶段或 OPSIS 安装脚本自己执行。

对 OAA 这没问题。对 RPM 不行：scriptlet 是包的语义，不是装饰。

RPM 的阶段与顺序要求：

| 阶段 | 时机 | 数量 |
|---|---|---|
| `%pretrans` | 全事务开始前 | 每个包一次 |
| `%pre` | 该包写入前 | 每个包一次 |
| `%post` | 该包写入后 | 每个包一次 |
| `%triggerin` | 被依赖包写入后 | 可多次 |
| `%triggerun` | 被依赖包删除后 | 可多次 |
| `%preun` | 该包删除前 | 每个包一次 |
| `%postun` | 该包删除后 | 每个包一次 |
| `%posttrans` | 全事务结束后 | 每个包一次 |
| `%transfiletriggerin` | 文件路径命中时 | 可多次 |

所以事务的执行序列要变成：

```
所有包的 %pretrans
  → 逐包：%pre → 写文件 → %post → 相关 %triggerin
  → 删除包：%preun → 删文件 → %postun → 相关 %triggerun
所有包的 %posttrans
```

`for_ai.md` 第 3 节要求 C 风格用 `goto` 收敛失败路径，但这里不适用——事务失败要走"逆序回滚 + 快照"，是既有机制，不是函数内的资源释放。

合同：

```cpp
enum class ScriptletPhase {
	PreTrans, PostTrans,
	PreInstall, PostInstall,
	PreRemove, PostRemove,
	TriggerIn, TriggerUn,
	FileTriggerIn, FileTriggerUn,
};

/**
 * RunScriptlet() - 在指定阶段运行一个包内脚本。
 * @Descriptor: 脚本描述。
 * @Phase: 当前阶段。
 * @InstallRoot: 安装根，脚本应以它为根。
 *
 * Return: 成功返回 0，失败返回负 errno。
 */
virtual int RunScriptlet(const ScriptletDescriptor& Descriptor, ScriptletPhase Phase,
                         const std::string& InstallRoot) = 0;
```

---

## 8. 对象模型的扩展

`object.h` 里现在是这样：

```cpp
std::vector<std::string> dependencies_;   // 只有 "GNU.make" 这种
```

RPM 需要的是：

```
Requires:    glibc >= 2.39
Requires:    /usr/bin/sh
Requires:    libstdc++.so.6()(64bit)
Requires:    (foo if bar)
Provides:    cmake = 3.30.5-1.fc41
Conflicts:   cmake < 3.28
Obsoletes:   cmake3 < 3.30
```

### 8.1 结构化依赖

```cpp
enum class DependencyKind {
	Requires, Provides, Conflicts, Obsoletes,
	Recommends, Suggests, Supplements, Enhances,
};

enum class ComparisonOperator { Any, Less, LessEqual, Equal, GreaterEqual, Greater };

/**
 * struct Dependency - 一条带约束的依赖。
 * @Namespace: 命名空间，可空。
 * @Name: 能力名。可以是包名、文件路径、soname。
 * @Operator: 版本比较符。
 * @Version: 版本字符串，按所属 scheme 解释。
 * @Kind: 依赖种类。
 */
struct Dependency {
	std::string Namespace;
	std::string Name;
	ComparisonOperator Operator;
	std::string Version;
	DependencyKind Kind;
};
```

### 8.2 能力是依赖的反面

`Provides` 在 RPM 里就是"这个包能当什么用"。所以 `Object` 需要：

```cpp
const std::vector<Dependency>& Capabilities() const;
void SetCapabilities(std::vector<Dependency> Capabilities);
```

关键事实：**Fedora 的依赖求解几乎全靠 Provides 虚拟能力**。`dnf install /usr/bin/cmake` 是文件依赖，`Requires: libstdc++.so.6()(64bit)` 是 soname 依赖。解析器必须能吃"文件路径当能力名"。

### 8.3 冻结规则

`oaabi.md` 第 4 节：

> 布局一旦发布就冻结。要加字段就新建一个结构体和对应的新符号。

`Object` 是 C++ 类、不是 OAABI 导出结构体，不受这条直接约束。但 `for_ai.md` 第 1 节要求"只对新增的函数、类型、宏、全局变量遵守本文"——所以 `dependencies_` 保持原样，`Capabilities()` 和 `Dependency` 是新增的，两者并存。老的裸字符串依赖继续能读，新的走结构化路径。

---

## 9. 装载与二进制边界

### 9.1 现在的问题

`extension_api.h` 的三个跨边界类型：

```cpp
using PluginInitFunc   = bool (*)(ExtensionApi*);
using HookCallback     = std::function<void(const Transaction&)>;
using OperationHandler = std::function<bool(const std::vector<std::string>& args)>;
```

全部是 libstdc++ 产物。`std::function`、`std::string`、`std::vector`、`Transaction`、`ExtensionApi` 都跨了 `.so` 边界。后果：模块必须和 lunar 用同一份 libstdc++、同一个 `_GLIBCXX_USE_CXX11_ABI`、同样的异常设置。Okra 用自建 gcc 16.2.0 和自建 glibc，没有稳定 C++ ABI 承诺，所以这不是模块化，是"静态链接的伪装"。

而 `oaabi.md` 第 13 节把这个接口明确列在：

> 现在还不是 OAABI 的接口。这些接口继续按它们今天的样子工作。新代码不要照着它们再导出一份。

### 9.2 两条路，选一条

**路线甲：C 链接门面。** 新插件导出 `struct OaabiPlugin`，`Version = 1`，`Reserved = 0`，`Init` / `Fini`。所有注册走 C 链接、PascalCase、`Oaabi` 前缀、只有 C 类型、聚合类型走指针、失败返回负 errno。加载器用 `RTLD_NOW | RTLD_LOCAL`。

```c
/**
 * OaabiRegisterRepositoryBackend() - 注册一个仓库后端。
 * @Descriptor: 后端描述，调用方拥有所有权。
 *
 * Return: 成功返回 0，失败返回负 errno。
 */
int32_t OaabiRegisterRepositoryBackend(const struct OaabiRepositoryDescriptor* Descriptor);
```

**路线乙：内置扩展。** 不做 `.so`，把 RPM 支持编进 lunar，像现在的 `lunar-core` / `lunar-oaa` / `okrapm` 三个内置扩展一样。

**建议先走乙，再走甲。** 乙能立刻验证对象模型和事务模型改得对不对，不用先解决 ABI 问题；等模型稳定了，再把边界换成甲。反过来的话，你会一边改 ABI 一边改模型，两边的 bug 混在一起。

---

## 10. 落地顺序

每一步都能独立提交、独立验证。

| 序 | 改动 | 依赖 | 可验证的标志 |
|---|---|---|---|
| 1 | 本文进 DOCS | 无 | 合同有权威源 |
| 2 | `VersionScheme` + `semver` + `rpm-evr` | 无 | `rpmvercmp` 边界用例全过 |
| 3 | `Object` 加 `Capabilities()` + `Dependency` | 无 | 旧仓库仍能 `lunar list` |
| 4 | `ArtifactBackend` 抽接口，OAA 成为第一个实现 | 3 | 装 `.oaa` 行为不变 |
| 5 | 事务加 scriptlet 阶段，OAA 的 `scripts/pre-install` 进提交路径 | 4 | 旧包多跑一个钩子，不回归 |
| 6 | `rpm-repo` 只读原型：`sync` + `list_objects` + `find` | 2, 3 | `lunar search fedora.cmake` 有结果 |
| 7 | `rpm` 的 `ArtifactBackend` + 可行性数据 | 4, 5, 6 | 见 `rpm.md` |
| 8 | C 链接门面，边界换成 `OaabiPlugin` | 7 | 模块可单独重编 |

第 2、3 步完全不依赖 librpm，也不依赖任何架构决策，可以立刻开工。
