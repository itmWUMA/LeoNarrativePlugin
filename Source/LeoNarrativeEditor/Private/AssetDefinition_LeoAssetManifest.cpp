#include "AssetDefinition_LeoAssetManifest.h"

#include "Data/LeoAssetManifest.h"
#include "LeoManifestEditorToolkit.h"

#include "Toolkits/IToolkitHost.h"

#define LOCTEXT_NAMESPACE "AssetDefinition_LeoAssetManifest"

FText UAssetDefinition_LeoAssetManifest::GetAssetDisplayName() const
{
	return LOCTEXT("AssetName", "叙事资产清单");
}

FLinearColor UAssetDefinition_LeoAssetManifest::GetAssetColor() const
{
	// 暖橙（与编排图海水蓝区分）
	return FLinearColor(0.85f, 0.55f, 0.15f);
}

TSoftClassPtr<UObject> UAssetDefinition_LeoAssetManifest::GetAssetClass() const
{
	return ULeoAssetManifest::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_LeoAssetManifest::GetAssetCategories() const
{
	static const auto Categories =
	{
		FAssetCategoryPath(EAssetCategoryPaths::Gameplay, LOCTEXT("NarrativeSection", "Narrative"), ECategoryMenuType::Section)
	};
	return Categories;
}

EAssetCommandResult UAssetDefinition_LeoAssetManifest::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UObject* Obj : OpenArgs.LoadObjects<UObject>())
	{
		if (ULeoAssetManifest* Manifest = Cast<ULeoAssetManifest>(Obj))
		{
			const TSharedRef<FLeoManifestEditorToolkit> Toolkit = MakeShared<FLeoManifestEditorToolkit>();
			Toolkit->InitLeoManifestEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Manifest);
		}
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
