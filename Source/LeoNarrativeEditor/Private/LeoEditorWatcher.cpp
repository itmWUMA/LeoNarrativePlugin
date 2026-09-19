#include "LeoEditorWatcher.h"
#include "Settings/LeoNarrativeSettings.h"

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
	WatchedScriptsDir = ULeoNarrativeSettings::Get()->GetScriptsDirPath();
	const FString& Dir = WatchedScriptsDir;
	if (FPaths::DirectoryExists(Dir))
	{
		FDirectoryWatcherModule& DW = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
		DW.Get()->RegisterDirectoryChangedCallback_Handle(
			Dir,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FLeoEditorWatcher::OnScriptsDirChanged),
			ScriptWatchHandle);
		UE_LOG(LogTemp, Log, TEXT("[Leo] 已监听剧本目录: %s"), *Dir);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[Leo] 工程还没有剧本目录，跳过监听"));
	}

	// 译文目录（递归含各语言子目录）：CSV 保存 → 校验中心刷新本地化核对
	WatchedL10nDir = ULeoNarrativeSettings::Get()->GetL10nDirPath();
	const FString& L10nDir = WatchedL10nDir;
	if (FPaths::DirectoryExists(L10nDir))
	{
		FDirectoryWatcherModule& DW = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
		DW.Get()->RegisterDirectoryChangedCallback_Handle(
			L10nDir,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FLeoEditorWatcher::OnL10nDirChanged),
			L10nWatchHandle);
		UE_LOG(LogTemp, Log, TEXT("[Leo] 已监听译文目录: %s"), *L10nDir);
	}
}

void FLeoEditorWatcher::Stop()
{
	if (ScriptWatchHandle.IsValid())
	{
		if (FDirectoryWatcherModule* DW = FModuleManager::GetModulePtr<FDirectoryWatcherModule>("DirectoryWatcher"))
		{
			DW->Get()->UnregisterDirectoryChangedCallback_Handle(WatchedScriptsDir, ScriptWatchHandle);
		}
		ScriptWatchHandle.Reset();
	}
	if (L10nWatchHandle.IsValid())
	{
		if (FDirectoryWatcherModule* DW = FModuleManager::GetModulePtr<FDirectoryWatcherModule>("DirectoryWatcher"))
		{
			DW->Get()->UnregisterDirectoryChangedCallback_Handle(WatchedL10nDir, L10nWatchHandle);
		}
		L10nWatchHandle.Reset();
	}
	if (DebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebounceHandle);
		DebounceHandle.Reset();
	}
	if (L10nDebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(L10nDebounceHandle);
		L10nDebounceHandle.Reset();
	}
}

void FLeoEditorWatcher::OnScriptsDirChanged(const TArray<FFileChangeData>& Changes)
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

void FLeoEditorWatcher::OnL10nDirChanged(const TArray<FFileChangeData>& Changes)
{
	for (const FFileChangeData& C : Changes)
	{
		if (C.Filename.EndsWith(TEXT(".csv"), ESearchCase::IgnoreCase))
		{
			bL10nPending = true;
			break;
		}
	}
	if (!bL10nPending) { return; }

	if (L10nDebounceHandle.IsValid()) { return; }
	L10nDebounceHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("LeoL10nRefresh"), 0.5f,
		[this](float)
		{
			L10nDebounceHandle.Reset();
			FlushL10nPending();
			return false;
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

void FLeoEditorWatcher::FlushL10nPending()
{
	if (!bL10nPending) { return; }
	bL10nPending = false;
	// CSV 的对错（解析/撞号/STALE…）由校验中心面板呈现，这里只通知 + 广播刷新
	ShowNotification(FText::FromString(TEXT("Leo 译文 CSV 已更新（核对结果见校验中心）")),
		SNotificationItem::CS_None);
	LeoValidation::OnScriptsRevalidated.Broadcast();
}
