// 静态分析（纯 C++）：表达式变量引用收集 + 整章变量使用统计。
// 消费方是 UE 侧编辑器工具（变量收割注册表、图校验的未知变量拼写检查），内核自身不使用。
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

} // namespace leo
