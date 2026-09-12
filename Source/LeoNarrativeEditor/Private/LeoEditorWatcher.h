// Content/Scripts 热校验：.leo 保存后 0.5s 防抖重编译，日志+通知反馈
#pragma once

#include "CoreMinimal.h"

class FLeoEditorWatcher
{
public:
	void Start();
	void Stop();

private:
	void OnDirectoryChanged(const TArray<struct FFileChangeData>& Changes);
	void RecompilePending();

	FDelegateHandle WatchHandle;
	TArray<FString> PendingFiles;
	FTSTicker::FDelegateHandle DebounceHandle;
};
