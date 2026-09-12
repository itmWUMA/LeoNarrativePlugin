// LeoNarrative .leo 编译器内核 —— 纯 C++，禁止 include 任何 UE 头文件（架构铁律 #1）
// 对应规范：Plugins/LeoNarrative/docs/leo-spec.md §6.1 / §9
#pragma once

#include <cstdint>
#include <string>

namespace leo
{

// ---- 值模型：黑板与表达式共用的动态值（结构体而非 std::variant，规避 UE 构建禁用异常）----
struct FLeoValue
{
	enum class EKind : uint8_t { Null, Bool, Int, Float, String };

	EKind Kind = EKind::Null;
	bool B = false;
	int64_t I = 0;
	double F = 0.0;
	std::string S;

	static FLeoValue MakeBool(bool InB) { FLeoValue V; V.Kind = EKind::Bool; V.B = InB; return V; }
	static FLeoValue MakeInt(int64_t InI) { FLeoValue V; V.Kind = EKind::Int; V.I = InI; return V; }
	static FLeoValue MakeFloat(double InF) { FLeoValue V; V.Kind = EKind::Float; V.F = InF; return V; }
	static FLeoValue MakeString(std::string InS) { FLeoValue V; V.Kind = EKind::String; V.S = std::move(InS); return V; }

	bool IsNumber() const { return Kind == EKind::Int || Kind == EKind::Float; }
	// 数值统一提升为 double 参与比较与算术
	double AsDouble() const { return Kind == EKind::Float ? F : (Kind == EKind::Int ? static_cast<double>(I) : 0.0); }

	bool operator==(const FLeoValue& Other) const;
	bool operator!=(const FLeoValue& Other) const { return !(*this == Other); }

	// 调试/日志用可读形式
	std::string ToString() const;
};

// ---- 诊断码：与 spec §9 错误码表一一对应 ----
enum class ELeoDiag : uint8_t
{
	Ok = 0,
	// 编译期错误
	E_TAB, E_INDENT, E_INDENT_LEVEL,
	E_UNKNOWN_CMD, E_ARG_COUNT, E_ARG_BAD, E_BAD_PARAM, E_PARAM_VALUE,
	E_BAD_NUMBER, E_UNTERM_STRING, E_BAD_ESCAPE, E_BAD_TOKEN, E_BAD_EXPR,
	E_LABEL_NAME, E_DUP_LABEL, E_UNDEF_LABEL,
	E_EMPTY_CHOICE, E_NESTED_CHOICE, E_MISSING_END, E_AFTER_END, E_IO,
	// 运行时错误
	E_UNDEF_VAR, E_TYPE, E_DIV_ZERO,
	// 警告
	W_Unreachable, W_UnusedLabel,
};

// 诊断码的标准名，如 "E_TAB"
const char* LeoDiagName(ELeoDiag Code);
inline bool IsLeoError(ELeoDiag Code) { return Code != ELeoDiag::Ok && Code != ELeoDiag::W_Unreachable && Code != ELeoDiag::W_UnusedLabel; }

struct FLeoDiag
{
	ELeoDiag Code = ELeoDiag::Ok;
	int Line = 0;          // 1 起始；运行时诊断填命令行号
	std::string Msg;       // 人类可读细节（中文）

	static FLeoDiag Make(ELeoDiag InCode, int InLine, std::string InMsg)
	{
		FLeoDiag D; D.Code = InCode; D.Line = InLine; D.Msg = std::move(InMsg); return D;
	}
};

} // namespace leo
