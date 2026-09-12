// .leo 表达式 AST 与求值器（纯 C++）
// 对应规范：docs/leo-spec.md §6
#pragma once

#include "Script/LeoTypes.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace leo
{

// 表达式节点：编译期构建一次，运行期反复求值。不做 Visitor——按 Kind switch 直解。
struct FLeoExprNode
{
	enum class EKind : uint8_t { Lit, Var, Unary, Binary };

	EKind Kind = EKind::Lit;
	FLeoValue Lit;                 // Lit：字面量
	std::string VarName;           // Var：黑板变量名
	char UnOp = 0;                 // Unary：'!' 或 '-'
	char BinOp = 0;                // Binary：'+' '-' '*' '/' '%' '<' '>' 'L'(<=) 'G'(>=) 'E'(==) 'N'(!=) 'A'&& 'O'||
	std::vector<std::shared_ptr<const FLeoExprNode>> Kids; // Unary 1 个，Binary 2 个
};

using FLeoExprPtr = std::shared_ptr<const FLeoExprNode>;

// 变量解析回调：未定义返回 false（由 UE 侧黑板适配层提供，局部→全局链）
using FLeoVarResolver = std::function<bool(const std::string&, FLeoValue&)>;

// 求值：成功返回 true；失败时 OutErr/OutMsg 描述运行时错误（E_UNDEF_VAR/E_TYPE/E_DIV_ZERO）
bool LeoEval(const FLeoExprNode& Root, const FLeoVarResolver& Resolver,
             FLeoValue& Out, ELeoDiag& OutErr, std::string& OutMsg);

} // namespace leo
