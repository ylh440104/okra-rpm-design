// rpm_repository_test.cpp - RpmRepositoryBackend 的测试。
//
// 用真实的 Fedora 42 primary.xml 测试。
// 期望值来自实测（analyze-correct.py 和 test_repo 的输出），不是手写。
//
// 构建与运行：
//   g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/repo_test rpm_repository_test.cpp \
//       ../src/rpm_repository.cpp ../src/dependency.cpp ../src/version_scheme.cpp \
//       ../src/dependency_serialization.cpp -I../include
//   /tmp/repo_test /path/to/primary.xml

#include "okrapmlib/rpm_repository.h"
#include "okrapmlib/dependency.h"

#include <cstdio>
#include <string>

using okrapm::DependencyIsInternal;
using okrapm::DependencyKind;
using okrapm::ComparisonOperatorText;
using okrapm::RpmPackageEntry;
using okrapm::RpmRepositoryBackend;

static int Total = 0;
static int Failed = 0;

static void ExpectInt(const char* What, long long Actual, long long Expected)
{
	Total++;
	if (Actual != Expected) {
		Failed++;
		printf("  FAIL %s   期望 %lld 实际 %lld\n", What, Expected, Actual);
		return;
	}
	printf("  ok   %s = %lld\n", What, Actual);
}

static void ExpectTrue(const char* What, bool Actual)
{
	Total++;
	if (!Actual) {
		Failed++;
		printf("  FAIL %s   期望 true\n", What);
		return;
	}
	printf("  ok   %s\n", What);
}

static void ExpectStr(const char* What, const std::string& Actual, const std::string& Expected)
{
	Total++;
	if (Actual != Expected) {
		Failed++;
		printf("  FAIL %s   期望 \"%s\" 实际 \"%s\"\n", What, Expected.c_str(),
		       Actual.c_str());
		return;
	}
	printf("  ok   %s = \"%s\"\n", What, Actual.c_str());
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "用法: %s <primary.xml>\n", argv[0]);
		return 1;
	}

	printf("RpmRepositoryBackend 测试\n");
	printf("数据来源：真实 Fedora 42 primary.xml\n");

	RpmRepositoryBackend Repo;
	Repo.SetNamespace("fedora");

	int Result = Repo.LoadPrimaryFromFile(argv[1]);
	ExpectInt("LoadPrimaryFromFile 返回 0", Result, 0);

	// 基本统计
	ExpectInt("包总数 67343", (long long)Repo.PackageCount(), 67343);

	// cmake
	const RpmPackageEntry* Cmake = Repo.Find("cmake");
	ExpectTrue("找到 cmake", Cmake != nullptr);
	if (Cmake) {
		ExpectStr("cmake 版本", Cmake->Version, "3.31.6");
		ExpectStr("cmake release", Cmake->Release, "2.fc42");
		ExpectStr("cmake 架构", Cmake->Arch, "aarch64");
		ExpectTrue("cmake 有 location",
		           !Cmake->Location.empty());
		ExpectTrue("cmake 有 provides",
		           !Cmake->Provides.empty());
		ExpectTrue("cmake 有 requires",
		           !Cmake->Requires.empty());
	}

	// libstdc++ 提供 libstdc++.so.6
	auto StdcppProviders = Repo.WhatProvides("libstdc++.so.6()(64bit)");
	ExpectTrue("WhatProvides(libstdc++.so.6) 非空", !StdcppProviders.empty());
	if (!StdcppProviders.empty()) {
		ExpectStr("提供者是 libstdc++", StdcppProviders[0]->Name,
		          "libstdc++");
	}

	// ninja-build 只依赖 libstdc++/libgcc_s/libc
	const RpmPackageEntry* Ninja = Repo.Find("ninja-build");
	ExpectTrue("找到 ninja-build", Ninja != nullptr);
	if (Ninja) {
		ExpectStr("ninja 版本", Ninja->Version, "1.12.1");

		// ninja 的 requires 里应该有 libstdc++.so.6
		bool HasStdcpp = false;
		for (const auto& Req : Ninja->Requires) {
			if (Req.Name.find("libstdc++.so.6") != std::string::npos) {
				HasStdcpp = true;
				break;
			}
		}
		ExpectTrue("ninja requires libstdc++.so.6", HasStdcpp);

		// ninja 的 requires 里应该有 libgcc_s.so.1
		bool HasGcc = false;
		for (const auto& Req : Ninja->Requires) {
			if (Req.Name.find("libgcc_s.so.1") != std::string::npos) {
				HasGcc = true;
				break;
			}
		}
		ExpectTrue("ninja requires libgcc_s.so.1", HasGcc);
	}

	// 3proxy 只依赖 libc（实测确认）
	const RpmPackageEntry* Proxy = Repo.Find("3proxy");
	ExpectTrue("找到 3proxy", Proxy != nullptr);
	if (Proxy) {
		bool OnlyGlibc = true;
		for (const auto& Req : Proxy->Requires) {
			// 跳过文件依赖（以 / 开头）
			if (!Req.Name.empty() && Req.Name[0] == '/') {
				continue;
			}
			// 跳过虚拟能力（含括号但不是 soname）
			if (Req.Name.find("(") != std::string::npos &&
			    Req.Name.find(".so") == std::string::npos) {
				continue;
			}
			// 跳过 libc 家族
			if (Req.Name.find("libc.so.6") != std::string::npos ||
			    Req.Name.find("ld-linux") != std::string::npos) {
				continue;
			}
			// 跳过纯包名依赖（无 .so）
			if (Req.Name.find(".so") == std::string::npos) {
				continue;
			}
			// 到这里的都是 .so 库依赖
			OnlyGlibc = false;
			break;
		}
		ExpectTrue("3proxy 只依赖 glibc 家族", OnlyGlibc);
	}

	// Search
	auto Results = Repo.Search("cmake");
	ExpectTrue("Search(cmake) 非空", !Results.empty());
	bool FoundCmake = false;
	for (auto* P : Results) {
		if (P->Name == "cmake") {
			FoundCmake = true;
			break;
		}
	}
	ExpectTrue("Search 结果包含 cmake", FoundCmake);

	// FindAll（多版本）
	auto AllCmake = Repo.FindAll("cmake");
	ExpectTrue("FindAll(cmake) 至少 1 个", !AllCmake.empty());

	// 统计
	const auto& All = Repo.ListObjects();
	long long TotalRequires = 0;
	long long TotalProvides = 0;
	for (const auto& Pkg : All) {
		TotalRequires += Pkg.Requires.size();
		TotalProvides += Pkg.Provides.size();
	}
	ExpectTrue("总 requires > 500000", TotalRequires > 500000);
	ExpectTrue("总 provides > 400000", TotalProvides > 400000);

	// 命名空间
	ExpectStr("命名空间", Repo.GetNamespace(), "fedora");

	printf("\n========================================\n");
	printf("共 %d 条，失败 %d 条\n", Total, Failed);
	printf("========================================\n");
	return Failed == 0 ? 0 : 1;
}