#include "LeoEditorWatcher.h"

#include "LeoValidation.h"
#include "LeoVariableHarvest.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	void ShowNotification(const FText& Text, SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Text);
		Info.bUseSuccessFailIcons = true;
		Info.ExpireDuration = 6.f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(State);
		}
	}
}

void FLeoEditorWatcher::Start()
{
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Scripts");
	if (!FPaths::DirectoryExists(Dir))
	{
		return; // 工程还没有剧本目录：不监听
	}
	FDirectoryWatcherModule& DW = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
	DW.Get()->RegisterDirectoryChangedCallback_Handle(
		Dir,
		IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FLeoEditorWatcher::OnDirectoryChanged),
		WatchHandle);
	UE_LOG(LogTemp, Log, TEXT("[Leo] 已监听剧本目录: %s"), *Dir);
}

void FLeoEditorWatcher::Stop()
{
	if (WatchHandle.IsValid())
	{
		if (FDirectoryWatcherModule* DW = FModuleManager::GetModulePtr<FDirectoryWatcherModule>("DirectoryWatcher"))
		{
			const FString Dir = FPaths::ProjectContentDir() / TEXT("Scripts");
			DW->Get()->UnregisterDirectoryChangedCallback_Handle(Dir, WatchHandle);
		}
		WatchHandle.Reset();
	}
	if (DebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebounceHandle);
		DebounceHandle.Reset();
	}
}

void FLeoEditorWatcher::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	for (const FFileChangeData& C : Changes)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(C.Filename);
		if (Path.EndsWith(TEXT(".leo"), ESearchCase::IgnoreCase))
		{
			PendingFiles.AddUnique(Path);
		}
	}
	if (PendingFiles.Num() == 0) { return; }

	// 防抖：编辑器保存常触发多次回调，等 0.5s 静默后统一重编译
	if (DebounceHandle.IsValid()) { return; }
	DebounceHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("LeoScriptRecompile"), 0.5f,
		[this](float)
		{
			DebounceHandle.Reset();
			RecompilePending();
			return false; // 单次
		});
}

void FLeoEditorWatcher::RecompilePending()
{
	int32 Failed = 0;
	for (const FString& Path : PendingFiles)
	{
		if (!LeoValidation::ValidateFile(Path, true))
		{
			++Failed;
		}
	}
	if (PendingFiles.Num() > 0)
	{
		if (Failed == 0)
		{
			ShowNotification(FText::FromString(FString::Printf(TEXT("Leo 剧本校验通过（%d 个文件）"), PendingFiles.Num())),
				SNotificationItem::CS_Success);
		}
		else
		{
			ShowNotification(FText::FromString(FString::Printf(TEXT("Leo 剧本校验失败：%d/%d（详见 Output Log）"), Failed, PendingFiles.Num())),
				SNotificationItem::CS_Fail);
		}
	}
	PendingFiles.Reset();
	// 校验面板订阅此通知自动刷新
	LeoValidation::OnScriptsRevalidated.Broadcast();
	// 脚本变了 → 变量收割注册表过期（拾取器/补全下次取用时重扫）
	FLeoVariableHarvest::Get().MarkStale();
}
