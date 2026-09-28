// fuzz_main.cpp - 差分模糊测试的 C++ 侧。
//
// 读 /tmp/fuzz-pairs.txt（每行两个操作数，制表符分开），把 RpmEvrCmp 的结果
// 逐行写到标准输出，格式和 /tmp/fuzz-expected.txt 一致。然后在外层用 diff 比。
//
// 期望值由 tests/fuzz-cases.lua 用系统 rpm 生成。这个程序不做任何判断，
// 只负责输出自己的答案，让 rpm 当裁判。
//
// 构建与运行：
//   g++ -std=c++17 -O2 -o /tmp/fuzz_main fuzz_main.cpp ../src/version_scheme.cpp -I../include
//   /tmp/fuzz_main > /tmp/fuzz-actual.txt
//   diff /tmp/fuzz-expected.txt /tmp/fuzz-actual.txt

#include "okrapmlib/version_scheme.h"

#include <cstdio>
#include <fstream>
#include <string>

int main(int ArgumentCount, char** Arguments)
{
	std::string InputPath = "/tmp/fuzz-pairs.txt";
	if (ArgumentCount > 1) {
		InputPath = Arguments[1];
	}

	std::ifstream Input(InputPath);
	if (!Input) {
		std::fprintf(stderr, "打不开 %s\n", InputPath.c_str());
		return 2;
	}

	std::string Line;
	size_t Count = 0;
	while (std::getline(Input, Line)) {
		if (Line.empty()) {
			continue;
		}

		const size_t Tab = Line.find('\t');
		if (Tab == std::string::npos) {
			std::fprintf(stderr, "第 %zu 行没有制表符：%s\n", Count + 1, Line.c_str());
			return 2;
		}

		const std::string Left = Line.substr(0, Tab);
		const std::string Right = Line.substr(Tab + 1);

		const int Result = okrapm::RpmEvrCmp(Left, Right);
		char Verdict = '=';
		if (Result < 0) {
			Verdict = '<';
		} else if (Result > 0) {
			Verdict = '>';
		}

		std::printf("%c\n", Verdict);
		Count++;
	}

	std::fprintf(stderr, "共处理 %zu 对\n", Count);
	return 0;
}