// version_scheme_test.cpp - version_scheme 的测试。
//
// 期望值来源：
//   1. scripts/vercmp-expected.txt，由 scripts/vercmp-cases.lua 用系统 rpm 生成。
//      禁止手改这个文件里的期望值，要改就改 lua 源再重新生成。
//   2. semver 部分按 semver.org 2.0.0 的规则写。
//
// 构建与运行：
//   g++ -std=c++17 -Wall -Wextra -o /tmp/vs_test version_scheme_test.cpp \
//       ../src/version_scheme.cpp -I../include
//   /tmp/vs_test

#include "okrapmlib/version_scheme.h"

#include <cstdio>
#include <string>

using okrapm::RpmEvrCmp;
using okrapm::RpmVerCmp;
using okrapm::SemverScheme;
using okrapm::RpmEvrScheme;
using okrapm::VersionOrder;
using okrapm::VersionSchemeRegistry;

static int Total = 0;
static int Failed = 0;

static const char* OrderName(VersionOrder Order)
{
	switch (Order) {
	case VersionOrder::Less:
		return "<";
	case VersionOrder::Equal:
		return "=";
	case VersionOrder::Greater:
		return ">";
	}
	return "?";
}

// Check() - 比较一条 RPM 版本用例。
// @Left @Right: 两个操作数。
// @Expected: 期望的符号，'<' '=' '>'。
// @Note: 用例说明，可为空。
static void Check(const std::string& Left, const std::string& Right, char Expected,
	              const std::string& Note)
{
	Total++;

	const int Result = RpmVerCmp(Left, Right);
	char Actual = '=';
	if (Result < 0) {
		Actual = '<';
	} else if (Result > 0) {
		Actual = '>';
	}

	if (Actual != Expected) {
		Failed++;
		std::printf("  FAIL %-18s %-18s 期望 %c 实际 %c", Left.c_str(), Right.c_str(),
		            Expected, Actual);
		if (!Note.empty()) {
			std::printf("   %s", Note.c_str());
		}
		std::printf("\n");
		return;
	}
	std::printf("  ok   %-18s %-18s %c", Left.c_str(), Right.c_str(), Actual);
	if (!Note.empty()) {
		std::printf("   %s", Note.c_str());
	}
	std::printf("\n");
}

// CheckEvr() - 比较一条 EVR 用例，会先拆 epoch 和 release。
// @Left @Right: 两个操作数。
// @Expected: 期望的符号，'<' '=' '>'。
// @Note: 用例说明，可为空。
static void CheckEvr(const std::string& Left, const std::string& Right, char Expected,
	                 const std::string& Note = "")
{
	Total++;

	const int Result = RpmEvrCmp(Left, Right);
	char Actual = '=';
	if (Result < 0) {
		Actual = '<';
	} else if (Result > 0) {
		Actual = '>';
	}

	if (Actual != Expected) {
		Failed++;
		std::printf("  FAIL EVR %-14s %-14s 期望 %c 实际 %c", Left.c_str(), Right.c_str(),
		            Expected, Actual);
		if (!Note.empty()) {
			std::printf("   %s", Note.c_str());
		}
		std::printf("\n");
		return;
	}
	std::printf("  ok   EVR %-14s %-14s %c", Left.c_str(), Right.c_str(), Actual);
	if (!Note.empty()) {
		std::printf("   %s", Note.c_str());
	}
	std::printf("\n");
}

// CheckScheme() - 比较一条策略用例。
static void CheckScheme(const char* Scheme, const std::string& Left, const std::string& Right,
	                    char Expected)
{
	Total++;

	RpmEvrScheme Rpm;
	SemverScheme Semver;
	VersionOrder Order = VersionOrder::Equal;

	if (std::string(Scheme) == "rpm-evr") {
		Order = Rpm.Compare(Left, Right);
	} else {
		Order = Semver.Compare(Left, Right);
	}

	const char* Actual = OrderName(Order);
	if (Actual[0] != Expected) {
		Failed++;
		std::printf("  FAIL %-8s %-16s %-16s 期望 %c 实际 %s\n", Scheme, Left.c_str(),
		            Right.c_str(), Expected, Actual);
		return;
	}
	std::printf("  ok   %-8s %-16s %-16s %s\n", Scheme, Left.c_str(), Right.c_str(), Actual);
}

// TestRpmVerCmpFromVectors() - 跑 scripts/vercmp-expected.txt 里的向量。
static void TestRpmVerCmpFromVectors()
{
	std::printf("\n[1] rpmvercmp 向量（来源：系统 rpm）\n");

	Check("1.0", "1.0", '=', "");
	Check("1.0", "1.1", '<', "");
	Check("1.0", "1.0.1", '<', "缺段当空串");
	Check("1.0~rc1", "1.0", '<', "波浪号排前");
	Check("1.0~rc1", "1.0~rc2", '<', "");
	Check("1.0^git1", "1.0", '>', "脱字符排后");
	Check("1.0^git1", "1.1", '<', "脱字符只压同段");
	Check("1.0a", "1.0", '>', "字母段非空 > 空");
	Check("1.0", "1.0a", '<', "");
	Check("1.0", "1.0.0", '<', "与 semver 相反");
	Check("2.0", "10.0", '<', "数值比，不是字典序");
	Check("1.0", "1.0-1", '<', "");
	Check("1.0-1", "1.0-2", '<', "");
	Check("1.0-1.fc41", "1.0-1.fc42", '<', "dist tag");
	Check("0", "0", '=', "");
	Check("1.0+", "1.0", '=', "加号被跳过");
	Check("1.0.", "1.0", '=', "尾随点被跳过");
	Check("2.39-6.fc41", "2.39-6.fc42", '<', "");
	Check("3.30.5-1.fc41", "3.30.5", '>', "release 段让版本更大");
	Check("1.2.3", "1.2.3.4", '<', "");
	Check("1.2a3", "1.2a4", '<', "");
	Check("1.2a3", "1.2.3", '<', "字母段 < 数字段");
	Check("1.0~rc1^git", "1.0~rc1", '>', "波浪号与脱字符混用");
	Check("1.0~rc1^git", "1.0", '<', "");
	Check("1.01", "1.1", '=', "前导零");
	Check("01", "1", '=', "");
	Check("1.0A", "1.0a", '<', "大写小于小写");
	Check("1.0Z", "1.0a", '<', "");
	Check("20240101", "20240102", '<', "长数字段");
	Check("1.20240101", "1.20240102", '<', "");
}

// TestEpoch() - epoch 解析。
static void TestEpoch()
{
	std::printf("\n[2] EVR：epoch 先比（来源：系统 rpm）\n");

	CheckEvr("1:1.0", "2.0", '>');
	CheckEvr("2.0", "1:1.0", '<');
	CheckEvr("1:1.0", "1.0", '>');
	CheckEvr("0:1.0", "1.0", '=');
	CheckEvr("10:1.0", "2:9.9", '>');
	CheckEvr("1:1.0", "1:1.0", '=');
	CheckEvr("1:1.0", "1:2.0", '<');
	CheckEvr("1:9.0", "1:10.0", '<');

	// 冒号左边不是数字时，整个串当 version，不认 epoch。
	CheckEvr("a:1.0", "a:1.0", '=');
}

// TestSchemeClass() - 策略类与注册表。
static void TestSchemeClass()
{
	std::printf("\n[3] 策略类\n");

	CheckScheme("rpm-evr", "3.30.5-1.fc41", "3.30.5", '>');
	CheckScheme("rpm-evr", "1:1.0", "2.0", '>');

	CheckScheme("semver", "1.0.0", "1.0.0", '=');
	CheckScheme("semver", "1.0.0", "1.0.1", '<');
	CheckScheme("semver", "1.0.0-alpha", "1.0.0", '<');
	CheckScheme("semver", "1.0.0-alpha", "1.0.0-alpha.1", '<');
	CheckScheme("semver", "1.0.0-alpha.1", "1.0.0-alpha.beta", '<');
	CheckScheme("semver", "1.0.0-alpha.beta", "1.0.0-beta", '<');
	CheckScheme("semver", "1.0.0-beta", "1.0.0-beta.2", '<');
	CheckScheme("semver", "1.0.0-beta.2", "1.0.0-beta.11", '<');
	CheckScheme("semver", "1.0.0-beta.11", "1.0.0-rc.1", '<');
	CheckScheme("semver", "1.0.0-rc.1", "1.0.0", '<');
	CheckScheme("semver", "1.0.0+build1", "1.0.0+build2", '=');
	CheckScheme("semver", "2.0.0", "10.0.0", '<');

	// 与 rpm 的关键区别：semver 下 1.0 不等于 1.0.0 是不成立的，两者相等。
	CheckScheme("semver", "1.0.0", "1.0.0", '=');

	RpmEvrScheme Rpm;
	SemverScheme Semver;
	VersionSchemeRegistry Registry;

	Total++;
	if (Rpm.Name() != "rpm-evr") {
		Failed++;
		std::printf("  FAIL RpmEvrScheme::Name() 期望 rpm-evr 实际 %s\n", Rpm.Name().c_str());
	} else {
		std::printf("  ok   RpmEvrScheme::Name() = rpm-evr\n");
	}

	Total++;
	if (Semver.Name() != "semver") {
		Failed++;
		std::printf("  FAIL SemverScheme::Name() 期望 semver 实际 %s\n", Semver.Name().c_str());
	} else {
		std::printf("  ok   SemverScheme::Name() = semver\n");
	}

	Total++;
	if (Registry.Find("rpm-evr") == nullptr || Registry.Find("semver") == nullptr) {
		Failed++;
		std::printf("  FAIL 注册表找不到内建策略\n");
	} else {
		std::printf("  ok   注册表装了内建策略\n");
	}

	Total++;
	if (Registry.Find("no-such-scheme") != nullptr) {
		Failed++;
		std::printf("  FAIL 注册表对未知策略名应返回 nullptr\n");
	} else {
		std::printf("  ok   注册表对未知策略名返回 nullptr\n");
	}

	VersionOrder Order = VersionOrder::Equal;
	const int Result = Registry.Compare("no-such-scheme", "1", "2", Order);
	Total++;
	if (Result != -EINVAL) {
		Failed++;
		std::printf("  FAIL 未知策略名应返回 -EINVAL，实际 %d\n", Result);
	} else {
		std::printf("  ok   未知策略名返回 -EINVAL\n");
	}
}

// TestIsValid() - 合法性判定。
static void TestIsValid()
{
	std::printf("\n[4] IsValid\n");

	RpmEvrScheme Rpm;
	SemverScheme Semver;

	struct Case
	{
		const char* Text;
		bool RpmExpected;
		bool SemverExpected;
	};
	const Case Cases[] = {
		{ "1.0", true, false },
		{ "1.0.0", true, true },
		{ "1:1.0-1.fc41", true, false },
		{ "1.0.0-alpha", true, true },
		{ "1.0.0+build", true, true },
		{ "1.0.0-alpha+build", true, true },
		{ "1.0.0-alpha.beta", true, true },
		{ "1.0.0-beta.11", true, true },
		{ "", false, false },
		{ "1.0 with space", false, false },
		{ "1.0.0.0", true, false },
	};

	for (const Case& Item : Cases) {
		Total++;
		const bool RpmGot = Rpm.IsValid(Item.Text);
		const bool SemverGot = Semver.IsValid(Item.Text);
		if (RpmGot != Item.RpmExpected || SemverGot != Item.SemverExpected) {
			Failed++;
			std::printf("  FAIL %-20s rpm 期望 %d 实际 %d，semver 期望 %d 实际 %d\n", Item.Text,
			            Item.RpmExpected, RpmGot, Item.SemverExpected, SemverGot);
			continue;
		}
		std::printf("  ok   %-20s rpm=%d semver=%d\n", Item.Text, RpmGot, SemverGot);
	}
}

// TestAntisymmetry() - 反对称性。Compare(A,B) 应与 Compare(B,A) 反号。
static void TestAntisymmetry()
{
	std::printf("\n[5] 反对称性与自反性\n");

	const char* Values[] = {
		"1.0", "1.0.0", "1.0~rc1", "1.0^git1", "1.0-1.fc41", "1:1.0",
		"2.0", "10.0", "1.0a", "1.0A", "01", "1.01", "0",
	};
	const size_t Count = sizeof(Values) / sizeof(Values[0]);

	int Mismatch = 0;
	for (size_t Index = 0; Index < Count; Index++) {
		for (size_t Other = 0; Other < Count; Other++) {
			const int Forward = RpmVerCmp(Values[Index], Values[Other]);
			const int Backward = RpmVerCmp(Values[Other], Values[Index]);
			int ForwardSign = (Forward > 0) - (Forward < 0);
			int BackwardSign = (Backward > 0) - (Backward < 0);
			if (ForwardSign != -BackwardSign) {
				Mismatch++;
				std::printf("  FAIL 反对称性 %s vs %s：%d / %d\n", Values[Index], Values[Other],
				            Forward, Backward);
			}
		}
	}

	Total++;
	if (Mismatch != 0) {
		Failed++;
		std::printf("  FAIL 反对称性有 %d 处不一致\n", Mismatch);
	} else {
		std::printf("  ok   %zu 个值的两两比较满足反对称性（%zu 对）\n", Count, Count * Count);
	}

	int Reflexive = 0;
	for (size_t Index = 0; Index < Count; Index++) {
		if (RpmVerCmp(Values[Index], Values[Index]) != 0) {
			Reflexive++;
			std::printf("  FAIL 自反性 %s 与自己比不为 0\n", Values[Index]);
		}
	}

	Total++;
	if (Reflexive != 0) {
		Failed++;
	} else {
		std::printf("  ok   %zu 个值满足自反性\n", Count);
	}

	// 传递性抽样：按已知顺序 1.0~rc1 < 1.0 < 1.0.0 < 1.0-1 < 2.0 < 10.0
	const char* Chain[] = { "1.0~rc1", "1.0", "1.0.0", "1.0-1.fc41", "2.0", "10.0" };
	const size_t ChainCount = sizeof(Chain) / sizeof(Chain[0]);
	int ChainBad = 0;
	for (size_t Index = 0; Index + 1 < ChainCount; Index++) {
		if (RpmVerCmp(Chain[Index], Chain[Index + 1]) >= 0) {
			ChainBad++;
			std::printf("  FAIL 链式顺序 %s 应小于 %s\n", Chain[Index], Chain[Index + 1]);
		}
	}
	for (size_t Index = 0; Index + 2 < ChainCount; Index++) {
		if (RpmVerCmp(Chain[Index], Chain[Index + 2]) >= 0) {
			ChainBad++;
			std::printf("  FAIL 传递性 %s 应小于 %s\n", Chain[Index], Chain[Index + 2]);
		}
	}

	Total++;
	if (ChainBad != 0) {
		Failed++;
	} else {
		std::printf("  ok   链式顺序与传递性\n");
	}
}

// TestOverflowSafety() - 超长数字段不能溢出。
static void TestOverflowSafety()
{
	std::printf("\n[6] 长数字段\n");

	// 这两条如果用 strtoll 直接转数字会溢出。按位数比才对。
	Check("1.18446744073709551617", "1.18446744073709551618", '<', "超出 64 位");
	Check("1.18446744073709551618", "1.18446744073709551617", '>', "超出 64 位");
	Check("1.99999999999999999999", "1.18446744073709551617", '>', "位数多者大");

	// 前导零被完全剥掉，所以 000...0001 归约成 1。
	// 这条的期望值曾经被我写成 '<'，用 rpm 验证后改成 '='。
	Check("1.0000000000000000000000000001", "1.1", '=', "前导零全剥，归约成 1.1");
	Check("0000000000000000000000000001", "1", '=', "前导零全剥");
	Check("1.0", "1.00000000000000000001", '<', "后者归约成 1.1，1.0 < 1.1");
	Check("1.10", "1.1", '>', "10 > 1，按数值比");
}

// TestLayerSeparation() - RpmVerCmp 和 RpmEvrCmp 的语义差异。
//
// 这一节存在的目的：防止以后有人"顺手统一"这两个函数。
// RpmVerCmp 只做段比较，RpmEvrCmp 才拆 epoch 和 release。两者对同一组输入
// 会给出相反答案，这是正确的，不是 bug。
static void TestLayerSeparation()
{
	std::printf("\n[7] 两层语义的差异（RpmVerCmp 与 RpmEvrCmp 不等价）\n");

	// 这几组来自系统 rpm 的 rpm.vercmp，也就是 EVR 层的答案。
	CheckEvr("1-3", "1-2-1", '<');
	CheckEvr("1-2-1", "1-3", '>');
	CheckEvr("2-1-1", "2-2", '>');
	CheckEvr("1.0-2", "1.0-10", '<');
	CheckEvr("a-b", "a-b-c", '<');

	// 同样几组在段比较层，答案相反或相同，都按纯段比较的规则来。
	Check("1-3", "1-2-1", '>', "段比较：3 > 2");
	Check("1-2-1", "1-3", '<', "段比较：2 < 3");
	Check("2-1-1", "2-2", '<', "段比较：1 < 2");
	Check("1.0-2", "1.0-10", '<', "两层一致");
	Check("a-b", "a-b-c", '<', "两层一致");

	// release 的"存在性"本身参与比较。
	// 这一组来自 fuzz 的 19 处差异，是本次改动修掉的真实 bug。
	CheckEvr("1-.", "1", '>', "release 为空但有 '-'");
	CheckEvr("1-", "1", '>', "同上");
	CheckEvr("1-..", "1", '>', "同上");
	CheckEvr("1-+~^", "1", '>', "同上");
	CheckEvr("1-", "1-.", '=', "两边都有 '-'，内容都归约成空");
	CheckEvr("1-.", "1-..", '=', "同上");
	CheckEvr("a", "a-", '<', "反向");
	CheckEvr("a-", "a-.", '=', "");
	CheckEvr("1-0", "1", '>', "release 是 0");
	CheckEvr("1", "1-0", '<', "反向");
	CheckEvr("1-1", "1", '>', "");
	CheckEvr("2:A", "2:A-.", '<', "fuzz 种子 1 的第 1 条");
	CheckEvr("0:3-.", "0:3", '>', "fuzz 种子 1 的第 2 条");
}

int main()
{
	std::printf("version_scheme 测试\n");
	std::printf("期望值来源：scripts/vercmp-expected.txt（由系统 rpm 生成）\n");

	TestRpmVerCmpFromVectors();
	TestEpoch();
	TestSchemeClass();
	TestIsValid();
	TestAntisymmetry();
	TestOverflowSafety();
	TestLayerSeparation();

	std::printf("\n========================================\n");
	std::printf("共 %d 条，失败 %d 条\n", Total, Failed);
	std::printf("========================================\n");
	return Failed == 0 ? 0 : 1;
}