// 边条件的结构化编解码（编辑器模块内）：
// 表达式串仍是唯一存储与运行时语义（LeoGraphEval 零改动），"下拉模式"只是对常见子集的双向映射——
// 顶层 && 链，每段为「变量 比较符 字面量」或「裸变量 / !变量」。任何更复杂的形态（||、嵌套、
// 变量对变量、字面量在左）解析失败，调用方留在自定义表达式模式。
#pragma once

#include "CoreMinimal.h"
#include "Script/LeoTypes.h"

enum class ELeoCondOp : uint8
{
	IsTrue,  // 裸变量
	IsFalse, // !变量
	Eq, Ne, Ge, Gt, Le, Lt,
};

// 一行条件 = 变量 + 比较符 + 字面量值（IsTrue/IsFalse 无值）
struct FLeoCondRow
{
	FName Var;
	ELeoCondOp Op = ELeoCondOp::IsTrue;
	FString Value; // 字面量源文本："3" / "2.5" / "true" / "\"text\""
};

// 比较符条目：Syntax 用于生成表达式，Display 用于下拉显示
struct FLeoCondOpInfo
{
	ELeoCondOp Op;
	const TCHAR* Syntax;
	const TCHAR* Display;
};

namespace LeoConditionCodec
{
	// 全部比较符（下拉数据源，顺序即显示序）
	const TArray<FLeoCondOpInfo>& GetOps();
	const FLeoCondOpInfo* FindOp(ELeoCondOp Op);

	// 值 → .leo 字面量源文本（条件行值与沙箱值显示共用；FLeoValue::ToString 未跨模块导出）
	bool ValueToSource(const leo::FLeoValue& V, FString& Out);

	// 表达式 → 条件行；空串 = 0 行（恒真，顺序/默认边）。不合规形态返回 false。
	bool Parse(const FString& Expr, TArray<FLeoCondRow>& OutRows);

	// 条件行 → 表达式（0 行 = 空串）
	FString Generate(const TArray<FLeoCondRow>& Rows);

	// 画布摘要：空 = "无条件"；可拆行走行摘要；否则原文截断
	FString Summarize(const FString& Expr);
}
