// ULeoAssetManifest 的现代资产定义（5.8 UAssetDefinition 体系）：
// 与编排图同一入口 Gameplay → Narrative；双击打开专属清单编辑器。
#pragma once

#include "AssetDefinition.h"
#include "AssetDefinition_LeoAssetManifest.generated.h"

UCLASS()
class UAssetDefinition_LeoAssetManifest : public UAssetDefinition
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
