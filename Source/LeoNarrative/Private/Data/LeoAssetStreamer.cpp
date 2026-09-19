#include "Data/LeoAssetStreamer.h"
#include "Data/LeoAssetManifest.h"
#include "ScriptRuntime/LeoScriptRegistry.h"
#include "Stage/LeoSequencerPerformer.h"

#include "Engine/AssetManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoStream, Log, All);

int32 FLeoAssetStreamer::PreloadChapter(ULeoScriptRegistry& Registry, FName Chapter, FStreamableDelegate OnDone)
{
	MissedIds.Reset();
	LastResolvedCount = 0;
	Handle.Reset(); // 覆盖上一章：旧句柄释放，资产引用交还 GC

	TSharedPtr<leo::FLeoProgram> Program;
	if (!Registry.TryGetProgram(Chapter, Program) || !Program)
	{
		UE_LOG(LogLeoStream, Warning, TEXT("预载章节不存在或编译失败: %s"), *Chapter.ToString());
		OnDone.ExecuteIfBound(); // 立即完成，转场流程不被卡住
		return 0;
	}

	// 收割整章资产引用（框架预注册命令的参数声明由 seq 所属的 Performer 单源提供）
	TArray<LeoBridge::FLeoCustomAssetArgInfo> FrameworkArgs;
	ULeoSequencerPerformer::GetFrameworkAssetArgs(FrameworkArgs);
	TArray<LeoBridge::FLeoAssetRefInfo> Refs;
	LeoBridge::CollectProgramAssetRefs(*Program, FrameworkArgs, Refs);

	// 类别感知解析：分表命中优先，旧平表回落，跨表错位留给校验报错（宁缺毋滥）
	TArray<FSoftObjectPath> Paths;
	if (Manifest.IsValid())
	{
		for (const LeoBridge::FLeoAssetRefInfo& Ref : Refs)
		{
			FSoftObjectPath Path;
			if (Manifest->TryResolve(Ref.Kind, FName(*Ref.Id), Path))
			{
				Paths.AddUnique(Path);
			}
			else
			{
				MissedIds.Add(FName(*Ref.Id));
			}
		}
	}
	else
	{
		for (const LeoBridge::FLeoAssetRefInfo& Ref : Refs) { MissedIds.Add(FName(*Ref.Id)); }
	}
	LastResolvedCount = Paths.Num();

	if (MissedIds.Num() > 0)
	{
		FString MissedList;
		for (int32 i = 0; i < MissedIds.Num() && i < 8; ++i)
		{
			MissedList += (i ? TEXT(", ") : TEXT("")) + MissedIds[i].ToString();
		}
		if (MissedIds.Num() > 8) { MissedList += FString::Printf(TEXT(", …共 %d 个"), MissedIds.Num()); }
		UE_LOG(LogLeoStream, Warning, TEXT("预载 %s：收割 %d 命中 %d，清单未命中（表现层届时同步兜底/占位）: %s"),
			*Chapter.ToString(), Refs.Num(), Paths.Num(), *MissedList);
	}
	else
	{
		UE_LOG(LogLeoStream, Display, TEXT("预载 %s：收割 %d 命中 %d"), *Chapter.ToString(), Refs.Num(), Paths.Num());
	}

	if (Paths.Num() == 0)
	{
		OnDone.ExecuteIfBound();
		return 0;
	}

	FStreamableManager& SM = UAssetManager::GetStreamableManager();
	Handle = SM.RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda(
		[this, OnDone]()
		{
			UE_LOG(LogLeoStream, Display, TEXT("预载完成：%d 条资产常驻"), LastResolvedCount);
			OnDone.ExecuteIfBound();
		}));
	if (!Handle.IsValid())
	{
		// 理论不可达（RequestAsyncLoad 非空列表必有句柄）：保底标记完成，不卡转场
		OnDone.ExecuteIfBound();
	}
	return Paths.Num();
}

void FLeoAssetStreamer::ReleaseHandle()
{
	Handle.Reset();
}

bool FLeoAssetStreamer::IsLoadInProgress() const
{
	return Handle.IsValid() && Handle->IsLoadingInProgress();
}
