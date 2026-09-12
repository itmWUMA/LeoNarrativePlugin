// ScriptRuntime 桥：把纯 C++ 编译器内核以 LEONARRATIVE_API 导出给其它模块
// （编辑器校验工具、未来的调试器都从这里进，不让 Script/ 头文件沾 UE 宏）
#pragma once

#include "CoreMinimal.h"
#include "Script/LeoCompiler.h"
#include "Script/LeoProgram.h"
#include "Script/LeoTypes.h"

namespace LeoBridge
{
	// FString(UTF-16) ↔ std::string(UTF-8)
	LEONARRATIVE_API std::string ToUtf8(const FString& Str);
	LEONARRATIVE_API FString ToFString(const std::string& Utf8);

	// 编译一章（源文本为 UTF-8 字符串）
	LEONARRATIVE_API leo::FLeoProgram CompileChapter(const FString& SourceUtf8, const FString& SourceName);

	// 单独编译表达式（ScenarioGraph 边条件等）
	LEONARRATIVE_API leo::FLeoExprPtr CompileExpr(const FString& ExprSrc, leo::FLeoDiag& OutDiag);

	// 诊断码名的 TCHAR 形式（日志/显示用）
	LEONARRATIVE_API const TCHAR* DiagName(leo::ELeoDiag Code);

	// 编译前注册自定义命令名
	LEONARRATIVE_API void SetCustomCommandNames(const TArray<FString>& Names);
}
