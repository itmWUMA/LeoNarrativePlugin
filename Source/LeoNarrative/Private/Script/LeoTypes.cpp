// 值模型与诊断码的纯 C++ 实现
#include "Script/LeoTypes.h"

namespace leo
{

bool FLeoValue::operator==(const FLeoValue& Other) const
{
	// 严格同型比较；跨 Int/Float 的数值比较由求值器先行提升后调用
	if (Kind != Other.Kind) { return false; }
	switch (Kind)
	{
	case EKind::Null:   return true;
	case EKind::Bool:   return B == Other.B;
	case EKind::Int:    return I == Other.I;
	case EKind::Float:  return F == Other.F;
	case EKind::String: return S == Other.S;
	}
	return false;
}

std::string FLeoValue::ToString() const
{
	char Buf[48];
	switch (Kind)
	{
	case EKind::Null:   return "null";
	case EKind::Bool:   return B ? "true" : "false";
	case EKind::Int:
		std::snprintf(Buf, sizeof(Buf), "%lld", static_cast<long long>(I));
		return Buf;
	case EKind::Float:
		std::snprintf(Buf, sizeof(Buf), "%g", F);
		return Buf;
	case EKind::String: return "\"" + S + "\"";
	}
	return "?";
}

const char* LeoDiagName(ELeoDiag Code)
{
	switch (Code)
	{
	case ELeoDiag::Ok:             return "Ok";
	case ELeoDiag::E_TAB:          return "E_TAB";
	case ELeoDiag::E_INDENT:       return "E_INDENT";
	case ELeoDiag::E_INDENT_LEVEL: return "E_INDENT_LEVEL";
	case ELeoDiag::E_UNKNOWN_CMD:  return "E_UNKNOWN_CMD";
	case ELeoDiag::E_ARG_COUNT:    return "E_ARG_COUNT";
	case ELeoDiag::E_ARG_BAD:      return "E_ARG_BAD";
	case ELeoDiag::E_BAD_PARAM:    return "E_BAD_PARAM";
	case ELeoDiag::E_PARAM_VALUE:  return "E_PARAM_VALUE";
	case ELeoDiag::E_BAD_NUMBER:   return "E_BAD_NUMBER";
	case ELeoDiag::E_UNTERM_STRING:return "E_UNTERM_STRING";
	case ELeoDiag::E_BAD_ESCAPE:   return "E_BAD_ESCAPE";
	case ELeoDiag::E_BAD_TOKEN:    return "E_BAD_TOKEN";
	case ELeoDiag::E_BAD_EXPR:     return "E_BAD_EXPR";
	case ELeoDiag::E_LABEL_NAME:   return "E_LABEL_NAME";
	case ELeoDiag::E_DUP_LABEL:    return "E_DUP_LABEL";
	case ELeoDiag::E_UNDEF_LABEL:  return "E_UNDEF_LABEL";
	case ELeoDiag::E_EMPTY_CHOICE: return "E_EMPTY_CHOICE";
	case ELeoDiag::E_NESTED_CHOICE:return "E_NESTED_CHOICE";
	case ELeoDiag::E_MISSING_END:  return "E_MISSING_END";
	case ELeoDiag::E_IO:           return "E_IO";
	case ELeoDiag::E_BAD_TEXT_ID:  return "E_BAD_TEXT_ID";
	case ELeoDiag::E_DUP_TEXT_ID:  return "E_DUP_TEXT_ID";
	case ELeoDiag::E_UNDEF_VAR:    return "E_UNDEF_VAR";
	case ELeoDiag::E_TYPE:         return "E_TYPE";
	case ELeoDiag::E_DIV_ZERO:     return "E_DIV_ZERO";
	case ELeoDiag::W_Unreachable:  return "W_UNREACHABLE";
	case ELeoDiag::W_UnusedLabel:  return "W_UNUSED_LABEL";
	}
	return "?";
}

} // namespace leo
