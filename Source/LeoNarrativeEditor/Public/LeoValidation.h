// LeoNarrative 编辑器模块的共享校验入口：命令行 / 菜单 / 文件监听 三处复用
#pragma once

#include "CoreMinimal.h"

namespace LeoValidation
{
	// 编译单个 .leo 文件并输出诊断。bExpectClean=true 时期待零错误（正常剧本），
	// false 时期待有错误（golden fail 语料）。返回是否 符合预期。
	bool ValidateFile(const FString& Path, bool bExpectClean);

	// 校验全部剧本源：
	//   <工程>/Content/Scripts/*.leo          —— 必须零错误
	//   <插件>/tests/golden/pass/*.leo        —— 必须零错误
	//   <插件>/tests/golden/fail/*.leo        —— 必须有错误（回归错误检测能力）
	// 返回不符合预期的文件数（0 = 全绿）。
	int32 ValidateAll();
}
