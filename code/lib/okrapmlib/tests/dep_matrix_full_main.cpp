// dep_matrix_full_main.cpp - 依赖匹配的完整矩阵差分测试。
//
// 和 tests/probe-full-matrix.sh 生成的 /tmp/dep-full-matrix.tsv 逐条对比。
//
// 用法：
//   bash tests/probe-full-matrix.sh                    # 生成 rpm 侧答案
//   g++ -std=c++17 -O2 -o /tmp/fm tests/dep_matrix_full_main.cpp \
//       src/dependency.cpp src/version_scheme.cpp -Iinclude
//   /tmp/fm > /tmp/mine.txt
//   diff <(cut -f4 /tmp/dep-full-matrix.tsv) /tmp/mine.txt
//
// 两个表的顺序必须完全一致：9 个提供者外层，14 个需求者内层。

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

// 9 个提供者的版本。空串表示无版本。顺序与 shell 脚本一致。
static const char* ProvidesVersions[] = {
	"1.0",      // P0
	"1.0-1",    // P1
	"1.0-2",    // P2
	"1.0-",     // P3
	"1",        // P4
	"1:1.0",    // P5
	"1:1.0-1",  // P6
	"2.0",      // P7
	"",         // P8
};

// 14 个需求者的比较符与版本。顺序与 shell 脚本一致。
struct RequiresSpec
{
	const char* Operator;
	const char* Version;
};

static const RequiresSpec RequiresTable[] = {
	{ "=", "1.0" },     // R0
	{ "=", "1.0-1" },   // R1
	{ "=", "1.0-2" },   // R2
	{ "=", "1.0-" },    // R3
	{ "=", "1" },       // R4
	{ "=", "1:1.0" },   // R5
	{ ">=", "1.0" },    // R6
	{ ">", "1.0" },     // R7
	{ "<", "1.0" },     // R8
	{ "<=", "1.0" },    // R9
	{ ">=", "1.0-1" },  // R10
	{ ">", "1.0-1" },   // R11
	{ "<", "1.0-2" },   // R12
	{ "<=", "1.0-1" },  // R13
};

int main()
{
	RpmEvrScheme Scheme;

	const size_t ProvidesCount = sizeof(ProvidesVersions) / sizeof(ProvidesVersions[0]);
	const size_t RequiresCount = sizeof(RequiresTable) / sizeof(RequiresTable[0]);

	size_t Count = 0;
	size_t Errors = 0;

	for (size_t P = 0; P < ProvidesCount; P++) {
		Dependency Capability;
		Capability.Name = "virt";
		Capability.Kind = DependencyKind::Provides;
		Capability.Source = DependencySource::Manual;
		if (ProvidesVersions[P][0] != '\0') {
			Capability.Operator = ComparisonOperator::Equal;
			Capability.Version = ProvidesVersions[P];
		}

		for (size_t R = 0; R < RequiresCount; R++) {
			Dependency Requirement;
			Requirement.Name = "virt";
			Requirement.Kind = DependencyKind::Requires;
			Requirement.Source = DependencySource::Manual;
			if (ParseComparisonOperator(RequiresTable[R].Operator, Requirement.Operator) != 0) {
				std::fprintf(stderr, "R%zu 的比较符无法解析\n", R);
				Errors++;
			}
			Requirement.Version = RequiresTable[R].Version;

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