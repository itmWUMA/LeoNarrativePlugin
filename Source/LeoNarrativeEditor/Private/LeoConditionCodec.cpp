#include "LeoConditionCodec.h"

#include "ScriptRuntime/LeoScriptBridge.h"

namespace LeoConditionCodec
{

const TArray<FLeoCondOpInfo>& GetOps()
{
	static const TArray<FLeoCondOpInfo> Ops =
	{
		{ ELeoCondOp::IsTrue,  TEXT(""),   TEXT("为真") },
		{ ELeoCondOp::IsFalse, TEXT(""),   TEXT("为假") },
		{ ELeoCondOp::Eq,      TEXT("=="), TEXT("==") },
		{ ELeoCondOp::Ne,      TEXT("!="), TEXT("≠") },
		{ ELeoCondOp::Ge,      TEXT(">="), TEXT("≥") },
		{ ELeoCondOp::Gt,      TEXT(">"),  TEXT(">") },
		{ ELeoCondOp::Le,      TEXT("<="), TEXT("≤") },
		{ ELeoCondOp::Lt,      TEXT("<"),  TEXT("<") },
	};
	return Ops;
}

const FLeoCondOpInfo* FindOp(ELeoCondOp Op)
{
	for (const FLeoCondOpInfo& Info : GetOps())
	{
		if (Info.Op == Op) { return &Info; }
	}
	return nullptr;
}

// 字面量/值 → 带引号转义的 .leo 源文本（String 字面量还原成带引号的源形）
bool ValueToSource(const leo::FLeoValue& V, FString& Out)
{
	switch (V.Kind)
	{
	case leo::FLeoValue::EKind::Bool:
		Out = V.B ? TEXT("true") : TEXT("false");
		return true;
	case leo::FLeoValue::EKind::Int:
		Out = FString::Printf(TEXT("%lld"), static_cast<long long>(V.I));
		return true;
	case leo::FLeoValue::EKind::Float:
		Out = FString::Printf(TEXT("%g"), V.F);
		return true;
	case leo::FLeoValue::EKind::String:
	{
		FString Text(UTF8_TO_TCHAR(V.S.c_str()));
		Text.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
		Text.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
		Out = TEXT("\"") + Text + TEXT("\"");
		return true;
	}
	default:
		return false;
	}
}

namespace
{
	// AST BinOp 字符 → 枚举（leo 约定：'E'== 'N'!= 'G'>= 'L'<= '<' '>'，见 LeoExpr.h）
	const FLeoCondOpInfo* SyntaxFromAstOp(char AstOp)
	{
		switch (AstOp)
		{
		case 'E': return FindOp(ELeoCondOp::Eq);
		case 'N': return FindOp(ELeoCondOp::Ne);
		case 'G': return FindOp(ELeoCondOp::Ge);
		case 'L': return FindOp(ELeoCondOp::Le);
		case '<': return FindOp(ELeoCondOp::Lt);
		case '>': return FindOp(ELeoCondOp::Gt);
		default:  return nullptr;
		}
	}

	// 单段是否为合规条件行：变量在左、字面量在右
	bool MatchRow(const leo::FLeoExprNode& N, FLeoCondRow& OutRow)
	{
		if (N.Kind == leo::FLeoExprNode::EKind::Var)
		{
			OutRow.Var = FName(N.VarName.c_str());
			OutRow.Op = ELeoCondOp::IsTrue;
			OutRow.Value.Reset();
			return !OutRow.Var.IsNone();
		}
		if (N.Kind == leo::FLeoExprNode::EKind::Unary && N.UnOp == '!'
			&& N.Kids[0] && N.Kids[0]->Kind == leo::FLeoExprNode::EKind::Var)
		{
			OutRow.Var = FName(N.Kids[0]->VarName.c_str());
			OutRow.Op = ELeoCondOp::IsFalse;
			OutRow.Value.Reset();
			return !OutRow.Var.IsNone();
		}
		if (N.Kind == leo::FLeoExprNode::EKind::Binary && N.Kids.size() == 2
			&& N.Kids[0] && N.Kids[1]
			&& N.Kids[0]->Kind == leo::FLeoExprNode::EKind::Var
			&& N.Kids[1]->Kind == leo::FLeoExprNode::EKind::Lit)
		{
		const FLeoCondOpInfo* Info = SyntaxFromAstOp(N.BinOp);
		FString LitText;
		if (Info && ValueToSource(N.Kids[1]->Lit, LitText))
			{
				OutRow.Var = FName(N.Kids[0]->VarName.c_str());
				OutRow.Op = Info->Op;
				OutRow.Value = LitText;
				return !OutRow.Var.IsNone();
			}
		}
		return false;
	}

	// 拆顶层 && 链（'A'）为操作数序列；遇其他节点原样入列（MatchRow 再判合规）
	void CollectAndOperands(const leo::FLeoExprNode& N, TArray<const leo::FLeoExprNode*>& Out)
	{
		if (N.Kind == leo::FLeoExprNode::EKind::Binary && N.BinOp == 'A')
		{
			for (const leo::FLeoExprPtr& Kid : N.Kids)
			{
				if (Kid) { CollectAndOperands(*Kid, Out); }
			}
			return;
		}
		Out.Add(&N);
	}
}

bool Parse(const FString& Expr, TArray<FLeoCondRow>& OutRows)
{
	OutRows.Reset();
	if (Expr.TrimStartAndEnd().IsEmpty()) { return true; } // 恒真（顺序/默认边）

	leo::FLeoDiag D;
	const leo::FLeoExprPtr Root = LeoBridge::CompileExpr(Expr, D);
	if (!Root) { return false; }

	TArray<const leo::FLeoExprNode*> Operands;
	CollectAndOperands(*Root, Operands);
	for (const leo::FLeoExprNode* N : Operands)
	{
		FLeoCondRow Row;
		if (!MatchRow(*N, Row)) { OutRows.Reset(); return false; }
		OutRows.Add(MoveTemp(Row));
	}
	return true;
}

FString Generate(const TArray<FLeoCondRow>& Rows)
{
	TArray<FString> Segments;
	Segments.Reserve(Rows.Num());
	for (const FLeoCondRow& R : Rows)
	{
		if (R.Var.IsNone()) { continue; }
		if (R.Op == ELeoCondOp::IsTrue)       { Segments.Add(R.Var.ToString()); }
		else if (R.Op == ELeoCondOp::IsFalse) { Segments.Add(TEXT("!") + R.Var.ToString()); }
		else
		{
			const FString Syntax = FindOp(R.Op) ? FindOp(R.Op)->Syntax : TEXT("==");
			Segments.Add(FString::Printf(TEXT("%s %s %s"), *R.Var.ToString(), *Syntax, *R.Value));
		}
	}
	return FString::Join(Segments, TEXT(" && "));
}

FString Summarize(const FString& Expr)
{
	const FString Trimmed = Expr.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) { return TEXT("无条件"); }

	TArray<FLeoCondRow> Rows;
	if (Parse(Trimmed, Rows))
	{
		// 行摘要用紧凑形："affection≥3 && saw_secret"
		TArray<FString> Parts;
		for (const FLeoCondRow& R : Rows)
		{
			if (R.Var.IsNone()) { continue; }
			if (R.Op == ELeoCondOp::IsTrue)       { Parts.Add(R.Var.ToString()); }
			else if (R.Op == ELeoCondOp::IsFalse) { Parts.Add(TEXT("!") + R.Var.ToString()); }
			else
			{
				const FLeoCondOpInfo* Info = FindOp(R.Op);
				Parts.Add(R.Var.ToString() + (Info ? Info->Display : TEXT("?")) + R.Value);
			}
		}
		const FString Joined = FString::Join(Parts, TEXT(" && "));
		return Joined.Len() <= 28 ? Joined : Joined.Left(25) + TEXT("…");
	}
	return Trimmed.Len() <= 28 ? Trimmed : Trimmed.Left(25) + TEXT("…");
}

} // namespace LeoConditionCodec
