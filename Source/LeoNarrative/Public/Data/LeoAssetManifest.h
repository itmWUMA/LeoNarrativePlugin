// 逻辑名清单资产：脚本里的 bgm_daily01 等逻辑名 → UE 资产软路径
// 脚本永不出现资产路径（铁律 #2）；清单资产在编辑器里创建并交给子系统
#pragma once

#include "Engine/DataAsset.h"
#include "LeoAssetManifest.generated.h"

UCLASS()
class LEONARRATIVE_API ULeoAssetManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Leo")
	TMap<FName, FSoftObjectPath> Assets;

	bool TryResolve(FName LogicalId, FSoftObjectPath& OutPath) const
	{
		if (const FSoftObjectPath* P = Assets.Find(LogicalId))
		{
			OutPath = *P;
			return true;
		}
		return false;
	}
};
