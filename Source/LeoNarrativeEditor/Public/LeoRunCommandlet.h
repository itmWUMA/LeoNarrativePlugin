// 无头运行器：命令行环境里创建 GameInstance + World，泵帧驱动叙事会话。
// 用途：CI 回归（不依赖编辑器启动/PIE，绕开编辑器环境问题）。
//   UnrealEditor-Cmd <proj> -run=LeoRun -exec="leo.autotest chapter01" -seconds=30
#pragma once

#include "Commandlets/Commandlet.h"
#include "LeoRunCommandlet.generated.h"

UCLASS()
class ULeoRunCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	ULeoRunCommandlet();
	virtual int32 Main(const FString& Params) override;
};
