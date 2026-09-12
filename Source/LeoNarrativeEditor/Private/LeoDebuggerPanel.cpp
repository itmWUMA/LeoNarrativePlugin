#include "LeoDebuggerPanel.h"

#include "Save/LeoSaveGame.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "Subsystem/LeoNarrativeSubsystem.h"

#include "Engine/Engine.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Kismet/GameplayStatics.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "LeoDebuggerPanel"

namespace
{

using FStringRowPtr = TSharedPtr<FString>;

// 取 PIE/游戏世界里的叙事子系统（无活跃实例返回 null——面板显示提示）
ULeoNarrativeSubsystem* FindLiveSubsystem()
{
	if (!GEngine) { return nullptr; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.OwningGameInstance &&
			(Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
		{
			if (ULeoNarrativeSubsystem* S = Ctx.OwningGameInstance->GetSubsystem<ULeoNarrativeSubsystem>())
			{
				return S;
			}
		}
	}
	return nullptr;
}

// 面板状态：最新快照 + 列表行数据
class FLeoDebuggerState
{
public:
	FLeoDebugSnapshot Snap;
	TArray<FStringRowPtr> LocalRows;
	TArray<FStringRowPtr> GlobalRows;
	TArray<FStringRowPtr> EventRows;
	FString SaveSummary;      // 存档查看文本（点击按钮时生成）
	bool bAutoRefresh = true;
};

// 存档值展示（编辑器侧独立格式化——纯内核符号不跨模块导出）
FString SavedValueToString(const FLeoSavedValue& V)
{
	switch (V.Type)
	{
	case 1: return V.B ? TEXT("true") : TEXT("false");
	case 2: return FString::Printf(TEXT("%lld"), V.I);
	case 3: return FString::Printf(TEXT("%g"), V.F);
	case 4: return V.S;
	default: return TEXT("null");
	}
}

// 存档查看：直接读两个槽，拼摘要（编辑器态也可用，无需 PIE）
FString BuildSaveSummary()
{
	FString Out;
	if (ULeoGlobalSaveGame* G = Cast<ULeoGlobalSaveGame>(
		UGameplayStatics::LoadGameFromSlot(ULeoNarrativeSubsystem::GlobalSlotName(), 0)))
	{
		Out += FString::Printf(TEXT("全局档：已读文本 %d 条，全局变量 %d 个\n"),
			G->ReadTextIds.Num(), G->VarKeys.Num());
		for (int32 i = 0; i < G->VarKeys.Num() && i < 12; ++i)
		{
			Out += FString::Printf(TEXT("    %s = %s\n"), *G->VarKeys[i].ToString(),
				*SavedValueToString(G->VarValues[i]));
		}
		if (G->VarKeys.Num() > 12) { Out += TEXT("    …\n"); }
	}
	else
	{
		Out += TEXT("全局档：不存在\n");
	}
	if (ULeoProgressSaveGame* P = Cast<ULeoProgressSaveGame>(
		UGameplayStatics::LoadGameFromSlot(ULeoNarrativeSubsystem::ProgressSlotName(), 0)))
	{
		Out += FString::Printf(TEXT("进度档：章节 %s，锚点 %s+%d，图 %s@%s，局部变量 %d 个\n"),
			*P->Chapter.ToString(), *P->AnchorLabel.ToString(), P->AnchorOffset,
			P->GraphAsset.IsValid() ? *P->GraphAsset.GetAssetPathString() : TEXT("（无）"),
			*P->GraphNodeId.ToString(), P->LocalKeys.Num());
		if (P->TimestampTicks > 0)
		{
			// 存的是 UTC ticks；用 Now-UtcNow 差值换算本地时区展示
			const FDateTime Local = FDateTime(P->TimestampTicks) + (FDateTime::Now() - FDateTime::UtcNow());
			Out += FString::Printf(TEXT("    保存于 %s"), *Local.ToString());
		}
	}
	else
	{
		Out += TEXT("进度档：不存在");
	}
	return Out;
}

class SLeoDebuggerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLeoDebuggerPanel) {}
	SLATE_END_ARGS()

	BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
	void Construct(const FArguments& InArgs)
	{
		State = MakeShared<FLeoDebuggerState>();

		ChildSlot
		[
			SNew(SVerticalBox)
			// ---- 工具行：刷新 + 手动驱动 ----
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(SWrapBox).PreferredSize(400.f)
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("Refresh", "刷新"))
					.OnClicked_Lambda([this] { Refresh(); return FReply::Handled(); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this] { return State->bAutoRefresh ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState C) { State->bAutoRefresh = C == ECheckBoxState::Checked; })
					[
						SNew(STextBlock).Text(LOCTEXT("AutoRefresh", "自动刷新"))
					]
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("Advance", "推进"), [](ULeoNarrativeSubsystem* S) { S->Advance(); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("Choose0", "选0"), [](ULeoNarrativeSubsystem* S) { S->Choose(0); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("Choose1", "选1"), [](ULeoNarrativeSubsystem* S) { S->Choose(1); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("Resume0", "断点恢复0"), [this](ULeoNarrativeSubsystem* S)
					{ S->ResumeWith(State->Snap.SuspendToken, leo::FLeoValue::MakeInt(0)); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("Resume1", "断点恢复1"), [this](ULeoNarrativeSubsystem* S)
					{ S->ResumeWith(State->Snap.SuspendToken, leo::FLeoValue::MakeInt(1)); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("SkipSeq", "跳过过场"), [](ULeoNarrativeSubsystem* S) { S->SkipSequences(); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					MakeDriveButton(LOCTEXT("Stop", "停止会话"), [](ULeoNarrativeSubsystem* S) { S->Stop(); })
				]
				+ SWrapBox::Slot().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("ReadSaves", "读取存档"))
					.OnClicked_Lambda([this]
					{
						State->SaveSummary = BuildSaveSummary();
						return FReply::Handled();
					})
				]
			]
			// ---- 状态摘要 ----
			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return BuildStatusText(); })
				.AutoWrapText(true)
			]
			// ---- 双黑板 ----
			+ SVerticalBox::Slot().FillHeight(0.34f).Padding(4)
			[
				SNew(SSplitter)
				+ SSplitter::Slot().Value(0.5f)
				[
					MakeBoardBox(LOCTEXT("LocalBb", "局部黑板"), &State->LocalRows, LocalList)
				]
				+ SSplitter::Slot().Value(0.5f)
				[
					MakeBoardBox(LOCTEXT("GlobalBb", "全局黑板"), &State->GlobalRows, GlobalList)
				]
			]
			// ---- 事件流 + 存档 ----
			+ SVerticalBox::Slot().FillHeight(0.4f).Padding(4)
			[
				SNew(SSplitter)
				+ SSplitter::Slot().Value(0.62f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(4)
						[
							SNew(STextBlock).Text(LOCTEXT("EventLogHeader", "事件流"))
						]
						+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
						[
							SAssignNew(EventList, SListView<FStringRowPtr>)
							.ListItemsSource(&State->EventRows)
							.OnGenerateRow_Lambda([](FStringRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
							{
								return SNew(STableRow<FStringRowPtr>, OwnerTable)
									.Content()
									[
										SNew(STextBlock).Text(FText::FromString(*Row))
									];
							})
						]
					]
				]
				+ SSplitter::Slot().Value(0.38f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot().Padding(4)
						[
							SNew(STextBlock)
							.Text_Lambda([this] { return FText::FromString(State->SaveSummary.IsEmpty()
								? TEXT("（点击「读取存档」查看双档内容）") : State->SaveSummary); })
							.AutoWrapText(true)
						]
					]
				]
			]
		];
		Refresh();
	}
	END_SLATE_FUNCTION_BUILD_OPTIMIZATION

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (!State->bAutoRefresh) { return; }
		Accum += InDeltaTime;
		if (Accum >= 0.25f)
		{
			Accum = 0.f;
			Refresh();
		}
	}

private:
	using FDriveFn = TFunction<void(ULeoNarrativeSubsystem*)>;

	TSharedRef<SButton> MakeDriveButton(const FText& Label, FDriveFn Fn)
	{
		return SNew(SButton)
			.Text(Label)
			.OnClicked_Lambda([this, Fn = MoveTemp(Fn)]
			{
				if (ULeoNarrativeSubsystem* S = FindLiveSubsystem()) { Fn(S); }
				Refresh();
				return FReply::Handled();
			});
	}

	TSharedRef<SBorder> MakeBoardBox(const FText& Title, TArray<FStringRowPtr>* Rows,
		TSharedPtr<SListView<FStringRowPtr>>& OutList)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(4)
				[
					SNew(STextBlock).Text(Title)
				]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
				[
					SAssignNew(OutList, SListView<FStringRowPtr>)
					.ListItemsSource(Rows)
					.OnGenerateRow_Lambda([](FStringRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
					{
						return SNew(STableRow<FStringRowPtr>, OwnerTable)
							.Content()
							[
								SNew(STextBlock).Text(FText::FromString(*Row))
							];
					})
				]
			];
	}

	void Refresh()
	{
		if (ULeoNarrativeSubsystem* S = FindLiveSubsystem())
		{
			S->GetDebugSnapshot(State->Snap);
		}
		else
		{
			State->Snap = FLeoDebugSnapshot();
		}

		auto Fill = [](const TArray<FLeoDebugVar>& Vars, TArray<FStringRowPtr>& Rows)
		{
			Rows.Reset();
			for (const FLeoDebugVar& V : Vars)
			{
				Rows.Add(MakeShared<FString>(V.Key + TEXT(" = ") + V.Value));
			}
			if (Rows.Num() == 0) { Rows.Add(MakeShared<FString>(TEXT("（空）"))); }
		};
		Fill(State->Snap.LocalVars, State->LocalRows);
		Fill(State->Snap.GlobalVars, State->GlobalRows);

		State->EventRows.Reset();
		for (const FString& E : State->Snap.EventLog)
		{
			State->EventRows.Add(MakeShared<FString>(E));
		}
		if (State->EventRows.Num() == 0) { State->EventRows.Add(MakeShared<FString>(TEXT("（无事件）"))); }

		if (EventList.IsValid()) { EventList->RequestListRefresh(); }
		if (LocalList.IsValid()) { LocalList->RequestListRefresh(); }
		if (GlobalList.IsValid()) { GlobalList->RequestListRefresh(); }
	}

	FText BuildStatusText() const
	{
		const FLeoDebugSnapshot& S = State->Snap;
		if (!S.bActive)
		{
			return FText::FromString(TEXT("无活跃叙事会话（启动 PIE 并运行章节后可调试）"));
		}
		FString T = FString::Printf(TEXT("状态 %s ｜ 章节 %s ｜ PC %d（L%d） ｜ 锚点 %s"),
			*S.StateName, *S.Chapter, S.PC, S.Line, *S.Anchor);
		if (!S.SuspendToken.IsNone())
		{
			T += FString::Printf(TEXT(" ｜ 断点 %s"), *S.SuspendToken.ToString());
		}
		if (S.WaitRemaining > 0.f)
		{
			T += FString::Printf(TEXT(" ｜ 剩余 %.1fs"), S.WaitRemaining);
		}
		T += FString::Printf(TEXT("\n命令：%s"), *S.CommandDesc);
		T += FString::Printf(TEXT("\nauto=%s skip=%s ｜ 已读 %d ｜ 图=%s%s ｜ 注册命令 %d 个"),
			S.bAuto ? TEXT("开") : TEXT("关"), S.bSkip ? TEXT("开") : TEXT("关"),
			S.ReadTextCount, S.bGraphActive ? TEXT("是") : TEXT("否"),
			S.bGraphActive ? (*FString::Printf(TEXT("（节点 %s）"), *S.GraphNode)) : TEXT(""),
			S.CustomCommands.Num());
		return FText::FromString(T);
	}

	TSharedPtr<FLeoDebuggerState> State;
	TSharedPtr<SListView<FStringRowPtr>> LocalList;
	TSharedPtr<SListView<FStringRowPtr>> GlobalList;
	TSharedPtr<SListView<FStringRowPtr>> EventList;
	float Accum = 0.f;
};

const FName GDebuggerTabName(TEXT("LeoNarrativeDebuggerPanel"));

} // namespace

namespace LeoDebuggerPanel
{

void RegisterTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(GDebuggerTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SLeoDebuggerPanel)
				];
		}))
		.SetDisplayName(LOCTEXT("TabTitle", "Leo 叙事调试器"))
		.SetTooltipText(LOCTEXT("TabTip", "VM 状态 / 黑板 / 事件流 / 手动驱动 / 存档查看（PIE 运行中生效）"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void UnregisterTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GDebuggerTabName);
}

} // namespace LeoDebuggerPanel

#undef LOCTEXT_NAMESPACE
