#include "LeoManifestEditorToolkit.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/SClassPickerDialog.h"
#include "SClassViewer.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/Docking/SDockTab.h"
#include "SDropTarget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "LeoManifestEditorToolkit"

namespace
{
	const FName GManifestTabId(TEXT("LeoManifestEditorTab"));

	// 内置类别名（类名框只读——命令 token 绑定，改名等于换类别）
	bool IsBuiltInCategory(FName Name)
	{
		return Name == ULeoAssetManifest::BgmCategory() || Name == ULeoAssetManifest::SeCategory()
			|| Name == ULeoAssetManifest::VoiceCategory() || Name == ULeoAssetManifest::BgCategory()
			|| Name == ULeoAssetManifest::CharCategory() || Name == ULeoAssetManifest::SeqCategory();
	}
}

void FLeoManifestEditorToolkit::InitLeoManifestEditor(const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& Host, ULeoAssetManifest* InManifest)
{
	Manifest = InManifest;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(256);
	if (Manifest.IsValid() && Manifest->Categories.Num() > 0)
	{
		SelectedCategory = Manifest->Categories[0].Name; // 默认选首节
	}
	RefreshCategoryRows();
	RefreshEntryRows();

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("LeoManifestEditorLayout_v1"))
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Horizontal)
		->Split(FTabManager::NewStack()->SetHideTabWell(true)
			->AddTab(GManifestTabId, ETabState::OpenedTab))
	);

	InitAssetEditor(Mode, Host, TEXT("LeoManifestEditorApp"), Layout,
		/*bCreateDefaultStandaloneMenu*/true, /*bCreateDefaultToolbar*/false, InManifest);
}

FLeoManifestEditorToolkit::~FLeoManifestEditorToolkit() = default;

FText FLeoManifestEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "叙事资产清单");
}

void FLeoManifestEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(GManifestTabId,
		FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
		{
			return SNew(SDockTab).TabRole(ETabRole::PanelTab)
				.Label(LOCTEXT("ManifestTabLabel", "清单"))
				[
					MakeManifestTab()
				];
		}));
}

void FLeoManifestEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(GManifestTabId);
}

void FLeoManifestEditorToolkit::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		if (GetManifest() && GetManifest()->FindCategory(SelectedCategory) == nullptr)
		{
			SelectedCategory = GetManifest()->Categories.Num() > 0 ? GetManifest()->Categories[0].Name : NAME_None;
		}
		RefreshAll();
	}
}

void FLeoManifestEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

// ---- 布局 ----

TSharedRef<SWidget> FLeoManifestEditorToolkit::MakeManifestTab()
{
	CategoryList = SNew(SListView<TSharedPtr<FCategoryRow>>)
		.ListItemsSource(&CategoryRows)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &FLeoManifestEditorToolkit::MakeCategoryRow)
		.OnSelectionChanged(this, &FLeoManifestEditorToolkit::OnCategorySelected);

	EntryList = SNew(SListView<TSharedPtr<FEntryRow>>)
		.ListItemsSource(&EntryRows)
		.SelectionMode(ESelectionMode::Multi)
		.OnGenerateRow(this, &FLeoManifestEditorToolkit::MakeEntryRowWidget);

	return SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(6, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SButton).Text(LOCTEXT("AddCategory", "+ 类别"))
				.ToolTipText(LOCTEXT("AddCategoryTip", "新建自定义类别节（内置类别已预填；改名在右侧类别头）"))
				.OnClicked_Lambda([this]()
				{
					AddCategory();
					return FReply::Handled();
				})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SButton).Text(LOCTEXT("AddEntry", "+ 条目"))
				.ToolTipText(LOCTEXT("AddEntryTip", "在当前类别下新建逻辑名条目"))
				.OnClicked_Lambda([this]()
				{
					AddEntry();
					return FReply::Handled();
				})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SButton).Text(LOCTEXT("DeleteEntries", "删除选中"))
				.ToolTipText(LOCTEXT("DeleteEntriesTip", "删除选中的条目（可多选）"))
				.OnClicked_Lambda([this]()
				{
					DeleteSelectedEntries();
					return FReply::Handled();
				})
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(12, 0, 6, 0)
		[
			SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("SearchHint", "搜索逻辑名 / 资产路径（跨全部类别）"))
				.OnTextChanged(this, &FLeoManifestEditorToolkit::OnSearchChanged)
		]
	]
	+ SVerticalBox::Slot().FillHeight(1.f)
	[
		SNew(SSplitter).Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.28f).MinSize(180.f)
		[
			SNew(SBorder).Padding(4.f).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 4)
				[
					SNew(STextBlock).Text(LOCTEXT("CategoryHeader", "类别（内置 = 命令绑定，自定义可加）"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					CategoryList.ToSharedRef()
				]
			]
		]
		+ SSplitter::Slot().Value(0.72f)
		[
			// 整个条目区接收资产拖放：按对象名批量入库当前类别
			SNew(SDropTarget)
			.OnAllowDrop_Lambda([](TSharedPtr<FDragDropOperation> Op)
			{
				return Op.IsValid() && Op->IsOfType<FAssetDragDropOp>();
			})
			.OnDropped_Lambda([this](const FGeometry&, const FDragDropEvent& Event)
			{
				const TSharedPtr<FDragDropOperation> Op = Event.GetOperation();
				if (Op.IsValid() && Op->IsOfType<FAssetDragDropOp>())
				{
					OnAssetsDropped(StaticCastSharedPtr<FAssetDragDropOp>(Op)->GetAssets());
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder).Padding(4.f).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						// 类别头：类名（内置只读）/ 期望类声明 / 删除——选择变化时重建
						SNew(SBox).VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
								.Text_Lambda([this]()
								{
									return SelectedCategory.IsNone()
										? LOCTEXT("NoCategory", "← 先选择类别（或新建）")
										: FText::Format(LOCTEXT("CategoryTitleFmt", "类别：{0}（{1} 条）"),
											FText::FromName(SelectedCategory),
											FText::AsNumber(GetSelectedCategory() ? GetSelectedCategory()->Assets.Num() : 0));
								})
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 2, 4)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
						[
							SNew(STextBlock).Text(LOCTEXT("CategoryNameLabel", "类别名："))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
						[
							SNew(SEditableTextBox)
								.Text_Lambda([this]()
								{
									const FLeoManifestCategory* Cat = GetSelectedCategory();
									return Cat ? FText::FromName(Cat->Name) : FText::GetEmpty();
								})
								.IsReadOnly_Lambda([this]()
								{
									return IsBuiltInCategory(SelectedCategory); // 内置类别名 = 命令 token，不可改
								})
								.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
								{
									OnCategoryNameCommitted(NewText, ETextCommit::Type::Default);
								})
								.MinDesiredWidth(140.f)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
						[
							SNew(SButton).Text(LOCTEXT("PickClass", "期望类…"))
								.ToolTipText(LOCTEXT("PickClassTip", "声明该类别的期望资产类（校验按它核对类型；内置类别不声明时走框架默认）"))
								.IsEnabled_Lambda([this]() { return GetSelectedCategory() != nullptr; })
								.OnClicked_Lambda([this]()
								{
									PickExpectedClass();
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
						[
							SNew(STextBlock)
								.Text_Lambda([this]()
								{
									const FLeoManifestCategory* Cat = GetSelectedCategory();
									if (!Cat) { return FText::GetEmpty(); }
									if (!Cat->ExpectedClass.IsNull())
									{
										return FText::FromString(Cat->ExpectedClass->GetFName().ToString());
									}
									const UClass* Def = ULeoAssetManifest::GetBuiltInExpectedClass(Cat->Name);
									return Def
										? FText::FromString(FString::Printf(TEXT("(内置默认 %s)"), *Def->GetName()))
										: LOCTEXT("NoExpectedClass", "(只核存在性)");
								})
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.56f, 0.58f)))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
						[
							SNew(SButton).Text(LOCTEXT("ClearClass", "清除期望类"))
								.ToolTipText(LOCTEXT("ClearClassTip", "恢复默认（内置类别走框架默认，其余只核存在性）"))
								.IsEnabled_Lambda([this]()
								{
									const FLeoManifestCategory* Cat = GetSelectedCategory();
									return Cat && !Cat->ExpectedClass.IsNull();
								})
								.OnClicked_Lambda([this]()
								{
									ClearExpectedClass();
									return FReply::Handled();
								})
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SBox)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SButton).Text(LOCTEXT("DeleteCategory", "删除类别"))
								.ToolTipText(LOCTEXT("DeleteCategoryTip", "删除该类别节及其全部条目（需确认）"))
								.IsEnabled_Lambda([this]() { return GetSelectedCategory() != nullptr; })
								.OnClicked_Lambda([this]()
								{
									DeleteSelectedCategory();
									return FReply::Handled();
								})
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(2, 0)
					[
						EntryList.ToSharedRef()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("DropHint", "拖 Content Browser 资产到此 = 按对象名批量入库当前类别；单条在资产列下拉选择或直接拖到该列"))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.56f, 0.58f)))
					]
				]
			]
		]
	];
}

TSharedRef<ITableRow> FLeoManifestEditorToolkit::MakeCategoryRow(TSharedPtr<FCategoryRow> Row,
	const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FCategoryRow>>, Owner)
		.Padding(4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SBox).WidthOverride(8).HeightOverride(8)
				[
					SNew(SBorder).BorderBackgroundColor(Row->bBuiltIn
						? FLinearColor(0.1f, 0.5f, 0.9f)   // 内置类别：蓝点
						: FLinearColor(0.6f, 0.6f, 0.6f)) // 自定义类别：灰点
					[
						SNew(SBox)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromName(Row->Name))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::AsNumber(Row->Count)).ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.56f, 0.58f)))
			]
		];
}

TSharedRef<ITableRow> FLeoManifestEditorToolkit::MakeEntryRowWidget(TSharedPtr<FEntryRow> Row,
	const TSharedRef<STableViewBase>& Owner)
{
	const FSlateColor StatusColor = Row->Status == EEntryStatus::Ok
		? FSlateColor(FLinearColor(0.15f, 0.7f, 0.25f))
		: Row->Status == EEntryStatus::MissingAsset
			? FSlateColor(FLinearColor(0.9f, 0.2f, 0.15f))
			: FSlateColor(FLinearColor(0.95f, 0.75f, 0.1f)); // 类型不符 / 跨节重复共用黄

	// 资产列 = 引擎标准对象选择器（黑板编辑器同款）：缩略图 + 下拉资产树 + 浏览/拖放（控件自带）；
	// 有期望类的类别在树里只显示匹配资产（无期望类 = 不过滤），选错不硬拦（行状态点黄提示）
	return SNew(STableRow<TSharedPtr<FEntryRow>>, Owner)
		.Padding(2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SBox).WidthOverride(12).HeightOverride(12)
				[
					SNew(SBorder).BorderBackgroundColor(StatusColor)
						.ToolTipText(Row->StatusTip)
					[
						SNew(SBox)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.30f).VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SEditableTextBox)
					.Text(FText::FromName(Row->Id))
					.ToolTipText(LOCTEXT("IdTip", "脚本引用的逻辑名（铁律 #2：脚本只写这个名字）"))
					.OnTextCommitted_Lambda([this, Row](const FText& NewText, ETextCommit::Type)
					{
						OnEntryIdCommitted(Row, NewText);
					})
			]
			+ SHorizontalBox::Slot().FillWidth(0.52f).VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SObjectPropertyEntryBox)
					.AllowedClass(UObject::StaticClass())
					.OnShouldFilterAsset_Lambda([this, Row](const FAssetData& Asset)
					{
						const ULeoAssetManifest* M = GetManifest();
						const FLeoManifestCategory* Cat = M ? M->FindCategory(Row->Category) : nullptr;
						if (!Cat) { return false; }
						const UClass* Want = M->ResolveExpectedClass(*Cat);
						if (!Want) { return false; } // 只核存在性的类别不过滤
						UClass* Actual = Asset.GetClass();
						return !(Actual && Actual->IsChildOf(Want)); // true = 从树里滤掉
					})
					.ObjectPath_Lambda([Row]() { return Row->Path.ToString(); })
					.OnObjectChanged_Lambda([this, Row](const FAssetData& Asset)
					{
						OnEntryPathChanged(Row, Asset.GetSoftObjectPath());
					})
					.ThumbnailPool(ThumbnailPool)
					.ToolTipText(FText::FromString(Row->Path.ToString()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SButton).Text(LOCTEXT("Locate", "定位"))
					.ToolTipText(LOCTEXT("LocateTip", "在 Content Browser 中定位该资产"))
					.IsEnabled_Lambda([Row]() { return Row->Path.IsValid(); })
					.OnClicked_Lambda([this, Row]()
					{
						SyncRowToContentBrowser(Row);
						return FReply::Handled();
					})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				// M7f 教训：小行内删除钮用文字按钮，别试图标画刷
				SNew(SButton).Text(LOCTEXT("Delete", "删除"))
					.OnClicked_Lambda([this, Row]()
					{
						if (ULeoAssetManifest* M = GetManifest())
						{
							FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "DeleteEntry", "删除清单条目"), M);
							M->Modify();
							for (FLeoManifestCategory& Cat : M->Categories)
							{
								if (Cat.Name == Row->Category) { Cat.Assets.Remove(Row->Id); break; }
							}
							RefreshAll();
						}
						return FReply::Handled();
					})
			]
		];
}

// ---- 增删改 ----

void FLeoManifestEditorToolkit::AddCategory()
{
	ULeoAssetManifest* M = GetManifest();
	if (!M) { return; }
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "AddCategory", "新建清单类别"), M);
	M->Modify();
	FLeoManifestCategory& Cat = M->Categories.AddDefaulted_GetRef();
	Cat.Name = TEXT("new_category");
	for (int32 Suffix = 1; M->FindCategory(Cat.Name) != &Cat; ++Suffix) // 重名递增
	{
		Cat.Name = *FString::Printf(TEXT("new_category_%d"), Suffix);
	}
	SelectedCategory = Cat.Name;
	RefreshAll();
	Notify(FText::FromString(FString::Printf(TEXT("已新建类别 %s——在右侧改名/声明期望类"), *Cat.Name.ToString())), true);
}

void FLeoManifestEditorToolkit::DeleteSelectedCategory()
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = GetSelectedCategory();
	if (!M || !Cat) { return; }
	const FText Msg = FText::Format(
		NSLOCTEXT("LeoManifest", "DeleteCategoryConfirm", "删除类别 {0} 及其 {1} 条目？"),
		FText::FromName(Cat->Name), FText::AsNumber(Cat->Assets.Num()));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Msg) != EAppReturnType::Yes) { return; }

	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "DeleteCategory", "删除清单类别"), M);
	M->Modify();
	M->Categories.RemoveAll([Cat](const FLeoManifestCategory& C) { return C.Name == Cat->Name; });
	SelectedCategory = M->Categories.Num() > 0 ? M->Categories[0].Name : NAME_None;
	RefreshAll();
}

void FLeoManifestEditorToolkit::AddEntry()
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = GetSelectedCategory();
	if (!M || !Cat) { return; }
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "AddEntry", "新建清单条目"), M);
	M->Modify();
	FName Id = TEXT("new_id");
	for (int32 Suffix = 1; Cat->Assets.Contains(Id); ++Suffix)
	{
		Id = *FString::Printf(TEXT("new_id_%d"), Suffix);
	}
	Cat->Assets.Add(Id, FSoftObjectPath());
	RefreshAll();
}

void FLeoManifestEditorToolkit::DeleteSelectedEntries()
{
	ULeoAssetManifest* M = GetManifest();
	if (!M || !EntryList.IsValid()) { return; }
	const TArray<TSharedPtr<FEntryRow>> Selected = EntryList->GetSelectedItems();
	if (Selected.Num() == 0) { return; }
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "DeleteEntries", "删除清单条目"), M);
	M->Modify();
	for (const TSharedPtr<FEntryRow>& Row : Selected)
	{
		if (FLeoManifestCategory* Cat = M->FindCategory(Row->Category))
		{
			Cat->Assets.Remove(Row->Id);
		}
	}
	RefreshAll();
}

void FLeoManifestEditorToolkit::OnCategoryNameCommitted(const FText& NewText, ETextCommit::Type)
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = GetSelectedCategory();
	if (!M || !Cat || IsBuiltInCategory(Cat->Name)) { return; }
	const FName NewName(*NewText.ToString().TrimStartAndEnd());
	if (NewName.IsNone() || NewName == Cat->Name) { RefreshEntryRows(); return; }
	if (M->FindCategory(NewName))
	{
		RefreshEntryRows();
		Notify(FText::FromString(FString::Printf(TEXT("类别名 %s 已存在"), *NewName.ToString())), false);
		return;
	}
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "RenameCategory", "类别改名"), M);
	M->Modify();
	Cat->Name = NewName;
	SelectedCategory = NewName;
	RefreshAll();
}

void FLeoManifestEditorToolkit::PickExpectedClass()
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = GetSelectedCategory();
	if (!M || !Cat) { return; }
	UClass* Chosen = nullptr;
	if (SClassPickerDialog::PickClass(
			NSLOCTEXT("LeoManifest", "PickExpectedClassTitle", "选择类别的期望资产类"),
			FClassViewerInitializationOptions(), Chosen, /*AssetType=*/nullptr)
		&& Chosen)
	{
		FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "SetExpectedClass", "声明期望类"), M);
		M->Modify();
		Cat->ExpectedClass = Chosen;
		RefreshAll();
	}
}

void FLeoManifestEditorToolkit::ClearExpectedClass()
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = GetSelectedCategory();
	if (!M || !Cat) { return; }
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "ClearExpectedClass", "清除期望类"), M);
	M->Modify();
	Cat->ExpectedClass = nullptr;
	RefreshAll();
}

void FLeoManifestEditorToolkit::OnEntryIdCommitted(TSharedPtr<FEntryRow> Row, const FText& NewText)
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = M ? M->FindCategory(Row->Category) : nullptr;
	if (!Cat) { return; }
	const FName NewId(*NewText.ToString().TrimStartAndEnd());
	if (NewId.IsNone() || NewId == Row->Id) { RefreshEntryRows(); return; }
	if (Cat->Assets.Contains(NewId))
	{
		RefreshEntryRows();
		Notify(FText::FromString(FString::Printf(TEXT("逻辑名 %s 在 %s 节已存在"), *NewId.ToString(), *Row->Category.ToString())), false);
		return;
	}
	// map "改名" = 挪值
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "RenameEntry", "逻辑名改名"), M);
	M->Modify();
	if (const FSoftObjectPath* P = Cat->Assets.Find(Row->Id))
	{
		const FSoftObjectPath Path = *P;
		Cat->Assets.Remove(Row->Id);
		Cat->Assets.Add(NewId, Path);
	}
	RefreshAll();
}

void FLeoManifestEditorToolkit::OnEntryPathChanged(TSharedPtr<FEntryRow> Row, const FSoftObjectPath& NewPath)
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = M ? M->FindCategory(Row->Category) : nullptr;
	if (!Cat) { return; }
	if (NewPath == Row->Path) { return; }
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "SetEntryPath", "修改条目资产"), M);
	M->Modify();
	Cat->Assets.Add(Row->Id, NewPath);
	RefreshAll();
}

// ---- 拖放 / 定位 ----

void FLeoManifestEditorToolkit::OnAssetsDropped(const TArray<FAssetData>& Assets)
{
	ULeoAssetManifest* M = GetManifest();
	FLeoManifestCategory* Cat = GetSelectedCategory();
	if (!M) { return; }
	if (!Cat)
	{
		Notify(NSLOCTEXT("LeoManifest", "NeedCategory", "先在左侧选择类别，再拖资产入库"), false);
		return;
	}
	FScopedTransaction Tr(TEXT("LeoManifestEdit"), NSLOCTEXT("LeoManifest", "BatchImport", "拖放批量入库"), M);
	M->Modify();
	int32 Added = 0, Skipped = 0;
	for (const FAssetData& A : Assets)
	{
		if (Cat->Assets.Contains(A.AssetName)) { ++Skipped; continue; } // 对象名即逻辑名
		Cat->Assets.Add(A.AssetName, A.GetSoftObjectPath());
		++Added;
	}
	RefreshAll();
	Notify(FText::FromString(FString::Printf(TEXT("入库 %d 条到 %s（跳过同名 %d）"),
		Added, *Cat->Name.ToString(), Skipped)), true);
}

void FLeoManifestEditorToolkit::SyncRowToContentBrowser(TSharedPtr<FEntryRow> Row)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData Data = ARM.GetRegistry().GetAssetByObjectPath(Row->Path, /*bIncludeOnlyOnDiskAssets=*/true);
	if (Data.IsValid())
	{
		FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		CB.Get().SyncBrowserToAssets({ Data });
	}
	else
	{
		Notify(FText::FromString(FString::Printf(TEXT("资产不存在: %s"), *Row->Path.ToString())), false);
	}
}

// ---- 选择 / 刷新 ----

void FLeoManifestEditorToolkit::OnCategorySelected(TSharedPtr<FCategoryRow> Row, ESelectInfo::Type)
{
	if (Row.IsValid())
	{
		SelectedCategory = Row->Name;
	}
	RefreshEntryRows();
}

void FLeoManifestEditorToolkit::OnSearchChanged(const FText& Text)
{
	SearchText = Text;
	RefreshEntryRows();
}

void FLeoManifestEditorToolkit::RefreshCategoryRows()
{
	CategoryRows.Reset();
	if (const ULeoAssetManifest* M = GetManifest())
	{
		for (const FLeoManifestCategory& Cat : M->Categories)
		{
			CategoryRows.Add(MakeShared<FCategoryRow>(FCategoryRow{ Cat.Name, Cat.Assets.Num(), IsBuiltInCategory(Cat.Name) }));
		}
	}
	if (CategoryList.IsValid()) { CategoryList->RequestListRefresh(); }
}

void FLeoManifestEditorToolkit::RefreshEntryRows()
{
	EntryRows.Reset();
	const ULeoAssetManifest* M = GetManifest();
	const FString Filter = SearchText.ToString();
	if (M)
	{
		const bool bSearch = !Filter.IsEmpty();
		for (const FLeoManifestCategory& Cat : M->Categories)
		{
			if (!bSearch && Cat.Name != SelectedCategory) { continue; }
			if (Cat.Name == NAME_None) { continue; }
			// 键稳定序展示（不改资产数据：键拷出排序再取值）
			TArray<FName> Keys;
			Cat.Assets.GetKeys(Keys);
			Keys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			for (const FName& Key : Keys)
			{
				if (bSearch
					&& !Key.ToString().Contains(Filter)
					&& !Cat.Assets.FindChecked(Key).ToString().Contains(Filter))
				{
					continue;
				}
				TSharedPtr<FEntryRow> Row = MakeShared<FEntryRow>();
				Row->Category = Cat.Name;
				Row->Id = Key;
				Row->Path = Cat.Assets.FindChecked(Key);
				ComputeEntryStatus(*Row);
				EntryRows.Add(MoveTemp(Row));
			}
		}
	}
	if (EntryList.IsValid()) { EntryList->RequestListRefresh(); }
}

void FLeoManifestEditorToolkit::RefreshAll()
{
	RefreshCategoryRows();
	RefreshEntryRows();
}

void FLeoManifestEditorToolkit::ComputeEntryStatus(FEntryRow& Row) const
{
	const ULeoAssetManifest* M = GetManifest();
	const FLeoManifestCategory* Cat = M ? M->FindCategory(Row.Category) : nullptr;
	if (!Cat || !Row.Path.IsValid())
	{
		Row.Status = EEntryStatus::MissingAsset;
		Row.StatusTip = NSLOCTEXT("LeoManifest", "EmptyPathTip", "资产路径为空");
		return;
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData Data = ARM.GetRegistry().GetAssetByObjectPath(Row.Path, /*bIncludeOnlyOnDiskAssets=*/true);
	if (!Data.IsValid())
	{
		Row.Status = EEntryStatus::MissingAsset;
		Row.StatusTip = FText::FromString(FString::Printf(TEXT("资产不存在: %s"), *Row.Path.ToString()));
		return;
	}
	FName Other;
	if (M->FindCategoryOf(Row.Id, Other) && Other != Row.Category)
	{
		Row.Status = EEntryStatus::DupKey;
		Row.StatusTip = FText::FromString(FString::Printf(TEXT("逻辑名同时存在于 %s 与 %s 节（解析只认首节）"),
			*Other.ToString(), *Row.Category.ToString()));
		return;
	}
	if (const UClass* Want = M->ResolveExpectedClass(*Cat))
	{
		UClass* AssetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *Data.AssetClassPath.ToString());
		if (!AssetClass || !AssetClass->IsChildOf(Want))
		{
			Row.Status = EEntryStatus::WrongType;
			Row.StatusTip = FText::FromString(FString::Printf(TEXT("类型不符：%s 期望 %s，实际 %s"),
				*Row.Category.ToString(), *Want->GetName(), *Data.AssetClassPath.ToString()));
			return;
		}
	}
	Row.Status = EEntryStatus::Ok;
	Row.StatusTip = NSLOCTEXT("LeoManifest", "EntryOkTip", "正常");
}

ULeoAssetManifest* FLeoManifestEditorToolkit::GetManifest() const
{
	return Manifest.Get();
}

FLeoManifestCategory* FLeoManifestEditorToolkit::GetSelectedCategory() const
{
	ULeoAssetManifest* M = GetManifest();
	return M ? M->FindCategory(SelectedCategory) : nullptr;
}

void FLeoManifestEditorToolkit::Notify(const FText& Text, bool bSuccess) const
{
	FNotificationInfo Info(Text);
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = 5.f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

#undef LOCTEXT_NAMESPACE
