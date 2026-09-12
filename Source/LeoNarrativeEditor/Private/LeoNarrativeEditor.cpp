// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeoNarrativeEditor.h"

#include "LeoEditorWatcher.h"
#include "LeoValidation.h"

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
			? TEXT("Leo 剧本校验全绿（详见 Output Log）")
			: FString::Printf(TEXT("Leo 剧本校验：%d 项不符合预期（详见 Output Log）"), Failures)));
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
			NSLOCTEXT("Leo", "ValidateAllTip", "编译 Content/Scripts 与 golden 语料，输出全部诊断"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(&RunValidateAll));
	}
}

void FLeoNarrativeEditorModule::StartupModule()
{
	// 菜单（Tools → LeoNarrative）与剧本目录热校验
	GMenuCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterLeoMenus));
	GWatcher.Start();
}

void FLeoNarrativeEditorModule::ShutdownModule()
{
	GWatcher.Stop();
	if (GMenuCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(GMenuCallbackHandle);
		GMenuCallbackHandle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLeoNarrativeEditorModule, LeoNarrativeEditor)
