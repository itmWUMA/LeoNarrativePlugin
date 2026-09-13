#include "Data/LeoGraphEval.h"

#include "Blackboard/NarrativeBlackboard.h"
#include "Data/LeoScenarioGraph.h"
#include "ScriptRuntime/LeoScriptBridge.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoGraphEval, Log, All);

namespace
{
	leo::FLeoExprPtr GetCompiled(const FString& Text, LeoGraphEval::FExprCache& Cache)
	{
		if (leo::FLeoExprPtr* Found = Cache.Find(Text)) { return *Found; }
		leo::FLeoDiag D;
		leo::FLeoExprPtr Expr = LeoBridge::CompileExpr(Text, D);
		if (Expr)
		{
			Cache.Add(Text, Expr);
		}
		else
		{
			UE_LOG(LogLeoGraphEval, Error, TEXT("表达式编译失败: %s (%s)"), *Text, LeoBridge::DiagName(D.Code));
		}
		return Expr;
	}

	// 变量解析：局部（若在）→ 全局
	leo::FLeoVarResolver MakeResolver(UNarrativeBlackboard* Local, UNarrativeBlackboard* Global)
	{
		return [Local, Global](const std::string& Name, leo::FLeoValue& OutV) -> bool
		{
			const FName Key(Name.c_str());
			if (Local && Local->GetValue(Key, OutV)) { return true; }
			if (Global && Global->GetValue(Key, OutV)) { return true; }
			return false;
		};
	}

	bool EvalToBool(const FString& Text, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
		LeoGraphEval::FExprCache& Cache)
	{
		const leo::FLeoExprPtr Expr = GetCompiled(Text, Cache);
		if (!Expr) { return false; }
		leo::FLeoValue V;
		leo::ELeoDiag Code;
		std::string Msg;
		if (!leo::LeoEval(*Expr, MakeResolver(Local, Global), V, Code, Msg))
		{
			UE_LOG(LogLeoGraphEval, Error, TEXT("表达式求值失败: %s —— 按假"), *Text);
			return false;
		}
		if (V.Kind != leo::FLeoValue::EKind::Bool)
		{
			UE_LOG(LogLeoGraphEval, Error, TEXT("边条件结果非 Bool: %s —— 按假"), *Text);
			return false;
		}
		return V.B;
	}

	// 数值四则（Int/Int 保持 Int；除法总是 Float；镜像 VM set 语义的简化版）
	bool NumericOp(const leo::FLeoValue& A, const leo::FLeoValue& B, ELeoEdgeOp Op, leo::FLeoValue& Out)
	{
		if (!A.IsNumber() || !B.IsNumber()) { return false; }
		const double a = A.AsDouble(), b = B.AsDouble();
		double r = 0.0;
		switch (Op)
		{
		case ELeoEdgeOp::AddAssign: r = a + b; break;
		case ELeoEdgeOp::SubAssign: r = a - b; break;
		case ELeoEdgeOp::MulAssign: r = a * b; break;
		case ELeoEdgeOp::DivAssign:
			if (b == 0.0) { return false; }
			Out = leo::FLeoValue::MakeFloat(a / b);
			return true;
		default: return false;
		}
		if (A.Kind == leo::FLeoValue::EKind::Int && B.Kind == leo::FLeoValue::EKind::Int)
		{
			Out = leo::FLeoValue::MakeInt(static_cast<int64_t>(r));
		}
		else
		{
			Out = leo::FLeoValue::MakeFloat(r);
		}
		return true;
	}
}

namespace LeoGraphEval
{

bool EvalCondition(const FString& Condition, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
	FExprCache& Cache)
{
	if (Condition.TrimStartAndEnd().IsEmpty()) { return true; } // 空条件恒真
	return EvalToBool(Condition, Local, Global, Cache);
}

void EvaluateAllEdges(const FLeoScenarioNode& Node, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
	FExprCache& Cache, TArray<bool>& OutResults)
{
	OutResults.Init(false, Node.Edges.Num());
	for (int32 i = 0; i < Node.Edges.Num(); ++i)
	{
		OutResults[i] = EvalCondition(Node.Edges[i].Condition, Local, Global, Cache);
	}
}

FName SelectEdge(const FLeoScenarioNode& Node, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
	FExprCache& Cache)
{
	// 优先级降序；同优先级按数组序（稳定排序）
	TArray<int32> Order;
	for (int32 i = 0; i < Node.Edges.Num(); ++i) { Order.Add(i); }
	Order.StableSort([&Node](int32 A, int32 B) { return Node.Edges[A].Priority > Node.Edges[B].Priority; });

	for (const int32 Idx : Order)
	{
		const FLeoScenarioEdge& E = Node.Edges[Idx];
		if (!EvalCondition(E.Condition, Local, Global, Cache)) { continue; }
		ApplyActions(E, Local, Global, Cache);
		return E.To;
	}
	return NAME_None;
}

void ApplyActions(const FLeoScenarioEdge& Edge, UNarrativeBlackboard* Local, UNarrativeBlackboard* Global,
	FExprCache& Cache)
{
	for (const FLeoEdgeAction& A : Edge.Actions)
	{
		const leo::FLeoExprPtr Expr = GetCompiled(A.Expr, Cache);
		if (!Expr) { continue; }
		leo::FLeoValue NewV;
		leo::ELeoDiag Code;
		std::string Msg;
		if (!leo::LeoEval(*Expr, MakeResolver(Local, Global), NewV, Code, Msg))
		{
			UE_LOG(LogLeoGraphEval, Error, TEXT("边副作用求值失败: %s %s %s"),
				*A.Key.ToString(), LeoEdgeOpString(A.Operation), *A.Expr);
			continue;
		}
		// 写入层：setg 写全局；set 优先局部层（无会话局部层时落到全局）
		UNarrativeBlackboard* Scope = A.bGlobal ? Global : (Local ? Local : Global);
		if (!Scope) { continue; }
		const FName Key(A.Key);

		leo::FLeoValue Result;
		if (A.Operation == ELeoEdgeOp::Assign)
		{
			Result = NewV;
		}
		else
		{
			leo::FLeoValue Old;
			const bool bHasOld = MakeResolver(Local, Global)(TCHAR_TO_UTF8(*A.Key.ToString()), Old);
			if (A.Operation == ELeoEdgeOp::AddAssign
				&& (Old.Kind == leo::FLeoValue::EKind::String || NewV.Kind == leo::FLeoValue::EKind::String))
			{
				// 字符串拼接（镜像 VM：String += String）
				if (bHasOld && Old.Kind == leo::FLeoValue::EKind::String && NewV.Kind == leo::FLeoValue::EKind::String)
				{
					Result = leo::FLeoValue::MakeString(Old.S + NewV.S);
				}
				else
				{
					UE_LOG(LogLeoGraphEval, Error, TEXT("边副作用类型不兼容: %s %s"),
						*A.Key.ToString(), LeoEdgeOpString(A.Operation));
					continue;
				}
			}
			else if (!bHasOld || !NumericOp(Old, NewV, A.Operation, Result))
			{
				UE_LOG(LogLeoGraphEval, Error, TEXT("边副作用不合法: %s %s（复合赋值需已有数值或字符串）"),
					*A.Key.ToString(), LeoEdgeOpString(A.Operation));
				continue;
			}
		}
		Scope->SetValue(Key, Result);
		UE_LOG(LogLeoGraphEval, Verbose, TEXT("边副作用: %s%s%s → %s"), *A.Key.ToString(),
			A.bGlobal ? TEXT("(g)") : TEXT(""), LeoEdgeOpString(A.Operation), *LeoBridge::ToFString(Result.ToString()));
	}
}

} // namespace LeoGraphEval
