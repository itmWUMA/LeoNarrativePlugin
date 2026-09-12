// .leo 编译入口（纯 C++）
// 对应规范：docs/leo-spec.md
#pragma once

#include "Script/LeoExpr.h"
#include "Script/LeoProgram.h"
#include "Script/LeoTypes.h"
#include <string>
#include <vector>

namespace leo
{

// 编译一章剧本。SourceUtf8 为 UTF-8 源文本；SourceName 用于诊断与文本 ID 前缀。
// 返回的 FLeoProgram 含全部诊断；Ok == true 才可交给 VM 执行。
FLeoProgram CompileChapter(const std::string& SourceUtf8, const std::string& SourceName);

// 单独编译一段表达式（ScenarioGraph 边条件等复用）。失败返回 nullptr 并写 OutDiag。
FLeoExprPtr CompileExprSrc(const std::string& ExprSrc, FLeoDiag& OutDiag);

// 注册自定义命令名（须在 CompileChapter 之前调用；语法按"位置参数 + key=value"通用规则解析）。
// 非线程安全：约定只在启动期调用。
void SetCustomCommandNames(const std::vector<std::string>& Names);

// 查询某名字是否为已注册的自定义命令
bool IsCustomCommandName(const std::string& Name);

} // namespace leo
