// .leo 编译器主体：行词法 → 命令解析 → 表达式解析 → 后置校验（label 回填/警告）
// 纯 C++，不依赖 UE。规范：docs/leo-spec.md
#include "Script/LeoCompiler.h"

#include <charconv>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>

namespace leo
{

// ---------------- 通用小工具 ----------------
namespace
{
	std::string LTrim(const std::string& S)
	{
		size_t B = S.find_first_not_of(' ');
		return B == std::string::npos ? std::string() : S.substr(B);
	}
	std::string RTrim(const std::string& S)
	{
		size_t E = S.find_last_not_of(' ');
		return E == std::string::npos ? std::string() : S.substr(0, E + 1);
	}
	std::string Trim(const std::string& S) { return RTrim(LTrim(S)); }
	std::string FirstWord(const std::string& S)
	{
		size_t Sp = S.find(' ');
		return Sp == std::string::npos ? S : S.substr(0, Sp);
	}
	bool IsDigit(char C) { return C >= '0' && C <= '9'; }
	bool IsIdentStart(char C) { return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_'; }
	bool IsIdentChar(char C) { return IsIdentStart(C) || IsDigit(C); }
	bool IsIdent(const std::string& S)
	{
		if (S.empty() || !IsIdentStart(S[0])) { return false; }
		for (char C : S) { if (!IsIdentChar(C)) { return false; } }
		return true;
	}
	// Word 的终止字符集（见 spec §3）
	bool IsWordTerm(char C)
	{
		return C == ' ' || C == '"' || C == '=' || C == '<' || C == '>' || C == '!'
			|| C == '&' || C == '|' || C == '+' || C == '-' || C == '*' || C == '/'
			|| C == '%' || C == '(' || C == ')';
	}
	bool ParseInt64(const std::string& S, int64_t& Out)
	{
		auto R = std::from_chars(S.data(), S.data() + S.size(), Out);
		return R.ec == std::errc() && R.ptr == S.data() + S.size();
	}
	bool ParseDouble(const std::string& S, double& Out)
	{
		auto R = std::from_chars(S.data(), S.data() + S.size(), Out);
		return R.ec == std::errc() && R.ptr == S.data() + S.size();
	}

	// ---------------- 行词法 ----------------
	enum class ETok { Word, Int, Float, Str, Op, Arrow };
	struct FTok
	{
		ETok Type = ETok::Word;
		std::string Text; // Str 为解码后内容；Int/Float/Op 为原文
	};

	bool LexLine(const std::string& Line, std::vector<FTok>& Out, FLeoDiag& D)
	{
		static const std::string SingleOps = "=<>!+-*/%()";
		const size_t N = Line.size();
		size_t i = 0;
		while (i < N)
		{
			const char C = Line[i];
			const char Next = (i + 1 < N) ? Line[i + 1] : '\0';
			if (C == ' ') { ++i; continue; }

			if (C == '"') // 字符串字面量
			{
				++i;
				std::string Val;
				bool bClosed = false;
				while (i < N)
				{
					const char Ch = Line[i];
					if (Ch == '\\')
					{
						if (i + 1 >= N) { break; }
						const char Esc = Line[i + 1];
						if (Esc == '"')      { Val += '"'; }
						else if (Esc == '\\'){ Val += '\\'; }
						else if (Esc == 'n') { Val += '\n'; }
						else if (Esc == 't') { Val += '\t'; }
						else if (Esc == 'r') { Val += '\r'; }
						else
						{
							D = FLeoDiag::Make(ELeoDiag::E_BAD_ESCAPE, 0, std::string("未知转义序列 \\") + Esc);
							return false;
						}
						i += 2;
						continue;
					}
					if (Ch == '"') { bClosed = true; ++i; break; }
					Val += Ch;
					++i;
				}
				if (!bClosed)
				{
					D = FLeoDiag::Make(ELeoDiag::E_UNTERM_STRING, 0, "字符串未闭合");
					return false;
				}
				FTok T; T.Type = ETok::Str; T.Text = std::move(Val);
				Out.push_back(std::move(T));
				continue;
			}

			if (IsDigit(C)) // 数字（无符号；负数走一元 -）
			{
				size_t j = i;
				while (j < N && IsDigit(Line[j])) { ++j; }
				bool bFloat = false;
				if (j < N && Line[j] == '.')
				{
					if (j + 1 < N && IsDigit(Line[j + 1]))
					{
						bFloat = true;
						++j;
						while (j < N && IsDigit(Line[j])) { ++j; }
					}
					else
					{
						D = FLeoDiag::Make(ELeoDiag::E_BAD_NUMBER, 0, "小数点后缺少数字");
						return false;
					}
				}
				FTok T; T.Type = bFloat ? ETok::Float : ETok::Int; T.Text = Line.substr(i, j - i);
				Out.push_back(std::move(T));
				i = j;
				continue;
			}

			auto PushOp = [&](const char* S)
			{
				FTok T; T.Type = ETok::Op; T.Text = S;
				Out.push_back(std::move(T));
			};
			// 两字符运算符与箭头（精确逐个比对——不能用子串搜索，"= " 这类会在列表分隔空格处假命中）
			if (Next != '\0')
			{
				static const char* const TwoCharOps[] = { "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=" };
				bool bMatched = false;
				for (const char* const OpStr : TwoCharOps)
				{
					if (C == OpStr[0] && Next == OpStr[1])
					{
						PushOp(OpStr);
						i += 2;
						bMatched = true;
						break;
					}
				}
				if (bMatched) { continue; }
				if (C == '-' && Next == '>')
				{
					FTok T; T.Type = ETok::Arrow; T.Text = "->";
					Out.push_back(std::move(T));
					i += 2;
					continue;
				}
			}
			if (C == '&' || C == '|')
			{
				D = FLeoDiag::Make(ELeoDiag::E_BAD_TOKEN, 0, std::string("单独的 '") + C + "'（单竖线只用于 text 的说话人分隔）");
				return false;
			}
			if (SingleOps.find(C) != std::string::npos)
			{
				const char One[2] = { C, '\0' };
				PushOp(One);
				++i;
				continue;
			}
			// Word：直到终止字符集
			{
				size_t j = i;
				while (j < N && !IsWordTerm(Line[j])) { ++j; }
				FTok T; T.Type = ETok::Word; T.Text = Line.substr(i, j - i);
				Out.push_back(std::move(T));
				i = j;
			}
		}
		return true;
	}

	// ---------------- 表达式递归下降（优先级见 spec §6.2）----------------
	class FExprParser
	{
	public:
		FExprParser(const std::vector<FTok>& InTokens, size_t InBegin, size_t InEnd)
			: Tokens(InTokens), Pos(InBegin), End(InEnd) {}

		FLeoExprPtr Parse(FLeoDiag& D)
		{
			FLeoExprPtr E = ParseOr(D);
			if (E && Pos != End)
			{
				D = FLeoDiag::Make(ELeoDiag::E_BAD_EXPR, 0, "表达式末尾有多余内容: '" + Tokens[Pos].Text + "'");
				return nullptr;
			}
			return E;
		}

	private:
		const std::vector<FTok>& Tokens;
		size_t Pos;
		const size_t End;

		bool AtEnd() const { return Pos >= End; }
		bool IsOp(const char* S) const { return !AtEnd() && Tokens[Pos].Type == ETok::Op && Tokens[Pos].Text == S; }
		bool EatOp(const char* S) { if (IsOp(S)) { ++Pos; return true; } return false; }
		static std::shared_ptr<FLeoExprNode> Mk() { return std::make_shared<FLeoExprNode>(); }

		FLeoExprPtr Bin(const char* OpStr, char Code, FLeoExprPtr (FExprParser::*Sub)(FLeoDiag&), FLeoDiag& D)
		{
			FLeoExprPtr L = (this->*Sub)(D);
			if (!L) { return nullptr; }
			while (IsOp(OpStr))
			{
				++Pos;
				FLeoExprPtr R = (this->*Sub)(D);
				if (!R) { return nullptr; }
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Binary;
				N->BinOp = Code;
				N->Kids = { std::move(L), std::move(R) };
				L = std::move(N);
			}
			return L;
		}

		FLeoExprPtr ParseOr(FLeoDiag& D)  { return Bin("||", 'O', &FExprParser::ParseAnd, D); }
		FLeoExprPtr ParseAnd(FLeoDiag& D) { return Bin("&&", 'A', &FExprParser::ParseCmp, D); }

		// 比较与相等不可链式：至多一个运算符（spec §6.2）
		FLeoExprPtr ParseCmp(FLeoDiag& D)
		{
			FLeoExprPtr L = ParseEq(D);
			if (!L) { return nullptr; }
			char Code = 0;
			const char* OpStr = nullptr;
			if (IsOp("<")) { Code = '<'; OpStr = "<"; }
			else if (IsOp(">")) { Code = '>'; OpStr = ">"; }
			else if (IsOp("<=")) { Code = 'L'; OpStr = "<="; }
			else if (IsOp(">=")) { Code = 'G'; OpStr = ">="; }
			if (Code != 0)
			{
				++Pos;
				FLeoExprPtr R = ParseEq(D);
				if (!R) { return nullptr; }
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Binary;
				N->BinOp = Code;
				N->Kids = { std::move(L), std::move(R) };
				return N;
			}
			return L;
		}
		FLeoExprPtr ParseEq(FLeoDiag& D)
		{
			FLeoExprPtr L = ParseAddReal(D);
			if (!L) { return nullptr; }
			char Code = 0;
			if (IsOp("==")) { Code = 'E'; }
			else if (IsOp("!=")) { Code = 'N'; }
			if (Code != 0)
			{
				++Pos;
				FLeoExprPtr R = ParseAddReal(D);
				if (!R) { return nullptr; }
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Binary;
				N->BinOp = Code;
				N->Kids = { std::move(L), std::move(R) };
				return N;
			}
			return L;
		}
		FLeoExprPtr ParseAddReal(FLeoDiag& D) { return BinChain("+-", "+-", &FExprParser::ParseMul, D); }
		FLeoExprPtr ParseMul(FLeoDiag& D) { return BinChain("*/%", "*/%", &FExprParser::ParseUnary, D); }

		// 同级多运算符链（+ - 与 * / %）
		FLeoExprPtr BinChain(const char* OpSet, const char* /*Codes*/, FLeoExprPtr (FExprParser::*Sub)(FLeoDiag&), FLeoDiag& D)
		{
			FLeoExprPtr L = (this->*Sub)(D);
			if (!L) { return nullptr; }
			while (!AtEnd() && Tokens[Pos].Type == ETok::Op && std::string_view(OpSet).find(Tokens[Pos].Text) != std::string_view::npos)
			{
				const char Code = Tokens[Pos].Text[0];
				++Pos;
				FLeoExprPtr R = (this->*Sub)(D);
				if (!R) { return nullptr; }
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Binary;
				N->BinOp = Code;
				N->Kids = { std::move(L), std::move(R) };
				L = std::move(N);
			}
			return L;
		}

		FLeoExprPtr ParseUnary(FLeoDiag& D)
		{
			if (EatOp("!") || EatOp("-"))
			{
				const char Code = Tokens[Pos - 1].Text[0];
				FLeoExprPtr K = ParseUnary(D);
				if (!K) { return nullptr; }
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Unary;
				N->UnOp = Code;
				N->Kids = { std::move(K) };
				return N;
			}
			return ParsePrimary(D);
		}

		FLeoExprPtr ParsePrimary(FLeoDiag& D)
		{
			if (AtEnd())
			{
				D = FLeoDiag::Make(ELeoDiag::E_BAD_EXPR, 0, "表达式意外结束（缺少操作数）");
				return nullptr;
			}
			const FTok& T = Tokens[Pos];
			if (T.Type == ETok::Op && T.Text == "(")
			{
				++Pos;
				FLeoExprPtr E = ParseOr(D);
				if (!E) { return nullptr; }
				if (!EatOp(")"))
				{
					D = FLeoDiag::Make(ELeoDiag::E_BAD_EXPR, 0, "缺少右括号");
					return nullptr;
				}
				return E;
			}
			if (T.Type == ETok::Int)
			{
				int64_t V = 0;
				if (!ParseInt64(T.Text, V))
				{
					D = FLeoDiag::Make(ELeoDiag::E_BAD_NUMBER, 0, "整数溢出: " + T.Text);
					return nullptr;
				}
				++Pos;
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Lit;
				N->Lit = FLeoValue::MakeInt(V);
				return N;
			}
			if (T.Type == ETok::Float)
			{
				double V = 0;
				if (!ParseDouble(T.Text, V))
				{
					D = FLeoDiag::Make(ELeoDiag::E_BAD_NUMBER, 0, "浮点数解析失败: " + T.Text);
					return nullptr;
				}
				++Pos;
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Lit;
				N->Lit = FLeoValue::MakeFloat(V);
				return N;
			}
			if (T.Type == ETok::Str)
			{
				++Pos;
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Lit;
				N->Lit = FLeoValue::MakeString(T.Text);
				return N;
			}
			if (T.Type == ETok::Word)
			{
				++Pos;
				if (T.Text == "true" || T.Text == "false")
				{
					auto N = Mk();
					N->Kind = FLeoExprNode::EKind::Lit;
					N->Lit = FLeoValue::MakeBool(T.Text == "true");
					return N;
				}
				if (!IsIdent(T.Text))
				{
					D = FLeoDiag::Make(ELeoDiag::E_BAD_EXPR, 0, "非法变量名: '" + T.Text + "'（仅 [A-Za-z_][A-Za-z0-9_]*）");
					return nullptr;
				}
				auto N = Mk();
				N->Kind = FLeoExprNode::EKind::Var;
				N->VarName = T.Text;
				return N;
			}
			D = FLeoDiag::Make(ELeoDiag::E_BAD_EXPR, 0, "意外的记号: '" + T.Text + "'");
			return nullptr;
		}
	};

	// ---------------- 编译上下文 ----------------
	struct FCompileCtx
	{
		FLeoProgram Prog;
		void AddDiag(ELeoDiag Code, int Line, std::string Msg)
		{
			Prog.Diags.push_back(FLeoDiag::Make(Code, Line, std::move(Msg)));
		}
		int AddExpr(FLeoExprPtr E)
		{
			Prog.ExprPool.push_back(std::move(E));
			return static_cast<int>(Prog.ExprPool.size()) - 1;
		}
	};

	const std::string* FindParam(const std::vector<FLeoParam>& Ps, const std::string& Key)
	{
		for (const FLeoParam& P : Ps) { if (P.Key == Key) { return &P.Value; } }
		return nullptr;
	}

	// 校验数值参数并写回（范围检查失败 → E_PARAM_VALUE）
	bool CheckNumParam(FCompileCtx& C, const std::vector<FLeoParam>& Ps, const std::string& Key,
	                   double Min, double Max, int Line)
	{
		const std::string* V = FindParam(Ps, Key);
		if (!V) { return true; }
		double D = 0;
		if (!ParseDouble(*V, D) || D < Min || D > Max)
		{
			char Buf[96];
			std::snprintf(Buf, sizeof(Buf), "参数 %s=%s 越界（要求 %.3g..%.3g）", Key.c_str(), V->c_str(), Min, Max);
			C.AddDiag(ELeoDiag::E_PARAM_VALUE, Line, Buf);
			return false;
		}
		return true;
	}

	// 从 tokens 的 Pos 起解析 key=value 尾参（位置参数已消费完）
	bool ParseParams(FCompileCtx& C, const std::vector<FTok>& T, size_t Pos, std::vector<FLeoParam>& Out, int Line)
	{
		while (Pos < T.size())
		{
			if (T[Pos].Type != ETok::Word)
			{
				C.AddDiag(ELeoDiag::E_BAD_PARAM, Line, "命名参数应为 key=value 形式，得到: '" + T[Pos].Text + "'");
				return false;
			}
			std::string Key = T[Pos].Text;
			++Pos;
			if (Pos >= T.size() || !(T[Pos].Type == ETok::Op && T[Pos].Text == "="))
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, Line, "参数 '" + Key + "' 缺少 =值");
				return false;
			}
			++Pos;
			if (Pos >= T.size() ||
				!(T[Pos].Type == ETok::Word || T[Pos].Type == ETok::Int || T[Pos].Type == ETok::Float || T[Pos].Type == ETok::Str))
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, Line, "参数 '" + Key + "' 的值非法");
				return false;
			}
			std::string Val = T[Pos].Text;
			++Pos;
			for (const FLeoParam& P : Out)
			{
				if (P.Key == Key)
				{
					C.AddDiag(ELeoDiag::E_BAD_PARAM, Line, "重复参数: " + Key);
					return false;
				}
			}
			Out.push_back({ std::move(Key), std::move(Val) });
		}
		return true;
	}

	void CheckAllowedParams(FCompileCtx& C, const std::vector<FLeoParam>& Ps,
	                        const std::vector<std::string>& Allowed, int Line)
	{
		for (const FLeoParam& P : Ps)
		{
			bool bFound = false;
			for (const std::string& A : Allowed) { if (A == P.Key) { bFound = true; break; } }
			if (!bFound)
			{
				C.AddDiag(ELeoDiag::E_BAD_PARAM, Line, "未知参数: " + P.Key);
			}
		}
	}

	// 位置参数与尾参的分界：首个 "Word =" 对
	size_t FindParamStart(const std::vector<FTok>& T, size_t From)
	{
		for (size_t i = From; i + 1 < T.size(); ++i)
		{
			if (T[i].Type == ETok::Word && T[i + 1].Type == ETok::Op && T[i + 1].Text == "=")
			{
				return i;
			}
		}
		return T.size();
	}

	int StoreExpr(FCompileCtx& C, const std::vector<FTok>& T, size_t Begin, size_t End, int Line, bool& bOk)
	{
		if (Begin >= End)
		{
			C.AddDiag(ELeoDiag::E_ARG_BAD, Line, "缺少表达式");
			bOk = false;
			return -1;
		}
		FLeoDiag D;
		FExprParser P(T, Begin, End);
		FLeoExprPtr E = P.Parse(D);
		if (!E)
		{
			D.Line = Line;
			C.Prog.Diags.push_back(D);
			bOk = false;
			return -1;
		}
		return C.AddExpr(std::move(E));
	}

	std::set<std::string>& CustomNameSet()
	{
		static std::set<std::string> S;
		return S;
	}
	std::map<std::string, FLeoCommandSpec>& CustomSpecMap()
	{
		static std::map<std::string, FLeoCommandSpec> M;
		return M;
	}
	const FLeoCommandSpec* FindCustomSpec(const std::string& Name)
	{
		const auto It = CustomSpecMap().find(Name);
		return It == CustomSpecMap().end() ? nullptr : &It->second;
	}
} // namespace

void SetCustomCommandSpecs(const std::vector<FLeoCommandSpec>& Specs)
{
	CustomSpecMap().clear();
	for (const FLeoCommandSpec& S : Specs)
	{
		CustomSpecMap()[S.Name] = S;
	}
	// 严格注册的命令同时从宽松名单移除，避免双注册歧义
	for (const auto& KV : CustomSpecMap())
	{
		CustomNameSet().erase(KV.first);
	}
}
void SetCustomCommandNames(const std::vector<std::string>& Names)
{
	CustomNameSet() = std::set<std::string>(Names.begin(), Names.end());
}
bool IsCustomCommandName(const std::string& Name)
{
	return CustomNameSet().count(Name) > 0 || CustomSpecMap().count(Name) > 0;
}

FLeoExprPtr CompileExprSrc(const std::string& ExprSrc, FLeoDiag& OutDiag)
{
	std::vector<FTok> T;
	FLeoDiag D;
	if (!LexLine(ExprSrc, T, D))
	{
		OutDiag = D;
		return nullptr;
	}
	FExprParser P(T, 0, T.size());
	FLeoExprPtr E = P.Parse(D);
	if (!E) { OutDiag = D; return nullptr; }
	return E;
}

// ---------------- 章节编译主流程 ----------------
FLeoProgram CompileChapter(const std::string& SourceUtf8, const std::string& SourceName)
{
	FCompileCtx C;
	C.Prog.SourceName = SourceName;

	bool bSawEnd = false;      // 全文件至少一个 end（E_MISSING_END）
	bool bInChoice = false;
	size_t ChoiceIdx = 0;
	auto CloseChoiceAt = [&](int AtLine)
	{
		if (bInChoice && C.Prog.Commands[ChoiceIdx].Options.empty())
		{
			C.AddDiag(ELeoDiag::E_EMPTY_CHOICE, AtLine, "choice 块内没有任何选项");
		}
		bInChoice = false;
	};

	// 切行（容忍 CRLF）
	std::vector<std::string> Lines;
	{
		size_t B = 0;
		const std::string& S = SourceUtf8;
		while (B <= S.size())
		{
			size_t E = S.find('\n', B);
			if (E == std::string::npos)
			{
				if (B < S.size()) { Lines.push_back(S.substr(B)); }
				break;
			}
			std::string L = S.substr(B, E - B);
			if (!L.empty() && L.back() == '\r') { L.pop_back(); }
			Lines.push_back(std::move(L));
			B = E + 1;
		}
	}

	int LineNo = 0;
	for (const std::string& Raw : Lines)
	{
		++LineNo;

		// Tab 硬错误（先于一切，包括注释——见 spec §2）
		if (Raw.find('\t') != std::string::npos)
		{
			C.AddDiag(ELeoDiag::E_TAB, LineNo, "出现 Tab 字符（缩进与文本一律用空格）");
			continue;
		}
		const std::string Line = RTrim(Raw);
		const size_t First = Line.find_first_not_of(' ');
		if (First == std::string::npos) { continue; } // 空行
		if (Line[First] == '#') { continue; }         // 整行注释
		if (First % 4 != 0)
		{
			C.AddDiag(ELeoDiag::E_INDENT, LineNo, "缩进空格数必须是 4 的倍数");
			continue;
		}
		const int Indent = static_cast<int>(First / 4);
		const std::string Content = Line.substr(First);

		if (bInChoice && Indent == 1)
		{
			if (FirstWord(Content) == "choice")
			{
				C.AddDiag(ELeoDiag::E_NESTED_CHOICE, LineNo, "choice 不可嵌套");
				continue;
			}
			// 选项行：以行内最后一个 " -> " 切分文本与目标（spec §5.2）
			const size_t Sep = Content.rfind(" -> ");
			if (Sep == std::string::npos)
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "choice 块内只允许选项行（缺少 ' -> '）: " + Content);
				continue;
			}
			std::string OptText = Trim(Content.substr(0, Sep));
			std::string Tail = LTrim(Content.substr(Sep + 4));
			if (OptText.empty())
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "选项显示文本为空");
				continue;
			}
			std::vector<FTok> T;
			FLeoDiag D;
			if (!LexLine(Tail, T, D))
			{
				D.Line = LineNo;
				C.Prog.Diags.push_back(D);
				continue;
			}
			if (T.empty() || T[0].Type != ETok::Word)
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "选项缺少目标 label");
				continue;
			}
			FLeoOption Opt;
			Opt.Text = std::move(OptText);
			Opt.TargetLabel = T[0].Text;
			if (!IsIdent(Opt.TargetLabel))
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "选项目标不是合法标识符: " + Opt.TargetLabel);
				continue;
			}
			size_t P = 1;
			if (P < T.size() && T[P].Type == ETok::Word && T[P].Text == "if")
			{
				++P;
				bool bOk = true;
				Opt.ExprIndex = StoreExpr(C, T, P, T.size(), LineNo, bOk);
				P = T.size();
			}
			if (P < T.size())
			{
				C.AddDiag(ELeoDiag::E_ARG_COUNT, LineNo, "选项行目标后只允许 'if <条件>'，多余内容: '" + T[P].Text + "'");
				continue;
			}
			C.Prog.Commands[ChoiceIdx].Options.push_back(std::move(Opt));
			continue;
		}
		if (bInChoice && Indent == 0)
		{
			CloseChoiceAt(LineNo);
		}
		if (Indent >= 1)
		{
			C.AddDiag(ELeoDiag::E_INDENT_LEVEL, LineNo,
				Indent == 1 ? "顶层命令不允许缩进（此处没有打开的 choice 块）" : "缩进层级过深（v0.1 只有 choice 一层块）");
			continue;
		}

		// ---------- 顶层命令 ----------
		const std::string Head = FirstWord(Content);
		const std::string Rest = (Head.size() == Content.size()) ? std::string() : LTrim(Content.substr(Head.size()));

		FLeoCommand Cmd;
		Cmd.Line = LineNo;

		if (Head == "text")
		{
			// 正文走原文路径，不做 token 切分（spec §5.1）
			std::string Speaker, Body;
			const size_t Sep = Rest.find(" | ");
			if (Sep != std::string::npos)
			{
				Speaker = Trim(Rest.substr(0, Sep));
				Body = LTrim(Rest.substr(Sep + 3));
			}
			else
			{
				Body = Rest;
			}
			if (Speaker == "-") { Speaker.clear(); }
			Body = RTrim(Body);
			if (Body.empty())
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "text 正文为空");
				continue;
			}
			Cmd.Kind = ELeoCmd::Text;
			Cmd.Speaker = std::move(Speaker);
			Cmd.Body = std::move(Body);
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}

		if (Head == "jumpif")
		{
			// 以第一个 " -> " 切分表达式与目标（spec §5 表）
			const size_t Sep = Rest.find(" -> ");
			if (Sep == std::string::npos)
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "jumpif 需要 ' -> ' 分隔表达式与目标 label");
				continue;
			}
			std::string ExprSrc = RTrim(Rest.substr(0, Sep));
			std::string Tail = LTrim(Rest.substr(Sep + 4));
			std::vector<FTok> ET, TT;
			FLeoDiag D;
			bool bOk = true;
			if (!LexLine(ExprSrc, ET, D))
			{
				D.Line = LineNo;
				C.Prog.Diags.push_back(D);
				continue;
			}
			if (!LexLine(Tail, TT, D))
			{
				D.Line = LineNo;
				C.Prog.Diags.push_back(D);
				continue;
			}
			if (TT.size() != 1 || TT[0].Type != ETok::Word || !IsIdent(TT[0].Text))
			{
				C.AddDiag(ELeoDiag::E_ARG_COUNT, LineNo, "jumpif 的 ' -> ' 之后必须是单个 label 名");
				continue;
			}
			Cmd.Kind = ELeoCmd::JumpIf;
			Cmd.Label = TT[0].Text;
			Cmd.ExprIndex = StoreExpr(C, ET, 0, ET.size(), LineNo, bOk);
			if (!bOk) { continue; }
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}

		// 其余命令：token 化后解析
		std::vector<FTok> T;
		FLeoDiag D;
		if (!LexLine(Content, T, D))
		{
			D.Line = LineNo;
			C.Prog.Diags.push_back(D);
			continue;
		}

		auto ExpectPosCount = [&](size_t MinPos, size_t MaxPos) -> bool
		{
			const size_t PStart = FindParamStart(T, 1);
			const size_t PosCount = PStart - 1;
			if (PosCount < MinPos || PosCount > MaxPos)
			{
				C.AddDiag(ELeoDiag::E_ARG_COUNT, LineNo, "位置参数个数不符（得到 " + std::to_string(PosCount) + "）");
				return false;
			}
			return true;
		};

		if (Head == "label")
		{
			if (!ExpectPosCount(1, 1)) { continue; }
			const std::string& Name = T[1].Text;
			if (T[1].Type != ETok::Word || !IsIdent(Name))
			{
				C.AddDiag(ELeoDiag::E_LABEL_NAME, LineNo, "label 名必须是标识符: '" + Name + "'");
				continue;
			}
			if (C.Prog.LabelIndex.count(Name) > 0)
			{
				C.AddDiag(ELeoDiag::E_DUP_LABEL, LineNo, "label 重名: " + Name);
				continue;
			}
			Cmd.Kind = ELeoCmd::Label;
			Cmd.Label = Name;
			C.Prog.LabelIndex[Name] = static_cast<int>(C.Prog.Commands.size());
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}

		if (Head == "jump")
		{
			if (!ExpectPosCount(1, 1)) { continue; }
			if (T[1].Type != ETok::Word || !IsIdent(T[1].Text))
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "跳转目标不是合法标识符: '" + T[1].Text + "'");
				continue;
			}
			Cmd.Kind = ELeoCmd::Jump;
			Cmd.Label = T[1].Text;
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}

		if (Head == "wait")
		{
			if (!ExpectPosCount(1, 1)) { continue; }
			int64_t Ms = 0;
			if (T[1].Type != ETok::Int || !ParseInt64(T[1].Text, Ms) || Ms < 0 || Ms > 600000)
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "wait 需要整数毫秒字面量（0..600000）: '" + T[1].Text + "'");
				continue;
			}
			Cmd.Kind = ELeoCmd::Wait;
			Cmd.Millis = static_cast<int>(Ms);
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}

		if (Head == "set" || Head == "setg")
		{
			// set <name> <op> <expr>；表达式中可能出现 "x == 1"（两个 token），不可走 FindParamStart
			if (T.size() < 4 || T[1].Type != ETok::Word || !IsIdent(T[1].Text) ||
				T[2].Type != ETok::Op ||
				(T[2].Text != "=" && T[2].Text != "+=" && T[2].Text != "-=" && T[2].Text != "*=" && T[2].Text != "/="))
			{
				C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, Head + " 需要: " + Head + " <变量> (=|+=|-=|*=|/=) <表达式>");
				continue;
			}
			bool bOk = true;
			Cmd.Kind = (Head == "set") ? ELeoCmd::Set : ELeoCmd::SetG;
			Cmd.Name = T[1].Text;
			Cmd.Op = T[2].Text;
			Cmd.ExprIndex = StoreExpr(C, T, 3, T.size(), LineNo, bOk);
			if (!bOk) { continue; }
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}

		// 演出类命令：位置参数 + 可选 key=value 尾参
		auto ParseShowCmd = [&](ELeoCmd Kind, size_t MinPos, size_t MaxPos,
		                        const std::vector<std::string>& AllowedParams) -> bool
		{
			if (!ExpectPosCount(MinPos, MaxPos)) { return false; }
			const size_t PStart = FindParamStart(T, 1);
			for (size_t i = 1; i < PStart; ++i)
			{
				// char/bgm 的 "-" 移除标记是 Op token，单独放行
				if (T[i].Type == ETok::Op && T[i].Text == "-" && (Kind == ELeoCmd::Char || Kind == ELeoCmd::Bgm) && i == PStart - 1)
				{
					continue;
				}
				// char 的第一个位置参数是槽位名（宽松 Word，允许 CJK），不做标识符校验
				if (Kind == ELeoCmd::Char && i == 1)
				{
					if (T[i].Type != ETok::Word)
					{
						C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "char 槽位名应为普通词: '" + T[i].Text + "'");
						return false;
					}
					continue;
				}
				if (T[i].Type != ETok::Word || !IsIdent(T[i].Text))
				{
					C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "位置参数应为逻辑名标识符: '" + T[i].Text + "'");
					return false;
				}
			}
			if (!ParseParams(C, T, PStart, Cmd.Params, LineNo)) { return false; }
			CheckAllowedParams(C, Cmd.Params, AllowedParams, LineNo);
			Cmd.Kind = Kind;
			return true;
		};

		if (Head == "bg")
		{
			if (ParseShowCmd(ELeoCmd::Bg, 1, 1, { "transition", "duration" }))
			{
				Cmd.AssetId = T[1].Text;
				CheckNumParam(C, Cmd.Params, "duration", 0, 3600, LineNo);
				C.Prog.Commands.push_back(std::move(Cmd));
			}
			continue;
		}
		if (Head == "char")
		{
			if (ParseShowCmd(ELeoCmd::Char, 2, 2, { "at", "pose", "motion" }))
			{
				Cmd.Slot = T[1].Text;
				Cmd.AssetId = T[2].Text;
				if (const std::string* At = FindParam(Cmd.Params, "at"))
				{
					if (*At != "left" && *At != "center" && *At != "right")
					{
						C.AddDiag(ELeoDiag::E_PARAM_VALUE, LineNo, "at 只允许 left|center|right: " + *At);
					}
				}
				C.Prog.Commands.push_back(std::move(Cmd));
			}
			continue;
		}
		if (Head == "bgm")
		{
			if (ParseShowCmd(ELeoCmd::Bgm, 1, 1, { "volume", "fade" }))
			{
				Cmd.AssetId = T[1].Text;
				CheckNumParam(C, Cmd.Params, "volume", 0, 1, LineNo);
				CheckNumParam(C, Cmd.Params, "fade", 0, 60, LineNo);
				C.Prog.Commands.push_back(std::move(Cmd));
			}
			continue;
		}
		if (Head == "se")
		{
			if (ParseShowCmd(ELeoCmd::Se, 1, 1, { "volume" }))
			{
				Cmd.AssetId = T[1].Text;
				CheckNumParam(C, Cmd.Params, "volume", 0, 1, LineNo);
				C.Prog.Commands.push_back(std::move(Cmd));
			}
			continue;
		}
		if (Head == "voice")
		{
			if (ParseShowCmd(ELeoCmd::Voice, 1, 1, {}))
			{
				Cmd.AssetId = T[1].Text;
				C.Prog.Commands.push_back(std::move(Cmd));
			}
			continue;
		}
		if (Head == "choice")
		{
			if (T.size() != 1)
			{
				C.AddDiag(ELeoDiag::E_ARG_COUNT, LineNo, "choice 不带参数（选项写在缩进的下一层）");
				continue;
			}
			Cmd.Kind = ELeoCmd::Choice;
			ChoiceIdx = C.Prog.Commands.size();
			bInChoice = true;
			C.Prog.Commands.push_back(std::move(Cmd));
			continue;
		}
		if (Head == "end")
		{
			if (T.size() != 1)
			{
				C.AddDiag(ELeoDiag::E_ARG_COUNT, LineNo, "end 不带参数");
				continue;
			}
			Cmd.Kind = ELeoCmd::End;
			C.Prog.Commands.push_back(std::move(Cmd));
			bSawEnd = true; // end = 该执行路径终止；允许多个（多分支章节），解析继续
			continue;
		}
		if (IsCustomCommandName(Head))
		{
			// 自定义命令：有 spec 走严格校验（编辑期报错带行号），无 spec 走宽松解析
			Cmd.Kind = ELeoCmd::Custom;
			Cmd.CustomName = Head;
			const FLeoCommandSpec* Spec = FindCustomSpec(Head);
			bool bOk = true;
			if (Spec)
			{
				const size_t PStart = FindParamStart(T, 1);
				const size_t PosCount = PStart - 1;
				if (PosCount < static_cast<size_t>(Spec->MinArgs) ||
				    (Spec->MaxArgs >= 0 && PosCount > static_cast<size_t>(Spec->MaxArgs)))
				{
					C.AddDiag(ELeoDiag::E_ARG_COUNT, LineNo,
						Head + " 需要 " + std::to_string(Spec->MinArgs) + ".." +
						(Spec->MaxArgs < 0 ? std::string("N") : std::to_string(Spec->MaxArgs)) +
						" 个位置参数（得到 " + std::to_string(PosCount) + "）");
					continue;
				}
				for (size_t i = 1; i < PStart; ++i)
				{
					if (T[i].Type == ETok::Word || T[i].Type == ETok::Int || T[i].Type == ETok::Float || T[i].Type == ETok::Str)
					{
						Cmd.CustomArgs.push_back(T[i].Text);
					}
					else
					{
						C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "位置参数类型非法: '" + T[i].Text + "'");
						bOk = false;
					}
				}
				if (bOk && !ParseParams(C, T, PStart, Cmd.Params, LineNo)) { bOk = false; }
				if (bOk)
				{
					const size_t DiagCountBefore = C.Prog.Diags.size();
					CheckAllowedParams(C, Cmd.Params, Spec->AllowedParams, LineNo);
					bOk = C.Prog.Diags.size() == DiagCountBefore;
				}
			}
			else
			{
				// 宽松：位置参数原文 + 任意 key=value
				for (size_t i = 1; i < T.size(); ++i)
				{
					if (i + 2 < T.size() && T[i].Type == ETok::Word &&
						T[i + 1].Type == ETok::Op && T[i + 1].Text == "=" &&
						(T[i + 2].Type == ETok::Word || T[i + 2].Type == ETok::Int || T[i + 2].Type == ETok::Float || T[i + 2].Type == ETok::Str))
					{
						FLeoParam P;
						P.Key = T[i].Text;
						P.Value = T[i + 2].Text;
						Cmd.Params.push_back(std::move(P));
						i += 2;
						continue;
					}
					if (T[i].Type == ETok::Word || T[i].Type == ETok::Int || T[i].Type == ETok::Float || T[i].Type == ETok::Str)
					{
						Cmd.CustomArgs.push_back(T[i].Text);
					}
					else
					{
						C.AddDiag(ELeoDiag::E_ARG_BAD, LineNo, "自定义命令参数非法: '" + T[i].Text + "'");
					}
				}
			}
			if (bOk)
			{
				C.Prog.Commands.push_back(std::move(Cmd));
			}
			continue;
		}

		C.AddDiag(ELeoDiag::E_UNKNOWN_CMD, LineNo, "未知命令: " + Head);
	}

	// EOF 收尾
	CloseChoiceAt(LineNo + 1);
	if (!bSawEnd)
	{
		C.AddDiag(ELeoDiag::E_MISSING_END, LineNo, "缺少 end（全文件至少一个）");
	}

	// ---------- 后置校验：跳转目标回填 + 警告 ----------
	std::set<std::string> ReferencedLabels;
	for (FLeoCommand& Cmd : C.Prog.Commands)
	{
		if (Cmd.Kind == ELeoCmd::Jump || Cmd.Kind == ELeoCmd::JumpIf)
		{
			auto It = C.Prog.LabelIndex.find(Cmd.Label);
			if (It == C.Prog.LabelIndex.end())
			{
				C.AddDiag(ELeoDiag::E_UNDEF_LABEL, Cmd.Line, "未定义的 label: " + Cmd.Label);
			}
			else
			{
				Cmd.TargetIndex = It->second;
				ReferencedLabels.insert(Cmd.Label);
			}
		}
		else if (Cmd.Kind == ELeoCmd::Choice)
		{
			for (FLeoOption& Opt : Cmd.Options)
			{
				auto It = C.Prog.LabelIndex.find(Opt.TargetLabel);
				if (It == C.Prog.LabelIndex.end())
				{
					C.AddDiag(ELeoDiag::E_UNDEF_LABEL, Cmd.Line, "选项目标 label 不存在: " + Opt.TargetLabel);
				}
				else
				{
					Opt.TargetIndex = It->second;
					ReferencedLabels.insert(Opt.TargetLabel);
				}
			}
		}
	}
	for (const auto& KV : C.Prog.LabelIndex)
	{
		if (ReferencedLabels.count(KV.first) == 0)
		{
			C.AddDiag(ELeoDiag::W_UnusedLabel, C.Prog.Commands[KV.second].Line, "label 从未被引用: " + KV.first);
		}
	}
	for (size_t i = 0; i + 1 < C.Prog.Commands.size(); ++i)
	{
		// 无条件跳转/终止后的命令不可达（除非有 label 落在其上供跳入）
		const bool bUnconditionalHalt =
			C.Prog.Commands[i].Kind == ELeoCmd::Jump || C.Prog.Commands[i].Kind == ELeoCmd::End;
		if (bUnconditionalHalt && C.Prog.Commands[i + 1].Kind != ELeoCmd::Label)
		{
			C.AddDiag(ELeoDiag::W_Unreachable, C.Prog.Commands[i + 1].Line,
				"无条件跳转/终止后的命令不可达（如需落在此处请加 label）");
		}
	}

	C.Prog.Ok = true;
	for (const FLeoDiag& Dd : C.Prog.Diags)
	{
		if (IsLeoError(Dd.Code)) { C.Prog.Ok = false; break; }
	}
	return C.Prog;
}

} // namespace leo
