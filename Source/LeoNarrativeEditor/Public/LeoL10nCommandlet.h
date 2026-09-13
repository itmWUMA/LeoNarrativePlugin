// LeoL10n：剧本台词本地化命令行（提取译文 CSV / 冻结显式 ID / 自测）。
// 用法（UnrealEditor-Cmd.exe <proj> -run=LeoL10n ...）:
//   -action=extract -culture=en[,ja,...]  .leo → Content/L10n/<culture>/<章节>.csv（合并语义，不覆盖译文）
//   -action=freeze  [-chapter=<名>]       显式 id= 写回 .leo（送翻前冻结编号；空缺默认全部章节）
//   -selftest                             CSV 往返 + 合并语义 + ID 拼法自测
#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "LeoL10nCommandlet.generated.h"

UCLASS()
class ULeoL10nCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
