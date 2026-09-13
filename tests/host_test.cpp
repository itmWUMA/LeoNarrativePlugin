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

	// ---- 静态分析：变量引用收集（图条件拼写检查 / 编辑器收割的地基）----
	{
		const FLeoProgram P = CompileChapter(
			"set affection = 1\nsetg route = \"a\"\njumpif affection >= 2 && flag -> lab_a\nlabel lab_a\ntext - | x\nend\n", "t10");
		Check(P.Ok, "分析: 样例章节编译通过");
		std::unordered_map<std::string, FLeoVarUsage> Usage;
		LeoCollectVarUsage(P, Usage);
		Check(Usage.count("affection") == 1
			&& Usage["affection"].bWritten && !Usage["affection"].bGlobal
			&& Usage["affection"].bRead
			&& Usage["affection"].LitKind == FLeoValue::EKind::Int,
			"分析: set 目标收为局部写入+字面量类型提示");
		Check(Usage.count("route") == 1
			&& Usage["route"].bWritten && Usage["route"].bGlobal
			&& Usage["route"].LitKind == FLeoValue::EKind::String,
			"分析: setg 目标收为全局写入");
		Check(Usage.count("flag") == 1 && Usage["flag"].bRead && !Usage["flag"].bWritten,
			"分析: jumpif 引用但未写入 → 只读（拼写检查的检出目标）");
		std::vector<std::string> Reads;
		FLeoDiag D;
		const FLeoExprPtr E = CompileExprSrc("a + b * 2 > c", D);
		Check(E != nullptr, "分析: 表达式编译成功");
		LeoCollectExprReads(*E, Reads);
		Check(Reads.size() == 3, "分析: 表达式收集 3 个变量名");
	}

	// ---- 本地化显式文本 ID（spec §8）----
	{
		const FLeoProgram P = CompileChapter("text 李雷 | 你好。 id=d/a/0\nend\n", "lid1");
		Check(P.Ok && P.Commands.size() == 2
			&& P.Commands[0].TextId == "d/a/0" && P.Commands[0].Body == "你好。"
			&& P.Commands[0].Speaker == "李雷",
			"l10n: text 尾缀 id= 剥离进 TextId（body/speaker 不含尾缀）");
	}
	{
		const FLeoProgram P = CompileChapter("text - | 我的 ID 是 id=admin\nend\n", "lid2");
		Check(P.Ok && P.Commands[0].TextId.empty() && P.Commands[0].Body == "我的 ID 是 id=admin",
			"l10n: 无斜杠的 id=xxx 视为正文（不剥离不报错）");
	}
	{
		const FLeoProgram P = CompileChapter("text - | 你好。 id=x/a!b\nend\n", "lid3");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_BAD_TEXT_ID), "l10n: 斜杠+非法字符报 E_BAD_TEXT_ID");
	}
	{
		const FLeoProgram P = CompileChapter("text - | 一 id=x/a\ntext - | 二 id=x/a\nend\n", "lid4");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_DUP_TEXT_ID), "l10n: 显式 ID 重复报 E_DUP_TEXT_ID");
	}
	{
		const FLeoProgram P = CompileChapter(
			"choice\n    甲 -> lab_a id=d/c/0/0\n    乙 -> lab_b if v >= 1 id=d/c/0/1\nlabel lab_a\ntext - | x\nend\nlabel lab_b\ntext - | y\nend\n", "lid5");
		Check(P.Ok
			&& P.Commands[0].Kind == ELeoCmd::Choice
			&& P.Commands[0].Options[0].TextId == "d/c/0/0"
			&& P.Commands[0].Options[1].TextId == "d/c/0/1"
			&& P.Commands[0].Options[1].ExprIndex >= 0,
			"l10n: 选项行 id= 尾缀与 if 条件共存");
	}
	{
		const FLeoProgram P = CompileChapter(
			"choice\n    甲 -> lab_a id=x/o\nlabel lab_a\ntext - | 撞 id=x/o\nend\n", "lid6");
		Check(!P.Ok && HasDiag(P, ELeoDiag::E_DUP_TEXT_ID), "l10n: 选项与 text 显式 ID 相互撞号报 E_DUP_TEXT_ID");
	}
	{
		// 正文经 LTrim 后以 id= 开头（无前置空格）不构成尾缀，整段视为正文
		const FLeoProgram P = CompileChapter("text - | id=only/x\nend\n", "lid7");
		Check(P.Ok && P.Commands[0].TextId.empty() && P.Commands[0].Body == "id=only/x",
			"l10n: 行首 id=（无前置空格）视为正文");
	}

	// ---- golden 语料 + 工程剧本（须在插件根目录运行）----
	CheckDir(fs::path("tests/golden/pass"), true);
	CheckDir(fs::path("tests/golden/fail"), false);
	CheckDir(fs::path("../../Content/Scripts"), true); // 宿主工程剧本（含 chapter02 的 investigate）

	printf("== %d/%d 通过 ==\n", gTotal - gFail, gTotal);
	return gFail == 0 ? 0 : 1;
}
