// 纯内核独立宿主测试：不依赖引擎，直接驱动 .leo 编译器验证 v0.1 语义与 M6 严格 spec。
// 编译：见 tests/run_host_test.bat（MSVC + /std:c++17）；退出码非 0 = 有失败。
#include "Script/LeoCompiler.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace leo;
namespace fs = std::filesystem;

static int gFail = 0, gTotal = 0;

static void Check(bool Cond, const std::string& What)
{
	++gTotal;
	if (!Cond) { ++gFail; printf("[FAIL] %s\n", What.c_str()); }
	else { printf("[ ok ] %s\n", What.c_str()); }
}

static std::string ReadFile(const fs::path& P)
{
	std::ifstream F(P, std::ios::binary);
	std::ostringstream SS;
	SS << F.rdbuf();
	return SS.str();
}

static bool HasDiag(const FLeoProgram& P, ELeoDiag Code)
{
	for (const FLeoDiag& D : P.Diags) { if (D.Code == Code) { return true; } }
	return false;
}

static void CheckDir(const fs::path& Dir, bool bExpectClean)
{
	if (!fs::exists(Dir)) { Check(false, "目录不存在: " + Dir.string()); return; }
	for (const fs::directory_entry& E : fs::directory_iterator(Dir))
	{
		if (E.path().extension() != ".leo") { continue; }
		const FLeoProgram P = CompileChapter(ReadFile(E.path()), E.path().stem().string());
		if (bExpectClean)
		{
			std::string Msg = "pass 语料零错误: " + E.path().filename().string();
			bool Ok = P.Ok;
			if (!Ok && !P.Diags.empty())
			{
				Msg += " —— 首个诊断: " + std::string(LeoDiagName(P.Diags[0].Code)) + "(" +
					std::to_string(P.Diags[0].Line) + ") " + P.Diags[0].Msg;
			}
			Check(Ok, Msg);
		}
		else
		{
			const bool HasError = !P.Ok;
			std::string Msg = "fail 语料报错: " + E.path().filename().string();
			if (HasError && !P.Diags.empty())
			{
				Msg += " —— " + std::string(LeoDiagName(P.Diags[0].Code)) + "(" +
					std::to_string(P.Diags[0].Line) + ")";
			}
			Check(HasError, Msg);
		}
	}
}

int main()
{
	// ---- M6: 严格 spec 注册 + 宽松名单 ----
	// （与运行时注册保持一致：investigate 见 Examples，seq 见 SequencerPerformer）
	SetCustomCommandSpecs({
		FLeoCommandSpec{ "investigate", 1, 1, { "mode" } },
		FLeoCommandSpec{ "seq", 1, 1, { "wait", "rate", "start", "loop" } },
	});
	SetCustomCommandNames({ "mycmd" });

	// 严格：合法用法
	Check(CompileChapter("investigate scene_a mode=strict\ntext - | ok\nend\n", "t1").Ok,
		"strict: investigate 合法用法通过");
	// 严格：缺位置参数
	{
		const FLeoProgram P = CompileChapter("investigate\ntext - | x\nend\n", "t2");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_ARG_COUNT), "strict: 缺位置参数报 E_ARG_COUNT");
	}
	// 严格：位置参数超上限
	{
		const FLeoProgram P = CompileChapter("investigate a b\ntext - | x\nend\n", "t3");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_ARG_COUNT), "strict: 位置参数超上限报 E_ARG_COUNT");
	}
	// 严格：未知命名参数
	{
		const FLeoProgram P = CompileChapter("investigate a speed=3\ntext - | x\nend\n", "t4");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_BAD_PARAM), "strict: 未知命名参数报 E_BAD_PARAM");
	}
	// 严格：位置参数类型非法（- 是 Op token）
	{
		const FLeoProgram P = CompileChapter("investigate -\ntext - | x\nend\n", "t5");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_ARG_BAD), "strict: Op 记号作位置参数报 E_ARG_BAD");
	}
	// 宽松：任意位置/命名参数
	Check(CompileChapter("mycmd a b k=1\ntext - | x\nend\n", "t6").Ok, "lenient: 宽松命令任意参数通过");

	// ---- 既有语义回归（抽样）----
	Check(CompileChapter("set x = 1\nsetg y += 2\ntext - | x\nend\n", "t7").Ok,
		"语义: 复合赋值未定义变量编译期放行（E_UNDEF_VAR 属运行期）");
	{
		const FLeoProgram P = CompileChapter("jump nowhere\ntext - | x\nend\n", "t8");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_UNDEF_LABEL), "语义: 未定义 label 报 E_UNDEF_LABEL");
	}
	Check(CompileChapter("text a | b\n\ttext c | d\nend\n", "t9").Diags.empty() == false, "语义: Tab 仍硬报错");

	// ---- golden 语料 + 工程剧本（须在插件根目录运行）----
	CheckDir(fs::path("tests/golden/pass"), true);
	CheckDir(fs::path("tests/golden/fail"), false);
	CheckDir(fs::path("../../Content/Scripts"), true); // 宿主工程剧本（含 chapter02 的 investigate）

	printf("== %d/%d 通过 ==\n", gTotal - gFail, gTotal);
	return gFail == 0 ? 0 : 1;
}
