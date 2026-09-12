// LeoValidate：.leo 剧本 headless 校验命令行（CI 可用，退出码非 0 = 有失败）
// 用法: UnrealEditor-Cmd.exe <proj> -run=LeoValidate -stdout -unattended -nopause -nosplash
#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "LeoValidateCommandlet.generated.h"

UCLASS()
class ULeoValidateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
