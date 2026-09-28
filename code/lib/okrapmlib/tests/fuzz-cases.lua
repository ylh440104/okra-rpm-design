#!/usr/bin/lua
--
-- fuzz-cases.lua - 生成随机版本对，并用系统 rpm 算出期望值。
--
-- 输出两个文件：
--   /tmp/fuzz-pairs.txt     每行两个操作数，制表符分开
--   /tmp/fuzz-expected.txt  每行一个符号，< / = / >
--
-- 两文件行数相同，行号一一对应。C++ 侧读 pairs 输出自己的结果，再和 expected
-- 做 diff。这样比手写用例覆盖面大得多，而且期望值永远来自 rpm 自己。
--
-- 用法：
--   rpm --eval "%{lua: dofile('fuzz-cases.lua')}"

-- 字母表。包含所有会影响 rpmvercmp 分支的字符：
--   数字、字母：段的两种类型
--   . - _ + :  ：分隔符，会被跳过
--   ~ ^        ：两个有特殊排序含义的字符
local Alphabet = {
	"0", "1", "2", "3", "7", "9",
	"a", "b", "z", "A", "Z",
	".", "-", "_", "+", ":", "~", "^",
}

local PairCount = tonumber(os.getenv("FUZZ_PAIRS") or "") or 4000

-- 简单的线性同余，固定种子保证可复现。不依赖 math.random 的实现细节。
--
-- 种子从环境变量 FUZZ_SEED 读。
-- 注意：不能用 rpm --eval "FuzzSeed=1; ..." 这种方式传全局变量。rpm 的 Lua
-- 沙箱不传递外部全局，那样写会读到 nil，结果每轮都跑同一个默认种子，
-- 看起来"多轮全过"其实是一轮重复了多次。
local Seed = tonumber(os.getenv("FUZZ_SEED") or "") or 20260927
local function NextRandom(Bound)
	Seed = (Seed * 1103515245 + 12345) % 2147483648
	return (Seed % Bound) + 1
end

local function RandomVersion()
	-- 长度 1 到 14。太短覆盖不到多段，太长没必要。
	local Length = NextRandom(14)
	local Parts = {}
	for Index = 1, Length do
		local Position = NextRandom(#Alphabet)
		Parts[Index] = Alphabet[Position]
	end

	local Text = table.concat(Parts)

	-- rpm.vercmp 不接受空串，也不接受只由分隔符组成的串。
	-- 保证至少有一个字母或数字。
	if not string.find(Text, "[%w]") then
		Text = Text .. tostring(NextRandom(9))
	end
	return Text
end

local Pairs = io.open("/tmp/fuzz-pairs.txt", "w")
local Expected = io.open("/tmp/fuzz-expected.txt", "w")

if not Pairs or not Expected then
	error("打不开输出文件")
end

for Index = 1, PairCount do
	local Left = RandomVersion()
	local Right = RandomVersion()

	-- 一半的用例加个 epoch 前缀，把 SplitEpoch 也覆盖到。
	if NextRandom(2) == 1 then
		Left = tostring(NextRandom(5) - 1) .. ":" .. Left
	end
	if NextRandom(2) == 1 then
		Right = tostring(NextRandom(5) - 1) .. ":" .. Right
	end

	local Result = rpm.vercmp(Left, Right)
	local Verdict = "="
	if Result < 0 then
		Verdict = "<"
	elseif Result > 0 then
		Verdict = ">"
	end

	Pairs:write(Left .. "\t" .. Right .. "\n")
	Expected:write(Verdict .. "\n")
end

Pairs:close()
Expected:close()