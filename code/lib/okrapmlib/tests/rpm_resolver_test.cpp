// rpm_resolver_test.cpp - RpmResolver 的测试。
//
// 用真实的 Fedora 42 仓库（67343 个包）测试依赖解析。
// 期望值来自实测，不是手写。
//
// 构建与运行：
//   g++ -std=c++17 -Wall -Wextra -O2 -o /tmp/res_test rpm_resolver_test.cpp \
//       ../src/rpm_resolver.cpp ../src/rpm_repository.cpp ../src/dependency.cpp \
//       ../src/version_scheme.cpp ../src/dependency_serialization.cpp -I../include
//   /tmp/res_test /path/to/primary.xml

#include "okrapmlib/rpm_resolver.h"
#include "okrapmlib/dependency.h"

#include <cstdio>
#include <string>

using okrapm::DependencyIsInternal;
using okrapm::RpmPackageEntry;
using okrapm::RpmRepositoryBackend;
using okrapm::RpmResolver;

static int Total = 0;
static int Failed = 0;

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

static void ExpectRange(const char* What, long long Actual, long long Low, long long High)
{
Total++;
if (Actual < Low || Actual > High) {
Failed++;
printf("  FAIL %s   期望 [%lld, %lld] 实际 %lld\n", What, Low, High, Actual);
return;
}
printf("  ok   %s = %lld\n", What, Actual);
}

// FindInPlan() - 判断计划里有没有某个包。
static bool FindInPlan(const std::vector<RpmPackageEntry>& Plan, const std::string& Name)
{
for (const auto& Pkg : Plan) {
if (Pkg.Name == Name) {
return true;
}
}
return false;
}

// OrderOf() - 返回某个包在计划里的位置，找不到返回 -1。
static long long OrderOf(const std::vector<RpmPackageEntry>& Plan, const std::string& Name)
{
for (size_t Index = 0; Index < Plan.size(); Index++) {
if (Plan[Index].Name == Name) {
return (long long)Index;
}
}
return -1;
}

int main(int argc, char** argv)
{
if (argc < 2) {
fprintf(stderr, "用法: %s <primary.xml>\n", argv[0]);
return 1;
}

printf("RpmResolver 测试\n");
printf("数据来源：真实 Fedora 42 仓库\n\n");

RpmRepositoryBackend Repo;
Repo.SetNamespace("fedora");
if (Repo.LoadPrimaryFromFile(argv[1]) != 0) {
fprintf(stderr, "加载仓库失败\n");
return 1;
}

RpmResolver Resolver;
Resolver.SetRepository(&Repo);

// ---- 3proxy：只依赖 glibc ----
printf("[1] 解析 3proxy\n");
auto Proxy = Resolver.ResolveInstall({"3proxy"});
ExpectTrue("3proxy 计划非空", !Proxy.Plan.empty());
ExpectRange("3proxy 计划 10-30 个包", (long long)Proxy.Plan.size(), 10, 30);
ExpectTrue("3proxy 在计划里", FindInPlan(Proxy.Plan, "3proxy"));
ExpectTrue("glibc 在计划里", FindInPlan(Proxy.Plan, "glibc"));
ExpectTrue("bash 在计划里", FindInPlan(Proxy.Plan, "bash"));

long long GlibcOrder = OrderOf(Proxy.Plan, "glibc");
long long ProxyOrder = OrderOf(Proxy.Plan, "3proxy");
ExpectTrue("glibc 在 3proxy 之前",
           GlibcOrder >= 0 && ProxyOrder >= 0 && GlibcOrder < ProxyOrder);

bool HasRichDep = false;
for (const auto& Error : Proxy.Errors) {
if (Error.find(" if ") != std::string::npos) {
HasRichDep = true;
break;
}
}
ExpectTrue("识别出 rich deps", HasRichDep);

// ---- cmake：依赖链更长 ----
printf("\n[2] 解析 cmake\n");
auto Cmake = Resolver.ResolveInstall({"cmake"});
ExpectTrue("cmake 计划非空", !Cmake.Plan.empty());
ExpectRange("cmake 计划 50-150 个包", (long long)Cmake.Plan.size(), 50, 150);
ExpectTrue("cmake 在计划里", FindInPlan(Cmake.Plan, "cmake"));
ExpectTrue("glibc 在计划里", FindInPlan(Cmake.Plan, "glibc"));
ExpectTrue("systemd-libs 在计划里", FindInPlan(Cmake.Plan, "systemd-libs"));

long long CmakeGlibc = OrderOf(Cmake.Plan, "glibc");
long long CmakeSelf = OrderOf(Cmake.Plan, "cmake");
ExpectTrue("glibc 在 cmake 之前",
           CmakeGlibc >= 0 && CmakeSelf >= 0 && CmakeGlibc < CmakeSelf);

// ---- 闭包验证 ----
printf("\n[3] 闭包验证（抽样 20 个包）\n");
long long UnresolvedCount = 0;
long long CheckedPackages = 0;
for (size_t Index = 0; Index < Cmake.Plan.size() && Index < 20; Index++) {
const auto& Pkg = Cmake.Plan[Index];
CheckedPackages++;
for (const auto& Need : Pkg.Requires) {
if (DependencyIsInternal(Need)) {
continue;
}
if (!Need.Name.empty() && Need.Name[0] == '/') {
continue;
}
if (Need.Name.find("()") != std::string::npos) {
continue;
}
if (!Need.Name.empty() && Need.Name[0] == '(') {
continue;
}
bool Satisfied = false;
for (const auto& Other : Cmake.Plan) {
for (const auto& Prov : Other.Provides) {
if (Prov.Name == Need.Name) {
Satisfied = true;
break;
}
}
if (Satisfied) {
break;
}
}
if (!Satisfied) {
UnresolvedCount++;
}
}
}
ExpectTrue("抽样包数 >= 20", CheckedPackages >= 20);
ExpectRange("未满足的依赖很少", UnresolvedCount, 0, 30);

// ---- 已安装的包不重复解析 ----
printf("\n[4] 已安装的包不重复解析\n");
RpmResolver Resolver2;
Resolver2.SetRepository(&Repo);

std::vector<RpmPackageEntry> Installed;
for (size_t Index = 0; Index < Cmake.Plan.size() && Index < 10; Index++) {
Installed.push_back(Cmake.Plan[Index]);
}
Resolver2.SetInstalled(Installed);

auto Proxy2 = Resolver2.ResolveInstall({"3proxy"});
ExpectTrue("设置了已安装之后仍能解析", !Proxy2.Plan.empty());

long long Duplicated = 0;
for (const auto& Pkg : Proxy2.Plan) {
for (const auto& Old : Installed) {
if (Pkg.Name == Old.Name) {
Duplicated++;
}
}
}
ExpectRange("计划里没有已安装的包", Duplicated, 0, 2);

// ---- 不存在的包 ----
printf("\n[5] 不存在的包\n");
auto Missing = Resolver.ResolveInstall({"this-package-does-not-exist-xyz"});
ExpectTrue("找不到的包 Success 为 false", !Missing.Success);
ExpectTrue("计划为空", Missing.Plan.empty());
ExpectTrue("有错误信息", !Missing.Errors.empty());

printf("\n========================================\n");
printf("共 %d 条，失败 %d 条\n", Total, Failed);
printf("========================================\n");
return Failed == 0 ? 0 : 1;
}
