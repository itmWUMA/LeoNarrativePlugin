// Content 热校验：.leo 保存后 0.5s 防抖重编译（日志+通知反馈）；
// Content/L10n 下 CSV 保存同样防抖 → 广播刷新（校验中心的本地化核对即时更新）
#pragma once

#include "CoreMinimal.h"

class FLeoEditorWatcher
{
public:
	void Start();
	void Stop();

private:
	void OnScriptsDirChanged(const TArray<struct FFileChangeData>& Changes);
	void OnL10nDirChanged(const TArray<struct FFileChangeData>& Changes);
	void RecompilePending();
	void FlushL10nPending();

	FDelegateHandle ScriptWatchHandle;
	FDelegateHandle L10nWatchHandle;
	FString WatchedScriptsDir;  // 注册时缓存（Stop 注销需同路径；关闭期不可再读设置 CDO）
	FString WatchedL10nDir;
	TArray<FString> PendingFiles;   // 待重编译的 .leo
	bool bL10nPending = false;      // CSV 有变更待刷新
	FTSTicker::FDelegateHandle DebounceHandle;
	FTSTicker::FDelegateHandle L10nDebounceHandle;
};
