// 编排图求值共享库：子系统的运行推进与编辑器干跑模拟共用（语义单源，防漂移）。
// 条件求值 / 出边选择 / 转移副作用，全部基于 .leo 表达式引擎（LeoBridge::CompileExpr）。
#pragma once

#include "CoreMinimal.h"
#include "Script/LeoExpr.h"

class UNarrativeBlackboard;
struct FLeoScenarioNode;
struct FLeoScenarioEdge;

namespace LeoGraphEval
{
	// 表达式编译缓存（子系统与模拟器各持一份；键 = 表达式原文）
	using FExprCache = TMap<FString, leo::FLeoExprPtr>;

	// 条件求值：空串恒真；未定义变量/类型不符按假处理（与脚本 jumpif 同语义）
	LEONARRATIVE_API bool EvalCondition(const FString& Condition, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
		FExprCache& Cache);

	// 出边选择：优先级降序（稳定），第一条条件为真者胜出，并执行其 Actions。
	// 返回目标节点 Id；无可用边返回 NAME_None
	LEONARRATIVE_API FName SelectEdge(const FLeoScenarioNode& Node, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
		FExprCache& Cache);

	// 逐边求值（索引对齐 Node.Edges），供编辑器模拟显示每条边真假
	LEONARRATIVE_API void EvaluateAllEdges(const FLeoScenarioNode& Node, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
		FExprCache& Cache, TArray<bool>& OutResults);

	// 执行边副作用（SelectEdge 内部使用；模拟器重放也用）
	LEONARRATIVE_API void ApplyActions(const FLeoScenarioEdge& Edge, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
		FExprCache& Cache);
}
