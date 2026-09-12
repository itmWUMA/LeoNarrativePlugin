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

// 自定义命令的编译期规格（严格模式）：违反即编辑期报错，带行号。
// 保持"载入时编译"对 AI 创作的价值——扩展命令同样拿到前置校验。
struct FLeoCommandSpec
{
	std::string Name;
	int MinArgs = 0;                      // 位置参数下限
	int MaxArgs = 0;                      // 位置参数上限；-1 = 不限
	std::vector<std::string> AllowedParams; // 命名参数白名单；空 = 不允许任何命名参数
};

// 严格注册：按 spec 校验参数（E_ARG_COUNT / E_BAD_PARAM / E_ARG_BAD）
void SetCustomCommandSpecs(const std::vector<FLeoCommandSpec>& Specs);

// 宽松注册（兼容旧接口）：只登记命令名，参数不校验。
// 非线程安全：约定只在启动期调用。
void SetCustomCommandNames(const std::vector<std::string>& Names);

// 查询某名字是否为已注册的自定义命令
bool IsCustomCommandName(const std::string& Name);

} // namespace leo
