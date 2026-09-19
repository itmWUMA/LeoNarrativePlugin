// 叙事资产清单专属编辑器（仿 BT 黑板编辑器的行式体验）：
// 单 Tab 宿纳——顶部工具条（+类别/+条目/删除/搜索）｜左类别列表（计数徽章）｜
// 右类别头（类名/期望类声明）+ 条目行列表（状态点 + 逻辑名 + 资产路径 + 删除）。
// 编辑模型：清单资产唯一事实源，无镜像层；一切变更走 FScopedTransaction（Ctrl+Z 可撤），
// 撤销后从资产重建列表。即时反馈：资产失效红 / 期望类不符黄 / 跨节重复黄（AssetRegistry 查询，
// 刷新时计算不逐帧）。拖放：Content Browser 资产拖到条目区 = 按对象名批量入库当前类别；
// 资产列用引擎标准对象选择器（缩略图+下拉资产树+拖放），选错不硬拦（状态点黄提示）。
#pragma once

#include "CoreMinimal.h"
#include "Data/LeoAssetManifest.h"
#include "EditorUndoClient.h"
#include "Toolkits/AssetEditorToolkit.h"

class SSearchBox;
struct FAssetData;
class FAssetThumbnailPool;
template <typename ItemType> class SListView;

class FLeoManifestEditorToolkit : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
	void InitLeoManifestEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& Host,
		ULeoAssetManifest* InManifest);
	virtual ~FLeoManifestEditorToolkit();

	// ---- FAssetEditorToolkit ----
	virtual FName GetToolkitFName() const override { return FName(TEXT("LeoManifestEditor")); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("LeoManifest"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.9f, 0.6f, 0.2f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	// ---- FEditorUndoClient：资产已被事务还原 → 重建列表 ----
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	// 条目行的即时状态（刷新时经 AssetRegistry 计算）
	enum class EEntryStatus : uint8 { Ok, MissingAsset, WrongType, DupKey };

	struct FEntryRow
	{
		FName Category;             // 所属类别节名（搜索跨节时行来源不同）
		FName Id;
		FSoftObjectPath Path;
		EEntryStatus Status = EEntryStatus::Ok;
		FText StatusTip;            // 状态点 tooltip
	};
	struct FCategoryRow
	{
		FName Name;
		int32 Count = 0;
		bool bBuiltIn = false;      // 内置类别（名不可改——命令语义绑定）
	};

	TSharedRef<SWidget> MakeManifestTab();
	TSharedRef<ITableRow> MakeCategoryRow(TSharedPtr<FCategoryRow> Row, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeEntryRowWidget(TSharedPtr<FEntryRow> Row, const TSharedRef<STableViewBase>& Owner);

	// ---- 增删改（全部走事务）----
	void AddCategory();
	void DeleteSelectedCategory();
	void AddEntry();
	void DeleteSelectedEntries();
	void OnCategoryNameCommitted(const FText& NewText, ETextCommit::Type);
	void PickExpectedClass();
	void ClearExpectedClass();
	void OnEntryIdCommitted(TSharedPtr<FEntryRow> Row, const FText& NewText);
	void OnEntryPathChanged(TSharedPtr<FEntryRow> Row, const FSoftObjectPath& NewPath);

	// ---- 拖放 / 定位 ----
	void OnAssetsDropped(const TArray<FAssetData>& Assets);          // 批量入库当前类别
	void SyncRowToContentBrowser(TSharedPtr<FEntryRow> Row);

	// ---- 选择 / 刷新 ----
	void OnCategorySelected(TSharedPtr<FCategoryRow> Row, ESelectInfo::Type);
	void OnSearchChanged(const FText& Text);
	void RefreshCategoryRows();
	void RefreshEntryRows();
	void RefreshAll();
	void ComputeEntryStatus(FEntryRow& Row) const;

	ULeoAssetManifest* GetManifest() const;
	FLeoManifestCategory* GetSelectedCategory() const;
	void Notify(const FText& Text, bool bSuccess) const;

	TWeakObjectPtr<ULeoAssetManifest> Manifest;
	FName SelectedCategory;

	TArray<TSharedPtr<FCategoryRow>> CategoryRows;
	TSharedPtr<SListView<TSharedPtr<FCategoryRow>>> CategoryList;
	TArray<TSharedPtr<FEntryRow>> EntryRows;
	TSharedPtr<SListView<TSharedPtr<FEntryRow>>> EntryList;
	TSharedPtr<SSearchBox> SearchBox;
	FText SearchText;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool; // 条目行资产缩略图（黑板编辑器同款观感）
};
