// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeoNarrativeEditor.h"

#include "LeoDebuggerPanel.h"
#include "LeoEditorWatcher.h"
#include "LeoValidation.h"
#include "LeoValidationPanel.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FLeoNarrativeEditorModule"

namespace
{
	FLeoEditorWatcher GWatcher;
	FDelegateHandle GMenuCallbackHandle;

	void RunValidateAll()
	{
		const int32 Failures = LeoValidation::ValidateAll();
		FNotificationInfo Info(FText::FromString(Failures == 0
			? TEXT("Leo 剧本校验全绿（详见 Output Log / Leo 校验中心）")
			: FString::Printf(TEXT("Leo 剧本校验：%d 项失败（详见 Output Log / Leo 校验中心）"), Failures)));
		Info.bUseSuccessFailIcons = true;
		Info.ExpireDuration = 8.f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(Failures == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void RegisterLeoMenus()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
		if (!Menu) { return; }
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("LeoNarrative"));
		Section.Label = NSLOCTEXT("Leo", "SectionLabel", "LeoNarrative");
		Section.AddMenuEntry(
			TEXT("LeoValidateAll"),
			NSLOCTEXT("Leo", "ValidateAllLabel", "校验所有 .leo 剧本"),
			NSLOCTEXT("Leo", "ValidateAllTip", "编译 Content/Scripts 与 golden 语料 + 清单资产核对，输出全部诊断"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(&RunValidateAll));
		Section.AddMenuEntry(
			TEXT("LeoValidationPanel"),
			NSLOCTEXT("Leo", "ValidationPanelLabel", "打开校验中心"),
			NSLOCTEXT("Leo", "ValidationPanelTip", "文件 × 诊断面板；剧本保存后自动刷新"),
			FSlateIcon(),
			FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(FName("LeoNarrativeValidationPanel")); }));
		Section.AddMenuEntry(
			TEXT("LeoDebuggerPanel"),
			NSLOCTEXT("Leo", "DebuggerPanelLabel", "打开叙事调试器"),
			NSLOCTEXT("Leo", "DebuggerPanelTip", "VM 状态 / 黑板 / 事件流 / 手动驱动 / 存档查看（PIE 运行中生效）"),
			FSlateIcon(),
			FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(FName("LeoNarrativeDebuggerPanel")); }));
	}
}

void FLeoNarrativeEditorModule::StartupModule()
{
	// 菜单（Tools → LeoNarrative）与剧本目录热校验
	GMenuCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterLeoMenus));
	// 面板：校验中心（P0）+ 叙事调试器（P1）
	LeoValidationPanel::RegisterTab();
	LeoDebuggerPanel::RegisterTab();
	GWatcher.Start();
}

void FLeoNarrativeEditorModule::ShutdownModule()
{
	GWatcher.Stop();
	LeoDebuggerPanel::UnregisterTab();
	LeoValidationPanel::UnregisterTab();
	if (GMenuCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(GMenuCallbackHandle);
		GMenuCallbackHandle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLeoNarrativeEditorModule, LeoNarrativeEditor)
