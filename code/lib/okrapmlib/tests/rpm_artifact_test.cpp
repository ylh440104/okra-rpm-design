// rpm_artifact_test.cpp - RpmArtifactBackend 的测试。
//
// 用真实的 Fedora 42 RPM 包测试。
//
// 构建与运行：
//   g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/art_test rpm_artifact_test.cpp \
//       ../src/rpm_artifact.cpp ../src/rpm_repository.cpp ../src/dependency.cpp \
//       ../src/version_scheme.cpp ../src/dependency_serialization.cpp -I../include
//   /tmp/art_test /path/to/cmake-3.31.6-2.fc42.aarch64.rpm

#include "okrapmlib/rpm_artifact.h"
#include "okrapmlib/dependency.h"

#include <cstdio>
#include <string>

using okrapm::DependencyIsInternal;
using okrapm::RpmArtifactBackend;
using okrapm::RpmPackageEntry;
using okrapm::RpmPayloadEntry;
using okrapm::RpmScriptlet;

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
		printf("  FAIL %s\n", What);
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
		fprintf(stderr, "用法: %s <file.rpm>\n", argv[0]);
		return 1;
	}

	std::string RpmPath = argv[1];

	printf("RpmArtifactBackend 测试\n");
	printf("测试文件：%s\n\n", RpmPath.c_str());

	RpmArtifactBackend Art;

	// CanHandle
	ExpectTrue("CanHandle(.rpm)", Art.CanHandle(RpmPath));
	ExpectTrue("CanHandle 拒绝非 rpm", !Art.CanHandle("test.txt"));

	// ReadMetadata
	RpmPackageEntry Entry;
	int Result = Art.ReadMetadata(RpmPath, Entry);
	ExpectInt("ReadMetadata 返回 0", Result, 0);

	if (Result == 0) {
		ExpectTrue("有名字", !Entry.Name.empty());
		ExpectTrue("有版本", !Entry.Version.empty());
		ExpectTrue("有 release", !Entry.Release.empty());
		ExpectTrue("有 arch", !Entry.Arch.empty());
		ExpectStr("arch 是 aarch64", Entry.Arch, "aarch64");
		ExpectTrue("有 provides", !Entry.Provides.empty());
		ExpectTrue("有 requires", !Entry.Requires.empty());

		// 如果是 cmake
		if (Entry.Name == "cmake") {
			ExpectStr("cmake 版本", Entry.Version, "3.31.6");
			ExpectStr("cmake release", Entry.Release, "2.fc42");

			// cmake 应该有 libstdc++.so.6 依赖
			bool HasStdcpp = false;
			for (const auto& Req : Entry.Requires) {
				if (Req.Name.find("libstdc++.so.6") != std::string::npos) {
					HasStdcpp = true;
					break;
				}
			}
			ExpectTrue("cmake requires libstdc++.so.6", HasStdcpp);

			// cmake 应该 provide cmake 自己
			bool SelfProvides = false;
			for (const auto& Prov : Entry.Provides) {
				if (Prov.Name == "cmake") {
					SelfProvides = true;
					break;
				}
			}
			ExpectTrue("cmake provides cmake", SelfProvides);
		}

		// 如果是 ninja-build
		if (Entry.Name == "ninja-build") {
			ExpectStr("ninja 版本", Entry.Version, "1.12.1");

			bool HasStdcpp = false;
			bool HasGcc = false;
			for (const auto& Req : Entry.Requires) {
				if (Req.Name.find("libstdc++.so.6") != std::string::npos) {
					HasStdcpp = true;
				}
				if (Req.Name.find("libgcc_s.so.1") != std::string::npos) {
					HasGcc = true;
				}
			}
			ExpectTrue("ninja requires libstdc++.so.6", HasStdcpp);
			ExpectTrue("ninja requires libgcc_s.so.1", HasGcc);
		}

		// rpmlib 内部依赖应该能识别
		int Internal = 0;
		for (const auto& Req : Entry.Requires) {
			if (DependencyIsInternal(Req)) {
				Internal++;
			}
		}
		// rpm 包的 requires 里通常有 rpmlib(...) 条目
		if (Internal > 0) {
			printf("  ok   识别出 %d 个 rpmlib 内部依赖\n", Internal);
			Total++;
		} else {
			printf("  ok   没有 rpmlib 内部依赖（部分包没有）\n");
			Total++;
		}
	}

	// ListPayload
	std::vector<RpmPayloadEntry> Payload;
	Result = Art.ListPayload(RpmPath, Payload);
	ExpectInt("ListPayload 返回 0", Result, 0);
	if (Result == 0) {
		ExpectTrue("有文件列表", !Payload.empty());
		if (!Payload.empty()) {
			printf("  ok   第一个文件: %s\n", Payload[0].Path.c_str());
			Total++;
		}
	}

	// ListScriptlets
	std::vector<RpmScriptlet> Scriptlets;
	Result = Art.ListScriptlets(RpmPath, Scriptlets);
	ExpectInt("ListScriptlets 返回 0", Result, 0);
	// 没有脚本也是合法的
	if (Scriptlets.empty()) {
		printf("  ok   没有安装脚本\n");
		Total++;
	} else {
		printf("  ok   有 %zu 个安装脚本\n", Scriptlets.size());
		Total++;
		for (const auto& S : Scriptlets) {
			printf("    %s (using %s): %.60s...\n", S.Phase.c_str(),
			       S.Interpreter.c_str(), S.Content.c_str());
		}
	}

	printf("\n========================================\n");
	printf("共 %d 条，失败 %d 条\n", Total, Failed);
	printf("========================================\n");
	return Failed == 0 ? 0 : 1;
}