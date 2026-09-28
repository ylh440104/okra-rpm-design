// dependency_serialization_test.cpp - 依赖序列化的测试。
//
// 重点测三件事：
//   1. 往返一致。序列化再解析回来必须和原来一样。
//   2. 空列表输出空串。这是"旧对象输出字节不变"的保证。
//   3. 坏输入被拒绝，而不是静默接受。
//
// 构建与运行：
//   g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/ds_test dependency_serialization_test.cpp \
//       ../src/dependency_serialization.cpp ../src/dependency.cpp \
//       ../src/version_scheme.cpp -I../include
//   /tmp/ds_test

#include "okrapmlib/dependency_serialization.h"

#include <cerrno>
#include <cstdio>
#include <string>

using okrapm::ComparisonOperator;
using okrapm::Dependency;
using okrapm::DependencyIsInternal;
using okrapm::DependencyKind;
using okrapm::DependencySource;
using okrapm::LooksLikeDependencyBlock;
using okrapm::ParseDependencies;
using okrapm::SerializeDependencies;

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
		std::printf("  FAIL %s\n    期望 [%s]\n    实际 [%s]\n", What, Expected.c_str(),
		            Actual.c_str());
		return;
	}
	std::printf("  ok   %s\n", What);
}

// MakeItem() - 造一条依赖。
static Dependency MakeItem(DependencyKind Kind, DependencySource Source, const std::string& Namespace,
	                       const std::string& Name, ComparisonOperator Operator,
	                       const std::string& Version)
{
	Dependency Item;
	Item.Kind = Kind;
	Item.Source = Source;
	Item.Namespace = Namespace;
	Item.Name = Name;
	Item.Operator = Operator;
	Item.Version = Version;
	return Item;
}

// SameItem() - 判断两条依赖完全相等。
static bool SameItem(const Dependency& Left, const Dependency& Right)
{
	return Left.Kind == Right.Kind && Left.Source == Right.Source &&
	       Left.Namespace == Right.Namespace && Left.Name == Right.Name &&
	       Left.Operator == Right.Operator && Left.Version == Right.Version;
}

// TestEmptyIsEmptyString() - 空列表必须序列化成空串。
//
// 这是整件事的前提：旧对象的依赖列表为空，拼上这个块之后输出必须字节不变。
static void TestEmptyIsEmptyString()
{
	std::printf("\n[1] 空列表序列化成空串\n");

	const std::vector<Dependency> Empty;
	ExpectText("空列表 -> 空串", SerializeDependencies(Empty), "");
	ExpectInt("空列表长度为 0", static_cast<int>(SerializeDependencies(Empty).size()), 0);

	std::vector<Dependency> Parsed;
	Parsed.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "", "dummy",
	                          ComparisonOperator::Any, ""));
	ExpectInt("解析空串返回 0", ParseDependencies("", Parsed), 0);
	ExpectInt("解析空串得到空列表", static_cast<int>(Parsed.size()), 0);
}

// TestRoundTrip() - 往返一致。
static void TestRoundTrip()
{
	std::printf("\n[2] 往返一致\n");

	std::vector<Dependency> Items;
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "glibc", ComparisonOperator::GreaterEqual, "2.39"));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "/usr/bin/sh", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Automatic, "",
	                         "libstdc++.so.6()(64bit)", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Provides, DependencySource::Manual, "",
	                         "cmake", ComparisonOperator::Equal, "3.30.5-1.fc41"));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Rpmlib, "",
	                         "rpmlib(FileDigests)", ComparisonOperator::LessEqual, "4.6.0-1"));
	Items.push_back(MakeItem(DependencyKind::Conflicts, DependencySource::Manual, "fedora",
	                         "cmake", ComparisonOperator::Less, "3.28"));
	Items.push_back(MakeItem(DependencyKind::Obsoletes, DependencySource::Manual, "",
	                         "cmake3", ComparisonOperator::Less, "3.30"));
	Items.push_back(MakeItem(DependencyKind::Recommends, DependencySource::Manual, "",
	                         "nice", ComparisonOperator::GreaterEqual, "1.0"));
	Items.push_back(MakeItem(DependencyKind::Suggests, DependencySource::Manual, "",
	                         "maybe", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Supplements, DependencySource::Manual, "",
	                         "supp", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Enhances, DependencySource::Manual, "",
	                         "enh", ComparisonOperator::Any, ""));

	const std::string Text = SerializeDependencies(Items);
	std::vector<Dependency> Parsed;
	ExpectInt("解析返回 0", ParseDependencies(Text, Parsed), 0);
	ExpectInt("条数一致", static_cast<int>(Parsed.size()), static_cast<int>(Items.size()));

	int Mismatch = 0;
	const size_t Common = Parsed.size() < Items.size() ? Parsed.size() : Items.size();
	for (size_t Index = 0; Index < Common; Index++) {
		if (!SameItem(Parsed[Index], Items[Index])) {
			Mismatch++;
		}
	}
	ExpectInt("逐条一致", Mismatch, 0);
	ExpectText("二次序列化相同", SerializeDependencies(Parsed), Text);
}

// TestEscaping() - 转义。
static void TestEscaping()
{
	std::printf("\n[3] 转义\n");

	std::vector<Dependency> Items;
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "has\ttab", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "has\\backslash", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "has\nnewline", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "has\rcarriage", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "rich (foo if bar)", ComparisonOperator::Any, ""));

	const std::string Text = SerializeDependencies(Items);
	std::vector<Dependency> Parsed;
	ExpectInt("解析返回 0", ParseDependencies(Text, Parsed), 0);
	ExpectInt("条数一致", static_cast<int>(Parsed.size()), 5);

	if (Parsed.size() == 5) {
		ExpectText("制表符还原", Parsed[0].Name, "has\ttab");
		ExpectText("反斜杠还原", Parsed[1].Name, "has\\backslash");
		ExpectText("换行还原", Parsed[2].Name, "has\nnewline");
		ExpectText("回车还原", Parsed[3].Name, "has\rcarriage");
		ExpectText("rich deps 原样存", Parsed[4].Name, "rich (foo if bar)");
	}

	// 每一行都应以 @ 开头，否则说明有没转义的换行漏出来了。
	int BadLines = 0;
	std::string Current;
	for (char Character : Text) {
		if (Character == '\n') {
			if (!Current.empty() && Current[0] != '@') {
				BadLines++;
			}
			Current.clear();
		} else {
			Current.push_back(Character);
		}
	}
	ExpectInt("所有行都以 @ 开头", BadLines, 0);
}

// TestEmbeddedInLargerText() - 能嵌在别的文本里。
static void TestEmbeddedInLargerText()
{
	std::printf("\n[4] 嵌在更大的文本里\n");

	std::vector<Dependency> Items;
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "glibc", ComparisonOperator::GreaterEqual, "2.39"));

	const std::string Block = SerializeDependencies(Items);

	// 模拟一个已有格式：前面几行旧字段，然后是我们的块，然后更多旧内容。
	const std::string Whole = "namespace=GNU\nname=gcc\nversion=16.2.1\n" + Block +
	                          "files=/usr/bin/gcc\nfiles=/usr/lib/libgcc.a\n";

	std::vector<Dependency> Parsed;
	ExpectInt("解析返回 0", ParseDependencies(Whole, Parsed), 0);
	ExpectInt("只取到 1 条", static_cast<int>(Parsed.size()), 1);
	if (Parsed.size() == 1) {
		ExpectText("名字正确", Parsed[0].Name, "glibc");
		ExpectText("版本正确", Parsed[0].Version, "2.39");
	}

	// 旧格式文本（没有块）应当解析成空列表且不报错。
	const std::string Old = "namespace=GNU\nname=gcc\nversion=16.2.1\ndep=GNU.make\n";
	std::vector<Dependency> OldParsed;
	OldParsed.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "", "x",
	                             ComparisonOperator::Any, ""));
	ExpectInt("旧格式返回 0", ParseDependencies(Old, OldParsed), 0);
	ExpectInt("旧格式得到空列表", static_cast<int>(OldParsed.size()), 0);

	ExpectInt("识别出块", LooksLikeDependencyBlock(Whole) ? 1 : 0, 1);
	ExpectInt("旧格式没有块", LooksLikeDependencyBlock(Old) ? 1 : 0, 0);
	ExpectInt("空串没有块", LooksLikeDependencyBlock("") ? 1 : 0, 0);
}

// TestBadInput() - 坏输入要被拒绝。
static void TestBadInput()
{
	std::printf("\n[5] 坏输入被拒绝\n");

	std::vector<Dependency> Parsed;

	ExpectInt("头版本 2 被拒绝", ParseDependencies("@dependencies\t2\n", Parsed), -EINVAL);
	ExpectInt("头缺版本被拒绝", ParseDependencies("@dependencies\n", Parsed), -EINVAL);
	ExpectInt("行没有头被拒绝",
	          ParseDependencies("@dependency\trequires\tmanual\t\tx\t\t\n", Parsed), -EINVAL);
	ExpectInt("行字段不足被拒绝",
	          ParseDependencies("@dependencies\t1\n@dependency\trequires\tmanual\t\tx\n", Parsed),
	          -EINVAL);
	ExpectInt("Kind 不认识被拒绝",
	          ParseDependencies("@dependencies\t1\n@dependency\tbogus\tmanual\t\tx\t\t\n", Parsed),
	          -EINVAL);
	ExpectInt("Source 不认识被拒绝",
	          ParseDependencies("@dependencies\t1\n@dependency\trequires\tbogus\t\tx\t\t\n", Parsed),
	          -EINVAL);
	ExpectInt("Operator 不认识被拒绝",
	          ParseDependencies("@dependencies\t1\n@dependency\trequires\tmanual\t\tx\t!=\t\n",
	                            Parsed),
	          -EINVAL);

	// 字段多于 7 列要容忍，便于以后加字段。
	ExpectInt("多余字段被忽略",
	          ParseDependencies(
		          "@dependencies\t1\n@dependency\trequires\tmanual\t\tx\t\t\textra1\textra2\n",
		          Parsed),
	          0);
	ExpectInt("  仍然解析出 1 条", static_cast<int>(Parsed.size()), 1);
}

// TestRealWorldShapes() - 真实 Fedora 包会出现的依赖形态。
static void TestRealWorldShapes()
{
	std::printf("\n[6] 真实形态\n");

	// 这些名字和版本取自实测的 rpm 4.18 输出。
	std::vector<Dependency> Items;
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "glibc", ComparisonOperator::GreaterEqual, "2.39"));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "/bin/sh", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Manual, "",
	                         "coreutils", ComparisonOperator::Any, ""));
	Items.push_back(MakeItem(DependencyKind::Requires, DependencySource::Rpmlib, "",
	                         "rpmlib(CompressedFileNames)", ComparisonOperator::LessEqual,
	                         "3.0.4-1"));
	Items.push_back(MakeItem(DependencyKind::Provides, DependencySource::Manual, "",
	                         "okra-system-base", ComparisonOperator::Equal, "7.2-2"));
	Items.push_back(MakeItem(DependencyKind::Provides, DependencySource::Manual, "",
	                         "bash", ComparisonOperator::Any, ""));

	const std::string Text = SerializeDependencies(Items);
	std::vector<Dependency> Parsed;
	ExpectInt("解析返回 0", ParseDependencies(Text, Parsed), 0);
	ExpectInt("条数一致", static_cast<int>(Parsed.size()), 6);

	int Internal = 0;
	for (const Dependency& Item : Parsed) {
		if (DependencyIsInternal(Item)) {
			Internal++;
		}
	}
	ExpectInt("rpmlib 那条被认出是内部依赖", Internal, 1);

	int NoVersion = 0;
	for (const Dependency& Item : Parsed) {
		if (Item.Kind == DependencyKind::Provides && Item.Version.empty()) {
			NoVersion++;
		}
	}
	ExpectInt("无版本能力 1 条", NoVersion, 1);
}

// TestRandomRoundTrip() - 随机往返测试。
//
// 生成大量随机的依赖列表（字段里塞各种特殊字符），序列化再解析，逐条比对。
// 固定种子，可复现。
static void TestRandomRoundTrip()
{
	std::printf("\n[7] 随机往返\n");

	// 线性同余，固定种子。
	unsigned long long Seed = 20260927ULL;
	auto NextRandom = [&Seed](unsigned long long Bound) -> unsigned long long {
		Seed = Seed * 1103515245ULL + 12345ULL;
		return (Seed / 65536ULL) % Bound;
	};

	// 字符集包含所有需要转义的字符，以及普通字符。
	const char* Alphabet = "abzAZ09.+-_:~^/()\\\t\r";

	auto RandomField = [&NextRandom, &Alphabet]() -> std::string {
		const size_t Length = NextRandom(12);
		std::string Result;
		for (size_t Index = 0; Index < Length; Index++) {
			Result.push_back(Alphabet[NextRandom(20)]);
		}
		return Result;
	};

	const DependencyKind AllKinds[] = {
		DependencyKind::Provides,    DependencyKind::Requires,
		DependencyKind::Conflicts,   DependencyKind::Obsoletes,
		DependencyKind::Recommends,  DependencyKind::Suggests,
		DependencyKind::Supplements, DependencyKind::Enhances,
	};
	const DependencySource AllSources[] = {
		DependencySource::Unknown, DependencySource::Manual,
		DependencySource::Automatic, DependencySource::Rpmlib,
	};
	const ComparisonOperator AllOperators[] = {
		ComparisonOperator::Any,          ComparisonOperator::Less,
		ComparisonOperator::LessEqual,    ComparisonOperator::Equal,
		ComparisonOperator::GreaterEqual, ComparisonOperator::Greater,
	};

	const size_t Rounds = 20000;
	int Bad = 0;
	std::string FirstBad;

	for (size_t Round = 0; Round < Rounds; Round++) {
		const size_t Count = NextRandom(6);

		std::vector<Dependency> Items;
		for (size_t Index = 0; Index < Count; Index++) {
			Dependency Item;
			Item.Kind = AllKinds[NextRandom(8)];
			Item.Source = AllSources[NextRandom(4)];
			Item.Namespace = RandomField();
			Item.Name = RandomField();
			Item.Operator = AllOperators[NextRandom(6)];
			Item.Version = RandomField();
			Items.push_back(Item);
		}

		const std::string Text = SerializeDependencies(Items);

		std::vector<Dependency> Parsed;
		const int Result = ParseDependencies(Text, Parsed);
		if (Result != 0) {
			Bad++;
			if (FirstBad.empty()) {
				FirstBad = "解析返回 " + std::to_string(Result);
			}
			continue;
		}
		if (Parsed.size() != Items.size()) {
			Bad++;
			if (FirstBad.empty()) {
				FirstBad = "条数 " + std::to_string(Parsed.size()) + " 应为 " +
				           std::to_string(Items.size());
			}
			continue;
		}

		for (size_t Index = 0; Index < Items.size(); Index++) {
			if (!SameItem(Parsed[Index], Items[Index])) {
				Bad++;
				if (FirstBad.empty()) {
					FirstBad = "第 " + std::to_string(Index) + " 条内容不同";
				}
				break;
			}
		}
	}

	Total++;
	if (Bad != 0) {
		Failed++;
		std::printf("  FAIL %zu 轮里有 %d 轮不一致   首个：%s\n", Rounds, Bad, FirstBad.c_str());
	} else {
		std::printf("  ok   %zu 轮随机往返全部一致\n", Rounds);
	}
}

int main()
{
	std::printf("dependency_serialization 测试\n");

	TestEmptyIsEmptyString();
	TestRoundTrip();
	TestEscaping();
	TestEmbeddedInLargerText();
	TestBadInput();
	TestRealWorldShapes();
	TestRandomRoundTrip();

	std::printf("\n========================================\n");
	std::printf("共 %d 条，失败 %d 条\n", Total, Failed);
	std::printf("========================================\n");
	return Failed == 0 ? 0 : 1;
}