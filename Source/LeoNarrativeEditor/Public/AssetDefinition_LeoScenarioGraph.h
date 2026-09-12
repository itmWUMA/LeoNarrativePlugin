// ULeoScenarioGraph 的现代资产定义（5.8 UAssetDefinition 体系）：
// Content 右键创建入口位于 Gameplay → Narrative 子菜单；资产颜色海水蓝。
// 取代旧的 FAssetTypeActions（其分类只支持平级，无法做子菜单）。
#pragma once

#include "AssetDefinition.h"
#include "AssetDefinition_LeoScenarioGraph.generated.h"

UCLASS()
class UAssetDefinition_LeoScenarioGraph : public UAssetDefinition
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
