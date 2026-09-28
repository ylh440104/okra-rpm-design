# Object 集成指引

把 `Dependency` 接进 `Object` 的步骤，以及**一个我无法验证的阻塞问题**。

---

## 1. 现在的状态

| 部分 | 状态 |
|---|---|
| `VersionScheme` / `RpmEvrScheme` / `SemverScheme` | 完成，103 条单测 + 40 万对差分 |
| `Dependency` / `DependencyKind` / `DependencySource` | 完成，98 条单测 + 126 组矩阵差分 |
| `DependencySatisfiedBy` | 完成 |
| 依赖块的序列化 | 完成，39 条单测 + 2 万轮随机往返 |
| **接进 `object.h`** | **未做，见第 3 节的阻塞问题** |

新代码在 `code/lib/okrapmlib/`，**完全独立，没有改动 `okrapm` 仓库里的任何文件**。

---

## 2. 要加的四个方法

按 `for_ai.md` 第 1 节，**只新增，不重排旧代码**。

```cpp
// object.h 新增
#include "okrapmlib/dependency.h"

// 在 Object 类里加：

	/**
	 * Capabilities() - 返回本对象提供的能力。
	 *
	 * Return: 能力列表，元素是 Kind 为 Provides 的 Dependency。
	 */
	const std::vector<Dependency>& Capabilities() const { return Capabilities_; }

	/**
	 * SetCapabilities() - 替换能力列表。
	 * @Items: 新的能力列表。
	 */
	void SetCapabilities(std::vector<Dependency> Items) { Capabilities_ = std::move(Items); }

	/**
	 * Dependencies() - 返回本对象的结构化依赖。
	 *
	 * 与已有的 dependencies_ 并存。dependencies_ 是裸字符串列表，只含
	 * namespace.name；本方法返回带比较符和版本的完整信息。
	 *
	 * Return: 依赖列表。
	 */
	const std::vector<Dependency>& Dependencies() const { return StructuredDependencies_; }

	/**
	 * SetDependencies() - 替换结构化依赖列表。
	 * @Items: 新的依赖列表。
	 */
	void SetDependencies(std::vector<Dependency> Items)
	{
		StructuredDependencies_ = std::move(Items);
	}

// 在 protected 段末尾加两个成员。不要动已有的 dependencies_。
	std::vector<Dependency> Capabilities_;
	std::vector<Dependency> StructuredDependencies_;
```

**关键点**：旧的 `std::vector<std::string> dependencies_;` **原样保留**。老的裸字符串依赖继续能读能写，新的走结构化路径。两条路并存是 `for_ai.md` 第 1 节要求的做法。

---

## 3. 阻塞问题：前向兼容没验证

`object.h` 里只有声明：

```cpp
virtual std::string serialize() const;
static std::optional<Object> deserialize(const std::string& data);
```

**格式实现在 `object.cpp`，我没有读到。**

这让"把依赖块拼到 `serialize()` 输出后面"这个做法有一个**我无法排除的风险**：

```
旧 lunar 读 新 system.db
  旧解析器遇到 "@dependencies\t1" 这一行
    ├── 如果它跳过不认识的行        → 安全
    └── 如果它把每行都当依赖名       → 多出一个叫 "@dependencies\t1" 的假依赖
                                      或者直接解析失败，状态库读不出来
```

`okrapm.md` 记的仓库格式是"一行一个对象"，但 `Object::serialize()` 的内部格式是另一回事。

### 我做了什么来降低风险

新代码满足三条：

1. **空列表序列化成空串**（有单测）。所以没有结构化依赖的旧对象，`serialize()` 输出**字节不变**。
2. **块是自包含的**，只由 `@dependencies` / `@dependency` 开头的行组成，不依赖上下文。
3. **解析时跳过不认识的行**（有单测）。所以新解析器读旧文件没问题——这个方向是安全的。

**新读旧：已证明安全。**
**旧读新：未验证。**

### 三条出路

| 方案 | 做法 | 评价 |
|---|---|---|
| **甲** | 读到 `object.cpp` 的格式之后，确认旧解析器会跳过不认识的行 | 最省事，但要先看代码 |
| **乙** | 依赖块不拼进 `Object::serialize()`，改成 `system.db` 里的独立段落 | 完全避开前向兼容问题，但要改存储层 |
| **丙** | 给 `system.db` 加整体版本号，旧 lunar 见到新版本直接拒绝启动 | 最稳，但要改文件头 |

**建议甲**，前提是先看到 `object.cpp`。

---

## 4. 怎么验证接对了

接进去之后跑这几个：

```bash
# 1. 旧对象输出字节不变
#    造一个没有结构化依赖的对象，序列化，和改动前的输出逐字节比

# 2. 新对象往返
#    造一个带 Capabilities / Dependencies 的对象，序列化再解析，逐字段比

# 3. 旧文件能读
#    拿一个改动前生成的 system.db，用新代码读，确认所有对象都在

# 4. 回归
#    103 + 98 + 39 条单测全过
#    126 组依赖矩阵一致
#    40 万对版本比较一致
```

第 1 条和第 3 条是这次改动的**真正风险点**，第 2 条和第 4 条是常规回归。

---

## 5. 序列化格式速查

```
@dependencies<TAB>1
@dependency<TAB>requires<TAB>manual<TAB><TAB>glibc<TAB>>=<TAB>2.39
@dependency<TAB>provides<TAB>manual<TAB><TAB>cmake<TAB>=<TAB>3.30.5-1.fc41
@dependency<TAB>requires<TAB>rpmlib<TAB><TAB>rpmlib(FileDigests)<TAB><=<TAB>4.6.0-1
```

七个字段：前缀、Kind、Source、Namespace、Name、Operator、Version。

转义：`\\` `\t` `\n` `\r`。

空列表 → 空串。

字段多于七个时忽略多余的，便于以后加字段。

---

## 6. 还没做的

- **`Conflicts` / `Obsoletes` 的求解语义。** 数据结构能装，但没实现"两个包冲突"和"旧包被替代"的判定。
- **rich deps。** `(foo if bar)` 能存能读，但不参与求解。
- **弱依赖的版本约束。** 实测发现 `rpm -qp --qf` 对 `RECOMMENDS` / `SUGGESTS` 只给出名字；但 `rpm -qp --recommends` 能显示 `nice >= 1.0`，说明版本在，只是要用 librpm 的 `rpmds` API 取。这台机器没有 `librpm-dev`，验证不了。
- **`ArtifactMetadata` 的对接。** `ArtifactMetadata::dependencies` 也是裸字符串列表，同样要加结构化版本。