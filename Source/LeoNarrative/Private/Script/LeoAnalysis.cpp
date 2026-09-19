// 静态分析（纯 C++）：表达式变量引用收集 + 整章变量使用统计 + 整章表现资产引用收集。
// 消费方是 UE 侧工具（变量收割注册表、图校验拼写检查、清单核对、章节预载），内核自身不使用。
#include "Script/LeoExpr.h"
#include "Script/LeoProgram.h"

namespace leo
{

void LeoCollectExprReads(const FLeoExprNode& Root, std::vector<std::string>& OutNames)
{
	switch (Root.Kind)
	{
	case FLeoExprNode::EKind::Var:
		OutNames.push_back(Root.VarName);
		break;
	case FLeoExprNode::EKind::Unary:
	case FLeoExprNode::EKind::Binary:
		for (const FLeoExprPtr& Kid : Root.Kids)
		{
			if (Kid) { LeoCollectExprReads(*Kid, OutNames); }
		}
		break;
	default: break; // Lit
	}
}

void LeoCollectVarUsage(const FLeoProgram& P, std::unordered_map<std::string, FLeoVarUsage>& Out)
{
	// 读取：表达式池条目全部被命令引用（set 值 / jumpif / choice 条件），无需逐命令关联
	std::vector<std::string> Reads;
	Reads.reserve(P.ExprPool.size() * 2);
	for (const FLeoExprPtr& E : P.ExprPool)
	{
		if (E) { LeoCollectExprReads(*E, Reads); }
	}
	for (const std::string& Name : Reads) { Out[Name].bRead = true; }

	// 写入：set/setg 目标 + 值为根字面量时的类型提示
	for (const FLeoCommand& C : P.Commands)
	{
		if (C.Kind != ELeoCmd::Set && C.Kind != ELeoCmd::SetG) { continue; }
		FLeoVarUsage& U = Out[C.Name];
		U.bWritten = true;
		U.bGlobal = (C.Kind == ELeoCmd::SetG);
		if (C.ExprIndex >= 0 && C.ExprIndex < static_cast<int>(P.ExprPool.size()) && P.ExprPool[C.ExprIndex]
			&& P.ExprPool[C.ExprIndex]->Kind == FLeoExprNode::EKind::Lit)
		{
			U.LitKind = P.ExprPool[C.ExprIndex]->Lit.Kind;
		}
	}
}

void LeoCollectAssetRefs(const FLeoProgram& P, const std::vector<FLeoCustomAssetArg>& CustomAssetArgs,
	std::unordered_map<std::string, FLeoAssetRef>& Out)
{
	// 内置命令的类别 token（命令语义的一部分；清单侧以同名类别节承接）
	static const char* const BgmToken = "bgm";
	static const char* const SeToken = "se";
	static const char* const VoiceToken = "voice";
	static const char* const BgToken = "bg";
	static const char* const CharToken = "char";

	auto AddRef = [&](const char* Kind, const std::string& Raw, int Line)
	{
		if (Raw.empty() || Raw == "-") { return; } // "-" = 移除/停止，不引用资产
		FLeoAssetRef& R = Out[Raw];
		if (R.Id.empty()) // 首次引用定类别与行号；重复引用保留首信息
		{
			R.Kind = Kind;
			R.Id = Raw;
			R.Line = Line;
		}
	};

	for (const FLeoCommand& C : P.Commands)
	{
		switch (C.Kind)
		{
		case ELeoCmd::Bgm:    AddRef(BgmToken, C.AssetId, C.Line); break;
		case ELeoCmd::Se:     AddRef(SeToken, C.AssetId, C.Line); break;
		case ELeoCmd::Voice:  AddRef(VoiceToken, C.AssetId, C.Line); break;
		case ELeoCmd::Bg:     AddRef(BgToken, C.AssetId, C.Line); break;
		case ELeoCmd::Char:   AddRef(CharToken, C.AssetId, C.Line); break;
		case ELeoCmd::Custom:
			// 自定义命令按声明表取参数位（seq 等框架命令、项目自定义命令的声明由 UE 侧传入）
			for (const FLeoCustomAssetArg& A : CustomAssetArgs)
			{
				if (C.CustomName == A.Cmd && A.ArgIndex < static_cast<int>(C.CustomArgs.size()))
				{
					AddRef(A.Kind.c_str(), C.CustomArgs[A.ArgIndex], C.Line);
					break;
				}
			}
			break;
		default: break;
		}
	}
}

} // namespace leo
