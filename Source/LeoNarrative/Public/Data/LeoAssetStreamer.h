// 章节资产流送器：预载 = 剧本静态分析收割逻辑名 → 清单分表解析 → FStreamableManager
// 异步批量加载。句柄存活期间资产常驻（使用点 ResolveObject/TryLoad 直接命中内存，
// 零同步加载卡顿）；释放句柄后引用交还 GC。
//
// 典型时序：转场遮罩下 PreloadChapter → 等完成 → StartChapter（消费点已就绪）。
// 与 VM/表现层完全解耦：不广播事件、不改执行流，只是让资产提前进内存。
#pragma once

#include "CoreMinimal.h"
#include "Data/LeoAssetManifest.h"
#include "Engine/StreamableManager.h"

class ULeoScriptRegistry;

class FLeoAssetStreamer
{
public:
	void SetManifest(ULeoAssetManifest* InManifest) { Manifest = InManifest; }

	// 章节预载。返回清单命中的条目数（0 = 无清单/全缺失/章节不存在）；
	// 重复调用覆盖上一章句柄（旧引用释放）。OnDone 在全部就绪后游戏线程回调（可为空；
	// 章节不存在时立即同步完成）。清单未命中的逻辑名进 GetMissedIds（每章覆盖）。
	int32 PreloadChapter(ULeoScriptRegistry& Registry, FName Chapter,
		FStreamableDelegate OnDone = FStreamableDelegate());

	void ReleaseHandle();   // 主动释放（切章/低内存）；无进行中加载时为空操作
	bool IsLoadInProgress() const;

	int32 GetResolvedCount() const { return LastResolvedCount; }
	const TArray<FName>& GetMissedIds() const { return MissedIds; }

private:
	TWeakObjectPtr<ULeoAssetManifest> Manifest;
	TSharedPtr<FStreamableHandle> Handle;
	int32 LastResolvedCount = 0;
	TArray<FName> MissedIds;
};
