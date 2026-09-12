// 表达式求值器（纯 C++）。语义见 docs/leo-spec.md §6.4：强类型、短路逻辑、数值提升。
#include "Script/LeoExpr.h"
#include <cmath>

namespace leo
{

namespace
{
	bool EvalBoolOp(const FLeoExprNode& Node, const FLeoVarResolver& Resolver,
	                FLeoValue& Out, ELeoDiag& Err, std::string& Msg)
	{
		FLeoValue L;
		if (!LeoEval(*Node.Kids[0], Resolver, L, Err, Msg)) { return false; }
		if (L.Kind != FLeoValue::EKind::Bool)
		{
			Err = ELeoDiag::E_TYPE; Msg = "逻辑运算符要求 Bool 操作数"; return false;
		}
		// 短路：&& 左假 / || 左真 直接出结果
		if ((Node.BinOp == 'A' && !L.B) || (Node.BinOp == 'O' && L.B))
		{
			Out = L; return true;
		}
		FLeoValue R;
		if (!LeoEval(*Node.Kids[1], Resolver, R, Err, Msg)) { return false; }
		if (R.Kind != FLeoValue::EKind::Bool)
		{
			Err = ELeoDiag::E_TYPE; Msg = "逻辑运算符要求 Bool 操作数"; return false;
		}
		Out = FLeoValue::MakeBool(Node.BinOp == 'A' ? (L.B && R.B) : (L.B || R.B));
		return true;
	}

	bool EvalArith(const FLeoExprNode& Node, const FLeoValue& L, const FLeoValue& R,
	               FLeoValue& Out, ELeoDiag& Err, std::string& Msg)
	{
		const char Op = Node.BinOp;
		// 字符串拼接：仅 String + String
		if (Op == '+' && L.Kind == FLeoValue::EKind::String && R.Kind == FLeoValue::EKind::String)
		{
			Out = FLeoValue::MakeString(L.S + R.S); return true;
		}
		if (!L.IsNumber() || !R.IsNumber())
		{
			Err = ELeoDiag::E_TYPE; Msg = std::string("算术运算符要求数值操作数（或 String+String 拼接），得到 ") +
				L.ToString() + " 与 " + R.ToString();
			return false;
		}
		// 取模仅 Int
		if (Op == '%' && (L.Kind != FLeoValue::EKind::Int || R.Kind != FLeoValue::EKind::Int))
		{
			Err = ELeoDiag::E_TYPE; Msg = "% 仅允许 Int 操作数"; return false;
		}
		if (L.Kind == FLeoValue::EKind::Int && R.Kind == FLeoValue::EKind::Int)
		{
			const int64_t A = L.I, B = R.I;
			switch (Op)
			{
			case '+': Out = FLeoValue::MakeInt(A + B); return true;
			case '-': Out = FLeoValue::MakeInt(A - B); return true;
			case '*': Out = FLeoValue::MakeInt(A * B); return true;
			case '/':
				if (B == 0) { Err = ELeoDiag::E_DIV_ZERO; Msg = "Int 除零"; return false; }
				Out = FLeoValue::MakeInt(A / B); return true; // C++ 整除即向零截断
			case '%':
				if (B == 0) { Err = ELeoDiag::E_DIV_ZERO; Msg = "Int 模零"; return false; }
				Out = FLeoValue::MakeInt(A % B); return true;
			}
		}
		const double A = L.AsDouble(), B = R.AsDouble();
		switch (Op)
		{
		case '+': Out = FLeoValue::MakeFloat(A + B); return true;
		case '-': Out = FLeoValue::MakeFloat(A - B); return true;
		case '*': Out = FLeoValue::MakeFloat(A * B); return true;
		case '/':
			if (B == 0.0) { Err = ELeoDiag::E_DIV_ZERO; Msg = "Float 除零"; return false; }
			Out = FLeoValue::MakeFloat(A / B); return true;
		}
		Err = ELeoDiag::E_TYPE; Msg = "未知算术运算符";
		return false;
	}

	bool EvalCompare(const FLeoExprNode& Node, const FLeoValue& L, const FLeoValue& R,
	                 FLeoValue& Out, ELeoDiag& Err, std::string& Msg)
	{
		const char Op = Node.BinOp;
		bool Result = false;
		if (Op == 'E' || Op == 'N') // == / !=
		{
			bool Equal;
			if (L.IsNumber() && R.IsNumber()) { Equal = L.AsDouble() == R.AsDouble(); }
			else if (L.Kind == R.Kind) { Equal = (L == R); }
			else
			{
				Err = ELeoDiag::E_TYPE;
				Msg = "==/!= 不允许跨类型比较（数值与 Bool/String 混用）";
				return false;
			}
			Result = (Op == 'E') ? Equal : !Equal;
		}
		else // < > <= >= 仅数值
		{
			if (!L.IsNumber() || !R.IsNumber())
			{
				Err = ELeoDiag::E_TYPE; Msg = "大小比较仅允许数值操作数"; return false;
			}
			const double A = L.AsDouble(), B = R.AsDouble();
			switch (Op)
			{
			case '<': Result = A < B; break;
			case '>': Result = A > B; break;
			case 'L': Result = A <= B; break;
			case 'G': Result = A >= B; break;
			}
		}
		Out = FLeoValue::MakeBool(Result);
		return true;
	}
} // namespace

bool LeoEval(const FLeoExprNode& Root, const FLeoVarResolver& Resolver,
             FLeoValue& Out, ELeoDiag& Err, std::string& Msg)
{
	switch (Root.Kind)
	{
	case FLeoExprNode::EKind::Lit:
		Out = Root.Lit;
		return true;

	case FLeoExprNode::EKind::Var:
		if (!Resolver || !Resolver(Root.VarName, Out))
		{
			Err = ELeoDiag::E_UNDEF_VAR; Msg = "读取未定义变量: " + Root.VarName;
			return false;
		}
		return true;

	case FLeoExprNode::EKind::Unary:
	{
		FLeoValue V;
		if (!LeoEval(*Root.Kids[0], Resolver, V, Err, Msg)) { return false; }
		if (Root.UnOp == '!')
		{
			if (V.Kind != FLeoValue::EKind::Bool) { Err = ELeoDiag::E_TYPE; Msg = "! 要求 Bool 操作数"; return false; }
			Out = FLeoValue::MakeBool(!V.B);
			return true;
		}
		if (Root.UnOp == '-')
		{
			if (V.Kind == FLeoValue::EKind::Int) { Out = FLeoValue::MakeInt(-V.I); return true; }
			if (V.Kind == FLeoValue::EKind::Float) { Out = FLeoValue::MakeFloat(-V.F); return true; }
			Err = ELeoDiag::E_TYPE; Msg = "一元 - 要求数值操作数"; return false;
		}
		Err = ELeoDiag::E_TYPE; Msg = "未知一元运算符";
		return false;
	}

	case FLeoExprNode::EKind::Binary:
	{
		if (Root.BinOp == 'A' || Root.BinOp == 'O')
		{
			return EvalBoolOp(Root, Resolver, Out, Err, Msg);
		}
		FLeoValue L, R;
		if (!LeoEval(*Root.Kids[0], Resolver, L, Err, Msg)) { return false; }
		if (!LeoEval(*Root.Kids[1], Resolver, R, Err, Msg)) { return false; }
		switch (Root.BinOp)
		{
		case '+': case '-': case '*': case '/': case '%':
			return EvalArith(Root, L, R, Out, Err, Msg);
		case '<': case '>': case 'L': case 'G': case 'E': case 'N':
			return EvalCompare(Root, L, R, Out, Err, Msg);
		}
		Err = ELeoDiag::E_TYPE; Msg = "未知二元运算符";
		return false;
	}
	}
	Err = ELeoDiag::E_TYPE; Msg = "未知表达式节点";
	return false;
}

} // namespace leo
