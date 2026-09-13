// ULeoScenarioGraph 的非内联实现：旧资产 Op 字符串 → ELeoEdgeOp 枚举迁移。
#include "Data/LeoScenarioGraph.h"

const TCHAR* LeoEdgeOpString(ELeoEdgeOp Op)
{
	switch (Op)
	{
	case ELeoEdgeOp::Assign:    return TEXT("=");
	case ELeoEdgeOp::AddAssign: return TEXT("+=");
	case ELeoEdgeOp::SubAssign: return TEXT("-=");
	case ELeoEdgeOp::MulAssign: return TEXT("*=");
	case ELeoEdgeOp::DivAssign: return TEXT("/=");
	default:                    return TEXT("?");
	}
}

namespace
{
	ELeoEdgeOp OpFromString(const FString& Str)
	{
		if (Str == TEXT("+=")) { return ELeoEdgeOp::AddAssign; }
		if (Str == TEXT("-=")) { return ELeoEdgeOp::SubAssign; }
		if (Str == TEXT("*=")) { return ELeoEdgeOp::MulAssign; }
		if (Str == TEXT("/=")) { return ELeoEdgeOp::DivAssign; }
		return ELeoEdgeOp::Assign;
	}
}

void ULeoScenarioGraph::PostLoad()
{
	Super::PostLoad();
	// 存量迁移：旧 FString Op（含默认 "="）→ 枚举；之后 Op 永远为空，新存档不再写出
	for (FLeoScenarioNode& N : Nodes)
	{
		for (FLeoScenarioEdge& E : N.Edges)
		{
			for (FLeoEdgeAction& A : E.Actions)
			{
				if (!A.Op.IsEmpty())
				{
					A.Operation = OpFromString(A.Op);
					A.Op.Reset();
				}
			}
		}
	}
}
