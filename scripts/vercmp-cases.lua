#!/usr/bin/lua
--
-- vercmp-cases.lua - rpmvercmp 的权威期望值生成器。
--
-- 用系统 rpm 自带的 Lua 解释器跑，不依赖我们的实现。产出的期望值直接当
-- 测试向量，禁止手写。
--
-- 用法：
--   rpm --eval "%{lua: dofile('vercmp-cases.lua')}"
--
-- 输出：
--   每行三列：左操作数、右操作数、比较结果（< / = / >）
--   与 vercmp-expected.txt 一致。
--
-- 为什么需要这个文件：rpmvercmp 对非字母非数字字符是直接跳过、不参与比较，
-- 不是按 ASCII 值比，也不是"比较剩下的段"。手写期望值必然写错。

local Cases = {
	-- 基本相等与数字段
	{ "1.0", "1.0" },
	{ "1.0", "1.1" },
	{ "1.0", "1.0.1" },

	-- 波浪号：排在任何东西之前
	{ "1.0~rc1", "1.0" },
	{ "1.0~rc1", "1.0~rc2" },

	-- 脱字符：排在任何东西之后
	{ "1.0^git1", "1.0" },
	{ "1.0^git1", "1.1" },

	-- 字母段与空段
	{ "1.0a", "1.0" },
	{ "1.0", "1.0a" },

	-- 缺段补空：与 semver 相反，1.0 < 1.0.0
	{ "1.0", "1.0.0" },

	-- 数值比，不是字典序
	{ "2.0", "10.0" },

	-- release 段
	{ "1.0", "1.0-1" },
	{ "1.0-1", "1.0-2" },

	-- epoch 优先
	{ "1:1.0", "2.0" },
	{ "1:1.0", "1:1.0" },

	-- dist tag
	{ "1.0-1.fc41", "1.0-1.fc42" },

	-- 零
	{ "0", "0" },

	-- 非字母非数字：直接跳过，因此相等
	{ "1.0+", "1.0" },
	{ "1.0.", "1.0" },

	-- 真实 Fedora EVR
	{ "2.39-6.fc41", "2.39-6.fc42" },
	{ "3.30.5-1.fc41", "3.30.5" },

	-- 多段数字与字母交替
	{ "1.2.3", "1.2.3.4" },
	{ "1.2a3", "1.2a4" },
	{ "1.2a3", "1.2.3" },

	-- 波浪号与脱字符混用
	{ "1.0~rc1^git", "1.0~rc1" },
	{ "1.0~rc1^git", "1.0" },

	-- 前导零
	{ "1.01", "1.1" },
	{ "01", "1" },

	-- 大小写：字母段按字典序，大写字母小于小写
	{ "1.0A", "1.0a" },
	{ "1.0Z", "1.0a" },

	-- 长数字段
	{ "20240101", "20240102" },
	{ "1.20240101", "1.20240102" },
}

-- 注：不测空串。rpm 的 Lua 绑定会先校验参数，空串直接报
-- "invalid version"。空串的比较行为要对着 librpm 的 rpmvercmp.c 单独确认。

local function Verdict(Result)
	if Result < 0 then
		return "<"
	elseif Result > 0 then
		return ">"
	end
	return "="
end

-- rpm 的 --eval 会把输出里的换行吃掉，所以直接写文件，不走标准输出。
-- 输出路径可以用 -o 之外的全局变量 OutputPath 覆盖。
local Path = OutputPath or "/tmp/vercmp-expected.txt"
local Handle, OpenError = io.open(Path, "w")

if not Handle then
	error("打不开输出文件 " .. Path .. "：" .. tostring(OpenError))
end

for _, Case in ipairs(Cases) do
	local Result = rpm.vercmp(Case[1], Case[2])
	Handle:write(string.format("%-18s %-18s %s\n", Case[1], Case[2], Verdict(Result)))
end

Handle:close()