// dep_matrix_main.cpp - 依赖满足性的差分测试。
//
// 和 rpm 的真实求解器逐条对比。rpm 侧的答案由 verification.md 里的脚本生成，
// 结果存在 /tmp/rpm-matrix.tsv。
//
// 用法：
//   /tmp/dep_matrix > /tmp/dep-matrix.txt
//   diff <(cut -f4 /tmp/rpm-matrix.tsv) /tmp/dep-matrix.txt
//
// 这个程序把 7 个提供者 × 10 个需求者 = 70 组全跑一遍，输出 满足 或 失败，
// 一行一个，顺序与 rpm 侧一致。

#include "okrapmlib/dependency.h"
#include "okrapmlib/version_scheme.h"

#include <cstdio>
#include <string>

using okrapm::ComparisonOperator;
using okrapm::Dependency;
using okrapm::DependencyKind;
using okrapm::DependencySatisfiedBy;
using okrapm::DependencySource;
using okrapm::ParseComparisonOperator;
using okrapm::RpmEvrScheme;

// ProvidesSpec - 提供者的描述。
struct ProvidesSpec
{
	const char* Name;
	const char* Version;   // 空串表示无版本
};

// RequiresSpec - 需求者的描述。
struct RequiresSpec
{
	const char* Name;
	const char* Operator;  // 空串表示无约束
	const char* Version;
};

// 7 个提供者，顺序与 rpm 侧一致。
static const ProvidesSpec ProvidesTable[] = {
	{ "virt", "" },        // P0
	{ "virt", "1.0" },     // P1
	{ "virt", "1.5" },     // P2
	{ "virt", "2.0" },     // P3
	{ "virt", "9.9" },     // P4
	{ "virt", "1.0-1" },   // P5
	{ "other", "1.0" },    // P6
};

// 10 个需求者，顺序与 rpm 侧一致。
static const RequiresSpec RequiresTable[] = {
	{ "virt", "", "" },         // R0
	{ "virt", ">=", "1.0" },    // R1
	{ "virt", ">=", "1.5" },    // R2
	{ "virt", ">=", "2.0" },    // R3
	{ "virt", ">", "1.5" },     // R4
	{ "virt", "<", "2.0" },     // R5
	{ "virt", "<=", "1.5" },    // R6
	{ "virt", "=", "1.5" },     // R7
	{ "virt", "=", "1.0-1" },   // R8
	{ "other", "", "" },        // R9
};

// MakeCapability() - 造一条 Provides。
static Dependency MakeCapability(const ProvidesSpec& Spec)
{
	Dependency Item;
	Item.Name = Spec.Name;
	Item.Kind = DependencyKind::Provides;
	Item.Source = DependencySource::Manual;
	if (Spec.Version[0] != '\0') {
		Item.Operator = ComparisonOperator::Equal;
		Item.Version = Spec.Version;
	}
	return Item;
}

// MakeRequirement() - 造一条 Requires。
static Dependency MakeRequirement(const RequiresSpec& Spec)
{
	Dependency Item;
	Item.Name = Spec.Name;
	Item.Kind = DependencyKind::Requires;
	Item.Source = DependencySource::Manual;
	if (ParseComparisonOperator(Spec.Operator, Item.Operator) != 0) {
		// 解析失败时保持 Any，下面的输出会暴露问题。
		Item.Operator = ComparisonOperator::Any;
	}
	Item.Version = Spec.Version;
	return Item;
}

int main()
{
	RpmEvrScheme Scheme;

	const size_t ProvidesCount = sizeof(ProvidesTable) / sizeof(ProvidesTable[0]);
	const size_t RequiresCount = sizeof(RequiresTable) / sizeof(RequiresTable[0]);

	size_t Count = 0;
	size_t Errors = 0;

	for (size_t P = 0; P < ProvidesCount; P++) {
		const Dependency Capability = MakeCapability(ProvidesTable[P]);

		for (size_t R = 0; R < RequiresCount; R++) {
			const Dependency Requirement = MakeRequirement(RequiresTable[R]);

			bool Satisfied = false;
			const int Result = DependencySatisfiedBy(Requirement, Capability, Scheme, Satisfied);

			if (Result != 0) {
				std::fprintf(stderr, "P%zu/R%zu 调用返回 %d\n", P, R, Result);
				Errors++;
				std::printf("失败\n");
			} else {
				std::printf("%s\n", Satisfied ? "满足" : "失败");
			}
			Count++;
		}
	}

	std::fprintf(stderr, "共 %zu 组，调用错误 %zu 组\n", Count, Errors);
	return Errors == 0 ? 0 : 1;
}