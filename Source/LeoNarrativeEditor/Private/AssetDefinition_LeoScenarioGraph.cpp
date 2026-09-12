#include "AssetDefinition_LeoScenarioGraph.h"

#include "Data/LeoScenarioGraph.h"
#include "LeoGraphEditorToolkit.h"

#include "Toolkits/IToolkitHost.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_LeoScenarioGraph"

FText UAssetDefinition_LeoScenarioGraph::GetAssetDisplayName() const
{
	return LOCTEXT("AssetName", "Leo 编排图");
}

FLinearColor UAssetDefinition_LeoScenarioGraph::GetAssetColor() const
{
	// 海水蓝（ocean blue #0077B6 附近）
	return FLinearColor(0.0f, 0.466f, 0.713f);
}

TSoftClassPtr<UObject> UAssetDefinition_LeoScenarioGraph::GetAssetClass() const
{
	return ULeoScenarioGraph::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_LeoScenarioGraph::GetAssetCategories() const
{
	// Gameplay 下平铺（同 GameplayCameras 的 Camera：Section = 分节标题，不折叠成子菜单）
	static const auto Categories =
	{
		FAssetCategoryPath(EAssetCategoryPaths::Gameplay, LOCTEXT("NarrativeSection", "Narrative"), ECategoryMenuType::Section)
	};
	return Categories;
}

EAssetCommandResult UAssetDefinition_LeoScenarioGraph::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UObject* Obj : OpenArgs.LoadObjects<UObject>())
	{
		if (ULeoScenarioGraph* Graph = Cast<ULeoScenarioGraph>(Obj))
		{
			const TSharedRef<FLeoGraphEditorToolkit> Toolkit = MakeShared<FLeoGraphEditorToolkit>();
			Toolkit->InitLeoGraphEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Graph);
		}
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
