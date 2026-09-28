// dependency_test.cpp - dependency 的测试。
//
// 期望值来源分两类，都在注释里标了：
//
//   [rpm]  用独立 rpmdb 跑真实依赖求解得出的，命令见 verification.md。
//          这一类是权威的，禁止手改。
//   [约定] 本文件自己定的语义，用于锁住行为，防止以后改动破坏兼容。
//
// 构建与运行：
//   g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/dep_test dependency_test.cpp \
//       ../src/dependency.cpp ../src/version_scheme.cpp -I../include
//   /tmp/dep_test

#include "okrapmlib/dependency.h"
#include "okrapmlib/version_scheme.h"

#include <cerrno>
#include <cstdio>
#include <string>

using okrapm::ComparisonOperator;
using okrapm::Dependency;
using okrapm::DependencyIsInternal;
using okrapm::DependencyKind;
using okrapm::DependencyKindText;
using okrapm::DependencySatisfiedBy;
using okrapm::DependencySource;
using okrapm::DependencySourceText;
using okrapm::FilterInternalDependencies;
using okrapm::ParseComparisonOperator;
using okrapm::ParseDependencyKind;
using okrapm::ParseDependencySource;
using okrapm::RpmEvrScheme;
using okrapm::ComparisonOperatorText;

static int Total = 0;
static int Failed = 0;

// ExpectInt() - 断言两个整数相等。
static void ExpectInt(const char* What, int Actual, int Expected)
{
	Total++;
	if (Actual != Expected) {
		Failed++;
		std::printf("  FAIL %s   期望 %d 实际 %d\n", What, Expected, Actual);
		return;
	}
	std::printf("  ok   %s = %d\n", What, Actual);
}

// ExpectText() - 断言两个字符串相等。
static void ExpectText(const char* What, const std::string& Actual, const std::string& Expected)
{
	Total++;
	if (Actual != Expected) {
		Failed++;
		std::printf("  FAIL %s   期望 \"%s\" 实际 \"%s\"\n", What, Expected.c_str(),
		            Actual.c_str());
		return;
	}
	std::printf("  ok   %s = \"%s\"\n", What, Actual.c_str());
}

// MakeCapability() - 造一条 Provides。
// @Name: 能力名。
// @Version: 版本，空串表示无版本。
static Dependency MakeCapability(const std::string& Name, const std::string& Version)
{
	Dependency Item;
	Item.Name = Name;
	Item.Kind = DependencyKind::Provides;
	Item.Source = DependencySource::Manual;
	if (!Version.empty()) {
		Item.Operator = ComparisonOperator::Equal;
		Item.Version = Version;
	}
	return Item;
}

// MakeRequirement() - 造一条 Requires。
// @Name: 能力名。
// @Operator: 比较符。
// @Version: 版本。
static Dependency MakeRequirement(const std::string& Name, ComparisonOperator Operator,
	                              const std::string& Version)
{
	Dependency Item;
	Item.Name = Name;
	Item.Kind = DependencyKind::Requires;
	Item.Source = DependencySource::Manual;
	Item.Operator = Operator;
	Item.Version = Version;
	return Item;
}

// CheckSatisfied() - 断言一条需求被一条能力满足或不被满足。
// @Requirement @Capability: 两个操作数。
// @Expected: 期望是否满足。
// @Note: 说明，标注来源。
static void CheckSatisfied(const Dependency& Requirement, const Dependency& Capability,
	                       bool Expected, const std::string& Note)
{
	Total++;

	RpmEvrScheme Scheme;
	bool Satisfied = false;
	const int Result = DependencySatisfiedBy(Requirement, Capability, Scheme, Satisfied);

	if (Result != 0) {
		Failed++;
		std::printf("  FAIL 调用失败 %d   %s\n", Result, Note.c_str());
		return;
	}
	if (Satisfied != Expected) {
		Failed++;
		std::printf("  FAIL %s vs %s  期望 %s 实际 %s   %s\n", Requirement.Name.c_str(),
		            Capability.Name.c_str(), Expected ? "满足" : "不满足",
		            Satisfied ? "满足" : "不满足", Note.c_str());
		return;
	}
	std::printf("  ok   %s%s%s <- %s%s%s  %s   %s\n", Requirement.Name.c_str(),
	            ComparisonOperatorText(Requirement.Operator).c_str(), Requirement.Version.c_str(),
	            Capability.Name.c_str(), ComparisonOperatorText(Capability.Operator).c_str(),
	            Capability.Version.c_str(), Satisfied ? "满足" : "不满足", Note.c_str());
}

// TestSatisfactionMatrix() - 12 组权威矩阵。
//
// 这一节全部是 [rpm] 来源。命令：
//   rpm -i --dbpath <库> --nodeps --noscripts --justdb <Provides 包>
//   rpm -i --test --dbpath <库> <Requires 包>     # 退出码 0 表示满足
static void TestSatisfactionMatrix()
{
	std::printf("\n[1] 满足性矩阵（来源：rpm 真实求解）\n");

	// 三个提供者
	const Dependency VirtNoVersion = MakeCapability("virt", "");
	const Dependency Virt15 = MakeCapability("virt", "1.5");
	const Dependency Virt99 = MakeCapability("virt", "9.9");

	// 四个需求者
	const Dependency AnyVirt = MakeRequirement("virt", ComparisonOperator::Any, "");
	const Dependency Ge10 = MakeRequirement("virt", ComparisonOperator::GreaterEqual, "1.0");
	const Dependency Ge20 = MakeRequirement("virt", ComparisonOperator::GreaterEqual, "2.0");
	const Dependency Eq15 = MakeRequirement("virt", ComparisonOperator::Equal, "1.5");

	// Provides: virt（无版本）—— 满足一切。这是最反直觉的一组。
	CheckSatisfied(AnyVirt, VirtNoVersion, true, "[rpm] 无版本能力");
	CheckSatisfied(Ge10, VirtNoVersion, true, "[rpm] 无版本能力满足 >= 1.0");
	CheckSatisfied(Ge20, VirtNoVersion, true, "[rpm] 无版本能力满足 >= 2.0");
	CheckSatisfied(Eq15, VirtNoVersion, true, "[rpm] 无版本能力满足 = 1.5");

	// Provides: virt = 1.5
	CheckSatisfied(AnyVirt, Virt15, true, "[rpm]");
	CheckSatisfied(Ge10, Virt15, true, "[rpm] 1.5 >= 1.0");
	CheckSatisfied(Ge20, Virt15, false, "[rpm] 1.5 不满足 >= 2.0");
	CheckSatisfied(Eq15, Virt15, true, "[rpm] 1.5 = 1.5");

	// Provides: virt = 9.9
	CheckSatisfied(AnyVirt, Virt99, true, "[rpm]");
	CheckSatisfied(Ge10, Virt99, true, "[rpm] 9.9 >= 1.0");
	CheckSatisfied(Ge20, Virt99, true, "[rpm] 9.9 >= 2.0");
	CheckSatisfied(Eq15, Virt99, false, "[rpm] 9.9 不等于 1.5");
}

// TestNameMatching() - 名字匹配规则。
static void TestNameMatching()
{
	std::printf("\n[2] 名字匹配\n");

	// RPM 不做归一化。文件路径和 soname 都只是普通名字。
	const Dependency NeedsSh = MakeRequirement("/usr/bin/sh", ComparisonOperator::Any, "");
	const Dependency HasSh = MakeCapability("/usr/bin/sh", "");
	CheckSatisfied(NeedsSh, HasSh, true, "[约定] 文件依赖按字符串比");

	const Dependency NeedsSo = MakeRequirement("libc.so.6()(64bit)", ComparisonOperator::Any, "");
	const Dependency HasSo = MakeCapability("libc.so.6()(64bit)", "");
	CheckSatisfied(NeedsSo, HasSo, true, "[约定] soname 依赖按字符串比");

	// 大小写敏感，不做折叠。
	const Dependency NeedsUpper = MakeRequirement("Virt", ComparisonOperator::Any, "");
	const Dependency HasLower = MakeCapability("virt", "");
	CheckSatisfied(NeedsUpper, HasLower, false, "[约定] 大小写敏感");

	// 名字不同就不满足。
	const Dependency NeedsOther = MakeRequirement("other", ComparisonOperator::Any, "");
	CheckSatisfied(NeedsOther, HasSh, false, "[约定] 名字不同");

	// 命名空间不同也不满足。
	Dependency NeedsNs = MakeRequirement("virt", ComparisonOperator::Any, "");
	NeedsNs.Namespace = "fedora";
	Dependency HasNoNs = MakeCapability("virt", "");
	CheckSatisfied(NeedsNs, HasNoNs, false, "[约定] 命名空间不同");
}

// TestOperators() - 全部比较符。
static void TestOperators()
{
	std::printf("\n[3] 比较符\n");

	const Dependency Cap = MakeCapability("x", "5.0");

	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Any, ""), Cap, true, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Less, "6.0"), Cap, true, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Less, "5.0"), Cap, false, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Less, "4.0"), Cap, false, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::LessEqual, "5.0"), Cap, true, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::LessEqual, "4.0"), Cap, false, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Equal, "5.0"), Cap, true, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Equal, "5.1"), Cap, false, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::GreaterEqual, "5.0"), Cap, true,
	               "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::GreaterEqual, "6.0"), Cap, false,
	               "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Greater, "4.0"), Cap, true, "[约定]");
	CheckSatisfied(MakeRequirement("x", ComparisonOperator::Greater, "5.0"), Cap, false, "[约定]");

	// 版本比较走 rpm-evr，但依赖匹配有它自己那一套 release 规则。
	// 下面这四条按 tests/probe-full-matrix.sh 的实测结果写。
	const Dependency FedoraCap = MakeCapability("cmake", "3.30.5-1.fc41");
	CheckSatisfied(MakeRequirement("cmake", ComparisonOperator::GreaterEqual, "3.30.5"), FedoraCap,
	               true, "[rpm] release 段让版本更大");
	CheckSatisfied(MakeRequirement("cmake", ComparisonOperator::Greater, "3.30.5"), FedoraCap, false,
	               "[rpm] 需求侧无 release，只比 version，相等所以 > 不成立");
	CheckSatisfied(MakeRequirement("cmake", ComparisonOperator::Equal, "3.30.5"), FedoraCap, true,
	               "[rpm] 需求侧无 release，只比 version，相等");
	CheckSatisfied(MakeRequirement("cmake", ComparisonOperator::Equal, "3.30.5-1.fc41"), FedoraCap,
	               true, "[rpm] 两侧都有 release，相等");
}

// TestInvalidArguments() - 参数不合法要返回 -EINVAL。
static void TestInvalidArguments()
{
	std::printf("\n[4] 参数校验\n");

	RpmEvrScheme Scheme;
	bool Satisfied = false;

	// 比较符不是 Any，但版本为空。
	const Dependency BadRequirement = MakeRequirement("x", ComparisonOperator::GreaterEqual, "");
	const Dependency Cap = MakeCapability("x", "1.0");

	ExpectInt("带比较符但无版本返回 -EINVAL",
	          DependencySatisfiedBy(BadRequirement, Cap, Scheme, Satisfied), -EINVAL);

	// 无约束但版本非空，不算错，版本被忽略。
	Dependency OddRequirement = MakeRequirement("x", ComparisonOperator::Any, "9.9");
	ExpectInt("无约束但带版本不报错",
	          DependencySatisfiedBy(OddRequirement, Cap, Scheme, Satisfied), 0);
	ExpectInt("无约束时忽略版本", Satisfied ? 1 : 0, 1);
}

// TestParsers() - 字符串与枚举的互转。
static void TestParsers()
{
	std::printf("\n[5] 解析与格式化\n");

	ComparisonOperator Operator = ComparisonOperator::Any;
	ExpectInt("Parse(\"\")", ParseComparisonOperator("", Operator), 0);
	ExpectInt("  结果是 Any", Operator == ComparisonOperator::Any ? 1 : 0, 1);
	ExpectInt("Parse(\"=\")", ParseComparisonOperator("=", Operator), 0);
	ExpectInt("  结果是 Equal", Operator == ComparisonOperator::Equal ? 1 : 0, 1);
	ExpectInt("Parse(\"==\")", ParseComparisonOperator("==", Operator), 0);
	ExpectInt("  结果是 Equal", Operator == ComparisonOperator::Equal ? 1 : 0, 1);
	ExpectInt("Parse(\">=\")", ParseComparisonOperator(">=", Operator), 0);
	ExpectInt("  结果是 GreaterEqual", Operator == ComparisonOperator::GreaterEqual ? 1 : 0, 1);
	ExpectInt("Parse(\"<=\")", ParseComparisonOperator("<=", Operator), 0);
	ExpectInt("  结果是 LessEqual", Operator == ComparisonOperator::LessEqual ? 1 : 0, 1);
	ExpectInt("Parse(\"!\")", ParseComparisonOperator("!", Operator), -EINVAL);
	ExpectInt("Parse(\"===\")", ParseComparisonOperator("===", Operator), -EINVAL);

	ExpectText("Text(Any)", ComparisonOperatorText(ComparisonOperator::Any), "");
	ExpectText("Text(Less)", ComparisonOperatorText(ComparisonOperator::Less), "<");
	ExpectText("Text(LessEqual)", ComparisonOperatorText(ComparisonOperator::LessEqual), "<=");
	ExpectText("Text(Equal)", ComparisonOperatorText(ComparisonOperator::Equal), "=");
	ExpectText("Text(GreaterEqual)", ComparisonOperatorText(ComparisonOperator::GreaterEqual), ">=");
	ExpectText("Text(Greater)", ComparisonOperatorText(ComparisonOperator::Greater), ">");

	// 往返一致性。
	const ComparisonOperator AllOperators[] = {
		ComparisonOperator::Any,       ComparisonOperator::Less,
		ComparisonOperator::LessEqual, ComparisonOperator::Equal,
		ComparisonOperator::GreaterEqual, ComparisonOperator::Greater,
	};
	int RoundTripBad = 0;
	for (ComparisonOperator Item : AllOperators) {
		ComparisonOperator Parsed = ComparisonOperator::Any;
		const std::string Text = ComparisonOperatorText(Item);
		if (ParseComparisonOperator(Text, Parsed) != 0 || Parsed != Item) {
			RoundTripBad++;
		}
	}
	ExpectInt("比较符往返一致", RoundTripBad, 0);

	const DependencyKind AllKinds[] = {
		DependencyKind::Provides,    DependencyKind::Requires,
		DependencyKind::Conflicts,   DependencyKind::Obsoletes,
		DependencyKind::Recommends,  DependencyKind::Suggests,
		DependencyKind::Supplements, DependencyKind::Enhances,
	};
	int KindBad = 0;
	for (DependencyKind Item : AllKinds) {
		DependencyKind Parsed = DependencyKind::Requires;
		const std::string Text = DependencyKindText(Item);
		if (ParseDependencyKind(Text, Parsed) != 0 || Parsed != Item) {
			KindBad++;
		}
	}
	ExpectInt("依赖种类往返一致", KindBad, 0);

	DependencyKind BadKind = DependencyKind::Requires;
	ExpectInt("ParseDependencyKind(\"bogus\")", ParseDependencyKind("bogus", BadKind), -EINVAL);

	const DependencySource AllSources[] = {
		DependencySource::Unknown, DependencySource::Manual,
		DependencySource::Automatic, DependencySource::Rpmlib,
	};
	int SourceBad = 0;
	for (DependencySource Item : AllSources) {
		DependencySource Parsed = DependencySource::Unknown;
		const std::string Text = DependencySourceText(Item);
		if (ParseDependencySource(Text, Parsed) != 0 || Parsed != Item) {
			SourceBad++;
		}
	}
	ExpectInt("来源往返一致", SourceBad, 0);

	// RPM 在 deptype 里写 "auto"。
	DependencySource Parsed = DependencySource::Unknown;
	ExpectInt("ParseDependencySource(\"auto\")", ParseDependencySource("auto", Parsed), 0);
	ExpectInt("  auto 映射到 Automatic", Parsed == DependencySource::Automatic ? 1 : 0, 1);
	ExpectInt("ParseDependencySource(\"automatic\")", ParseDependencySource("automatic", Parsed), 0);
	ExpectInt("ParseDependencySource(\"bogus\")", ParseDependencySource("bogus", Parsed), -EINVAL);
}

// TestInternalFiltering() - rpmlib(...) 的过滤。
static void TestInternalFiltering()
{
	std::printf("\n[6] 内部依赖过滤\n");

	// 实测自 rpm 4.18，deptype 的取值只有 manual、auto、rpmlib 三种。
	Dependency RpmlibSource;
	RpmlibSource.Name = "rpmlib(FileDigests)";
	RpmlibSource.Source = DependencySource::Rpmlib;
	ExpectInt("来源是 rpmlib 的算内部", DependencyIsInternal(RpmlibSource) ? 1 : 0, 1);

	// 名字是 rpmlib( 开头，即使来源没标。
	Dependency RpmlibName;
	RpmlibName.Name = "rpmlib(CompressedFileNames)";
	RpmlibName.Source = DependencySource::Manual;
	ExpectInt("名字是 rpmlib( 的算内部", DependencyIsInternal(RpmlibName) ? 1 : 0, 1);

	// 正常的依赖不算内部。
	Dependency Normal = MakeRequirement("/usr/bin/sh", ComparisonOperator::Any, "");
	ExpectInt("/usr/bin/sh 不算内部", DependencyIsInternal(Normal) ? 1 : 0, 0);

	// 名字以 rpmlib 开头但没带括号的，不算内部。
	Dependency Trick = MakeRequirement("rpmlibtool", ComparisonOperator::Any, "");
	ExpectInt("rpmlibtool 不算内部", DependencyIsInternal(Trick) ? 1 : 0, 0);

	// 短名字不能越界。
	Dependency Short = MakeRequirement("rpm", ComparisonOperator::Any, "");
	ExpectInt("rpm 不算内部", DependencyIsInternal(Short) ? 1 : 0, 0);

	Dependency Empty;
	ExpectInt("空名字不算内部", DependencyIsInternal(Empty) ? 1 : 0, 0);

	// 过滤保持顺序。
	std::vector<Dependency> Input;
	Input.push_back(MakeRequirement("alpha", ComparisonOperator::Any, ""));
	Input.push_back(RpmlibName);
	Input.push_back(MakeRequirement("beta", ComparisonOperator::Any, ""));
	Input.push_back(RpmlibSource);
	Input.push_back(MakeRequirement("gamma", ComparisonOperator::Any, ""));

	const std::vector<Dependency> Output = FilterInternalDependencies(Input);
	ExpectInt("过滤后条数", static_cast<int>(Output.size()), 3);

	std::string Joined;
	for (const Dependency& Item : Output) {
		if (!Joined.empty()) {
			Joined += ",";
		}
		Joined += Item.Name;
	}
	ExpectText("过滤后顺序", Joined, "alpha,beta,gamma");
}

// TestSourceParsingFromRealRpms() - 用真实 rpm 的 deptype 值。
static void TestSourceParsingFromRealRpms()
{
	std::printf("\n[7] 真实 deptype 取值（来源：rpm 4.18 实测）\n");

	struct Case
	{
		const char* Text;
		DependencySource Expected;
	};
	// 这三个是实测 rpm 4.18 上出现过的全部取值。
	const Case Cases[] = {
		{ "manual", DependencySource::Manual },
		{ "auto", DependencySource::Automatic },
		{ "rpmlib", DependencySource::Rpmlib },
	};

	for (const Case& Item : Cases) {
		Total++;
		DependencySource Parsed = DependencySource::Unknown;
		const int Result = ParseDependencySource(Item.Text, Parsed);
		if (Result != 0 || Parsed != Item.Expected) {
			Failed++;
			std::printf("  FAIL deptype \"%s\" 解析失败\n", Item.Text);
			continue;
		}
		std::printf("  ok   deptype \"%s\" -> %s\n", Item.Text,
		            DependencySourceText(Parsed).c_str());
	}

	// 完整场景：一个 Fedora 包的 Requires 列表，含 rpmlib 内部依赖。
	std::vector<Dependency> Raw;

	Dependency Item;
	Item.Name = "glibc";
	Item.Operator = ComparisonOperator::GreaterEqual;
	Item.Version = "2.39";
	Item.Source = DependencySource::Manual;
	Raw.push_back(Item);

	Item = Dependency();
	Item.Name = "/usr/bin/sh";
	Item.Source = DependencySource::Manual;
	Raw.push_back(Item);

	Item = Dependency();
	Item.Name = "libstdc++.so.6()(64bit)";
	Item.Source = DependencySource::Automatic;
	Raw.push_back(Item);

	Item = Dependency();
	Item.Name = "rpmlib(FileDigests)";
	Item.Operator = ComparisonOperator::LessEqual;
	Item.Version = "4.6.0-1";
	Item.Source = DependencySource::Rpmlib;
	Raw.push_back(Item);

	Item = Dependency();
	Item.Name = "rpmlib(PayloadFilesHavePrefix)";
	Item.Operator = ComparisonOperator::LessEqual;
	Item.Version = "4.0-1";
	Item.Source = DependencySource::Rpmlib;
	Raw.push_back(Item);

	const std::vector<Dependency> Clean = FilterInternalDependencies(Raw);
	ExpectInt("5 条原始依赖过滤后剩 3 条", static_cast<int>(Clean.size()), 3);

	int RpmlibLeft = 0;
	for (const Dependency& Left : Clean) {
		if (DependencyIsInternal(Left)) {
			RpmlibLeft++;
		}
	}
	ExpectInt("过滤后没有内部依赖残留", RpmlibLeft, 0);
}

// TestReleaseAsymmetry() - release 通配的不对称性。
//
// 全部是 [rpm] 来源，来自 tests/probe-full-matrix.sh 的 126 组实测。
// 这一节专门锁住最容易写错的那一条：需求侧缺 release 和提供侧缺 release
// 行为**不对称**。
static void TestReleaseAsymmetry()
{
	std::printf("\n[8] release 通配的不对称性（来源：rpm 126 组矩阵）\n");

	// 提供侧无 release、需求侧有 → 提供侧通配，满足任何比较符。
	const Dependency CapNoRelease = MakeCapability("virt", "1.0");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0-1"), CapNoRelease, true,
	               "[rpm] 提供侧通配");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0-2"), CapNoRelease, true,
	               "[rpm] 提供侧通配");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Greater, "1.0-1"), CapNoRelease,
	               true, "[rpm] 提供侧通配，连 > 都满足");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Less, "1.0-2"), CapNoRelease, true,
	               "[rpm] 提供侧通配，连 < 也满足");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::LessEqual, "1.0-1"), CapNoRelease,
	               true, "[rpm]");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::GreaterEqual, "1.0-1"),
	               CapNoRelease, true, "[rpm]");

	// 提供侧有 release、需求侧无 → 只比 version，release 被忽略。
	const Dependency CapWithRelease = MakeCapability("virt", "1.0-1");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0"), CapWithRelease, true,
	               "[rpm] 需求侧无 release，只比 version");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Greater, "1.0"), CapWithRelease,
	               false, "[rpm] version 相等，> 不成立");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Less, "1.0"), CapWithRelease, false,
	               "[rpm] version 相等，< 不成立");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::GreaterEqual, "1.0"),
	               CapWithRelease, true, "[rpm]");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::LessEqual, "1.0"), CapWithRelease,
	               true, "[rpm]");

	// 两侧都有 release → 正常比 release。
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0-1"), CapWithRelease,
	               true, "[rpm]");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0-2"), CapWithRelease,
	               false, "[rpm] 1 不等于 2");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Greater, "1.0-1"), CapWithRelease,
	               false, "[rpm] 相等");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Less, "1.0-2"), CapWithRelease,
	               true, "[rpm] 1 < 2");

	// 空 release 在依赖匹配里视为"无"。这条与排序语义不同。
	const Dependency CapEmptyRelease = MakeCapability("virt", "1.0-");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0"), CapEmptyRelease,
	               true, "[rpm] 空 release 视为无");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Greater, "1.0"), CapEmptyRelease,
	               false, "[rpm] 空 release 视为无，version 相等");

	// epoch 不同时直接按 epoch 定胜负，不看 release。
	const Dependency CapEpoch = MakeCapability("virt", "1:1.0");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1:1.0"), CapEpoch, true,
	               "[rpm]");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0"), CapEpoch, false,
	               "[rpm] epoch 不同");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Greater, "1.0"), CapEpoch, true,
	               "[rpm] epoch 1 > 0");

	// version 段不同时正常比 version，不看 release。
	const Dependency CapVersionDiff = MakeCapability("virt", "2.0");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Equal, "1.0-1"), CapVersionDiff,
	               false, "[rpm] 2.0 不等于 1.0");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Greater, "1.0-1"), CapVersionDiff,
	               true, "[rpm] 2.0 > 1.0");
	CheckSatisfied(MakeRequirement("virt", ComparisonOperator::Less, "3.0-1"), CapVersionDiff, true,
	               "[rpm] 2.0 < 3.0");
}

int main()
{
	std::printf("dependency 测试\n");
	std::printf("带 [rpm] 的期望值来自真实 rpm 依赖求解，禁止手改\n");

	TestSatisfactionMatrix();
	TestNameMatching();
	TestOperators();
	TestInvalidArguments();
	TestParsers();
	TestInternalFiltering();
	TestSourceParsingFromRealRpms();
	TestReleaseAsymmetry();

	std::printf("\n========================================\n");
	std::printf("共 %d 条，失败 %d 条\n", Total, Failed);
	std::printf("========================================\n");
	return Failed == 0 ? 0 : 1;
}