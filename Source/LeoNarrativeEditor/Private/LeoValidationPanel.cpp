#include "LeoValidationPanel.h"

#include "LeoL10nToolkit.h"
#include "LeoValidation.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "HAL/PlatformProcess.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "LeoValidationPanel"

struct FLeoFileRow
{
	FString Display;   // 文件名（附 PASS/FAIL 标记）
	FString Path;      // 绝对路径（资产核对行为清单资产路径）
	bool bPass = true;
	bool bIsAssetSection = false; // "资产引用核对" 伪条目
	bool bIsGraphSection = false; // "编排图核对" 伪条目
	bool bIsL10nSection = false;  // "本地化核对" 伪条目
	int32 ItemIndex = -1;         // 对应 Summary.Files 索引
};

struct FLeoDiagRow
{
	FString Display;   // 一行式：Code L<行号> Message
	FString OpenTarget; // 双击目标：.leo 绝对路径 或 /Game 资产路径（空 = 不可跳转）
	bool bError = false;
};
using FLeoFileRowPtr = TSharedPtr<FLeoFileRow>;
using FLeoDiagRowPtr = TSharedPtr<FLeoDiagRow>;

namespace
{

// 面板状态：持有最新校验摘要 + 行数据；订阅 watcher 完成通知置脏，由控件 Tick 拉取刷新
class FLeoValidationPanelState : public TSharedFromThis<FLeoValidationPanelState>
{
public:
	FLeoValidationPanelState()
	{
		// 注意：不可在构造函数里 AddSP——TSharedFromThis 的共享引用要到
		// MakeShared 装配完成才就位，构造期内绑定即崩溃。Init() 由 Construct 在
		// MakeShared 之后调用
	}

	void Init()
	{
		LeoValidation::OnScriptsRevalidated.AddSP(this, &FLeoValidationPanelState::MarkDirty);
		Refresh();
	}

	~FLeoValidationPanelState()
	{
		LeoValidation::OnScriptsRevalidated.RemoveAll(this);
	}

	void MarkDirty() { bDirty = true; }
	bool ConsumeDirty() { const bool b = bDirty; bDirty = false; return b; }

	void Refresh()
	{
		Summary = LeoValidation::ValidateAllStructured();
		FileRows.Reset();
		for (int32 i = 0; i < Summary.Files.Num(); ++i)
		{
			const LeoValidation::FLeoFileResult& R = Summary.Files[i];
			const TSharedPtr<FLeoFileRow> Row = MakeShared<FLeoFileRow>();
			Row->Display = FString::Printf(TEXT("%s  %s"), R.bPass ? TEXT("√") : TEXT("×"),
				*FPaths::GetCleanFilename(R.Path));
			Row->Path = R.Path;
			Row->bPass = R.bPass;
			Row->ItemIndex = i;
			FileRows.Add(Row);
		}
		// 资产引用核对伪条目（始终在列；无清单时显示跳过说明）
		const TSharedPtr<FLeoFileRow> AssetRow = MakeShared<FLeoFileRow>();
		const bool bAssetOk = Summary.AssetErrors == 0;
		AssetRow->Display = FString::Printf(TEXT("%s  资产引用核对%s"), bAssetOk ? TEXT("√") : TEXT("×"),
			Summary.ManifestPath.IsEmpty() ? TEXT("（未找到清单，跳过）") : TEXT(""));
		AssetRow->Path = Summary.ManifestPath;
		AssetRow->bPass = bAssetOk;
		AssetRow->bIsAssetSection = true;
		FileRows.Add(AssetRow);

		const TSharedPtr<FLeoFileRow> GraphRow = MakeShared<FLeoFileRow>();
		const bool bGraphOk = Summary.GraphErrors == 0;
		GraphRow->Display = FString::Printf(TEXT("%s  编排图核对（%d 张图）"), bGraphOk ? TEXT("√") : TEXT("×"), Summary.GraphItems.Num());
		GraphRow->bPass = bGraphOk;
		GraphRow->bIsGraphSection = true;
		FileRows.Add(GraphRow);

		const TSharedPtr<FLeoFileRow> L10nRow = MakeShared<FLeoFileRow>();
		const bool bL10nOk = Summary.L10nErrors == 0;
		L10nRow->Display = Summary.L10nCultures == 0
			? TEXT("○  本地化核对（未启用，跳过）")
			: FString::Printf(TEXT("%s  本地化核对（%d 种语言）"), bL10nOk ? TEXT("√") : TEXT("×"), Summary.L10nCultures);
		L10nRow->bPass = bL10nOk;
		L10nRow->bIsL10nSection = true;
		FileRows.Add(L10nRow);

		SelectedFileRow = nullptr;
		RebuildDiags();
		if (FileList.IsValid()) { FileList.Pin()->RequestListRefresh(); }
	}

	void SelectFile(const FLeoFileRowPtr& Row)
	{
		if (!Row.IsValid()) { return; }
		SelectedFileRow = Row;
		RebuildDiags();
	}

	void RebuildDiags()
	{
		DiagRows.Reset();
		if (SelectedFileRow.IsValid())
		{
			if (SelectedFileRow->bIsAssetSection)
			{
				for (const LeoValidation::FLeoCheckItem& It : Summary.AssetItems)
				{
					AddDiag(It, It.Code == TEXT("NO_MANIFEST_ENTRY") ? FString() : Summary.ManifestPath);
				}
			}
			else if (SelectedFileRow->bIsGraphSection)
			{
				for (const LeoValidation::FLeoCheckItem& It : Summary.GraphItems)
				{
					AddDiag(It, It.File); // 双击打开对应图资产
				}
			}
			else if (SelectedFileRow->bIsL10nSection)
			{
				for (const LeoValidation::FLeoCheckItem& It : Summary.L10nItems)
				{
					AddDiag(It, It.File); // 双击用系统关联程序打开 CSV
				}
			}
			else if (Summary.Files.IsValidIndex(SelectedFileRow->ItemIndex))
			{
				const LeoValidation::FLeoFileResult& R = Summary.Files[SelectedFileRow->ItemIndex];
				for (const LeoValidation::FLeoCheckItem& It : R.Items)
				{
					AddDiag(It, R.Path);
				}
			}
		}
		if (DiagList.IsValid()) { DiagList.Pin()->RequestListRefresh(); }
	}

	LeoValidation::FLeoValidateSummary Summary;
	TArray<FLeoFileRowPtr> FileRows;
	TArray<FLeoDiagRowPtr> DiagRows;
	TWeakPtr<SListView<FLeoFileRowPtr>> FileList;
	TWeakPtr<SListView<FLeoDiagRowPtr>> DiagList;
	FLeoFileRowPtr SelectedFileRow;

private:
	void AddDiag(const LeoValidation::FLeoCheckItem& It, const FString& OpenTarget)
	{
		const TSharedPtr<FLeoDiagRow> Row = MakeShared<FLeoDiagRow>();
		Row->Display = It.Line > 0
			? FString::Printf(TEXT("%s  L%d  %s"), *It.Code, It.Line, *It.Message)
			: FString::Printf(TEXT("%s  %s"), *It.Code, *It.Message);
		Row->bError = It.bError;
		Row->OpenTarget = OpenTarget;
		DiagRows.Add(Row);
	}

	bool bDirty = false;
};

// 双击统一入口：.leo 用系统关联程序打开；/Game 资产在资产编辑器打开
void OpenDiagTarget(const FString& Target)
{
	if (Target.IsEmpty()) { return; }
	if (Target.StartsWith(TEXT("/Game")))
	{
		if (GEditor)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Target);
		}
	}
	else if (FPaths::FileExists(Target))
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Target);
	}
}

void ShowPanelNotification(const FText& Text, SNotificationItem::ECompletionState State)
{
	FNotificationInfo Info(Text);
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = 6.f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(State);
	}
}

// 面板主体
class SLeoValidationPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLeoValidationPanel) {}
	SLATE_END_ARGS()

	BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct(const FArguments& InArgs)
	{
		State = MakeShared<FLeoValidationPanelState>();
		State->Init(); // 构造后绑定委托 + 首次校验（共享引用已就位）
		RebuildCultureOptions(); // 语言下拉选项（组合框创建前就位，保证默认选中有效）

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Revalidate", "重新校验"))
					.OnClicked_Lambda([this]
					{
						State->Refresh();
						return FReply::Handled();
					})
				]
				// 本地化提取：语言下拉（种子项 ∪ Content/L10n 已有目录）→ extract → 通知 + 刷新
				+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 4, 0).VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ExtractL10n", "提取译文 CSV"))
					.ToolTipText(LOCTEXT("ExtractL10nTip", "从 Content/Scripts 提取文本 ID 与原文到 Content/L10n/<语言>/<章节>.csv（增量合并，不覆盖已有译文）"))
					.OnClicked_Lambda([this]
					{
						const FString Culture = SelectedCulture.IsValid() ? *SelectedCulture : FString(TEXT("en"));
						const int32 Failures = LeoL10nToolkit::ExtractForCultures({ Culture });
						ShowPanelNotification(
							FText::FromString(FString::Printf(TEXT("%s 译文 CSV 提取%s（%d 失败，详见 Output Log）"),
								*Culture, Failures == 0 ? TEXT("完成") : TEXT("有失败"), Failures)),
							Failures == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
						State->Refresh(); // extract 触发 watcher 也会刷新，这里立即刷一次避免竞态
						RebuildCultureOptions(); // 刚建出的语言目录要能立刻在下拉里看到
						if (CultureCombo.IsValid()) { CultureCombo->RefreshOptions(); }
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 2).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(120.f)
					[
						SAssignNew(CultureCombo, STextComboBox)
						.OptionsSource(&CultureOptions)
						.InitiallySelectedItem(SelectedCulture)
						.ToolTipText(LOCTEXT("CultureComboTip", "目标语言（已有译文目录置顶标 ●，其余为引擎支持的语言全集）；选中即提取该语言"))
						.OnGetTextLabelForItem_Lambda([this](TSharedPtr<FString> Item) -> FString
						{
							return Item.IsValid() ? CultureLabels.FindRef(*Item) : FString();
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSel, ESelectInfo::Type)
						{
							SelectedCulture = NewSel;
						})
						.OnComboBoxOpening_Lambda([this]()
						{
							RebuildCultureOptions(); // 打开下拉即发现新目录（重建保指针，选择不丢）
							if (CultureCombo.IsValid()) { CultureCombo->RefreshOptions(); }
						})
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8, 2).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text_Lambda([this] { return BuildSummaryText(); })
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
			[
				SNew(SSplitter)
				+ SSplitter::Slot().Value(0.42f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(4)
						[
							SNew(STextBlock).Text(LOCTEXT("FileListHeader", "文件（双击打开）"))
						]
						+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
						[
							SAssignNew(FileListView, SListView<FLeoFileRowPtr>)
							.ListItemsSource(&State->FileRows)
							.OnGenerateRow_Lambda([](FLeoFileRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
							{
								return SNew(STableRow<FLeoFileRowPtr>, OwnerTable)
									.Content()
									[
										SNew(STextBlock)
										.Text(FText::FromString(Row->Display))
										.ColorAndOpacity(Row->bPass
											? FSlateColor(FLinearColor(0.35f, 0.8f, 0.4f))
											: FSlateColor(FLinearColor(0.95f, 0.35f, 0.3f)))
									];
							})
							.OnSelectionChanged_Lambda([this](FLeoFileRowPtr Row, ESelectInfo::Type)
							{
								State->SelectFile(Row);
							})
							.OnMouseButtonDoubleClick_Lambda([](FLeoFileRowPtr Row)
							{
								OpenDiagTarget(Row.IsValid() ? Row->Path : FString());
							})
						]
					]
				]
				+ SSplitter::Slot().Value(0.58f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(4)
						[
							SNew(STextBlock).Text(LOCTEXT("DiagListHeader", "诊断（双击打开文件 / 资产）"))
						]
						+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
						[
							SAssignNew(DiagListView, SListView<FLeoDiagRowPtr>)
							.ListItemsSource(&State->DiagRows)
							.OnGenerateRow_Lambda([](FLeoDiagRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
							{
								return SNew(STableRow<FLeoDiagRowPtr>, OwnerTable)
									.Content()
									[
										SNew(STextBlock)
										.Text(FText::FromString(Row->Display))
										.ColorAndOpacity(Row->bError
											? FSlateColor(FLinearColor(0.95f, 0.35f, 0.3f))
											: FSlateColor(FSlateColor::UseForeground()))
									];
							})
							.OnMouseButtonDoubleClick_Lambda([](FLeoDiagRowPtr Row)
							{
								OpenDiagTarget(Row.IsValid() ? Row->OpenTarget : FString());
							})
						]
					]
				]
			]
		];
		State->FileList = FileListView;
		State->DiagList = DiagListView;
	}
	END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	// watcher 热校验完成 → 下一次 Tick 拉取刷新
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (State->ConsumeDirty())
		{
			State->Refresh();
		}
	}

private:
	FText BuildSummaryText() const
	{
		const LeoValidation::FLeoValidateSummary& S = State->Summary;
		FString L10nPart = S.L10nCultures == 0 ? TEXT("本地化未启用") :
			FString::Printf(TEXT("本地化 %d 种语言（错误 %d）"), S.L10nCultures, S.L10nErrors);
		return FText::FromString(FString::Printf(TEXT("%d 个文件，%d 不符预期；清单 %s（资产错误 %d）；%s%s"),
			S.Files.Num(), S.Failures,
			S.ManifestPath.IsEmpty() ? TEXT("未找到") : *S.ManifestPath,
			S.AssetErrors, *L10nPart,
			S.AllGreen() ? TEXT("—— 全绿") : TEXT("")));
	}

	TSharedPtr<FLeoValidationPanelState> State;
	TSharedPtr<SListView<FLeoFileRowPtr>> FileListView;
	TSharedPtr<SListView<FLeoDiagRowPtr>> DiagListView;
	TSharedPtr<STextComboBox> CultureCombo;
	TArray<TSharedPtr<FString>> CultureOptions; // 组合框数据源（重建保指针，选择不丢）
	TMap<FString, FString> CultureLabels;      // 码 → 显示名（"日本語 (ja) ●"）
	TSharedPtr<FString> SelectedCulture;

	// 语言选项 = 已有译文目录（置顶标 ●）∪ 引擎本地化语言全集（见 LeoL10nToolkit::BuildCultureChoices）
	void RebuildCultureOptions()
	{
		TArray<LeoL10nToolkit::FCultureChoice> Choices;
		LeoL10nToolkit::BuildCultureChoices(Choices);
		for (const LeoL10nToolkit::FCultureChoice& Ch : Choices)
		{
			const bool bExists = CultureOptions.ContainsByPredicate(
				[&Ch](const TSharedPtr<FString>& P) { return P.IsValid() && *P == Ch.Code; });
			if (!bExists) { CultureOptions.Add(MakeShared<FString>(Ch.Code)); }
			CultureLabels.Add(Ch.Code, Ch.bHasCsv ? Ch.Label + TEXT("  ●已有译文") : Ch.Label);
		}
		if (!SelectedCulture.IsValid())
		{
			for (const TSharedPtr<FString>& P : CultureOptions)
			{
				if (P.IsValid() && *P == TEXT("en")) { SelectedCulture = P; break; }
			}
			if (!SelectedCulture.IsValid() && CultureOptions.Num() > 0) { SelectedCulture = CultureOptions[0]; }
		}
	}
};

const FName GValidationTabName(TEXT("LeoNarrativeValidationPanel"));

} // namespace

namespace LeoValidationPanel
{

void RegisterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(GValidationTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SLeoValidationPanel)
				];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "Leo 校验中心"))
		.SetTooltipText(LOCTEXT("TabTip", "剧本编译诊断 + 清单/资产引用核对"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void UnregisterTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GValidationTabName);
}

} // namespace LeoValidationPanel

#undef LOCTEXT_NAMESPACE
