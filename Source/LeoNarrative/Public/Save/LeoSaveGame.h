// 双档体系：全局档（已读文本 + 全局黑板）与进度档（图位置 + VM 锚点 + 局部黑板快照）。
// leo::FLeoValue 不做反射——存档侧用 FLeoSavedValue 平行结构手动转换。
#pragma once

#include "GameFramework/SaveGame.h"
#include "Script/LeoTypes.h"
#include "LeoSaveGame.generated.h"

// 反射友好的值容器（与 leo::FLeoValue 互转）
USTRUCT()
struct FLeoSavedValue
{
	GENERATED_BODY()
	// 0 Null / 1 Bool / 2 Int / 3 Float / 4 String
	UPROPERTY() int32 Type = 0;
	UPROPERTY() bool B = false;
	UPROPERTY() int64 I = 0;
	UPROPERTY() double F = 0.0;
	UPROPERTY() FString S;

	static FLeoSavedValue From(const leo::FLeoValue& V);
	static leo::FLeoValue To(const FLeoSavedValue& SV);
};

UCLASS()
class ULeoGlobalSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TSet<FString> ReadTextIds;
	UPROPERTY() TArray<FName> VarKeys;
	UPROPERTY() TArray<FLeoSavedValue> VarValues;
};

UCLASS()
class ULeoProgressSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	// 图调用栈（Subgraph 嵌套，末位为当前帧）；空表示非图模式（单章直跑）
	UPROPERTY() TArray<FSoftObjectPath> GraphAssets;
	UPROPERTY() TArray<FName> GraphNodeIds;
	// 旧版单图字段（读档兼容；新档不再写）
	UPROPERTY() FSoftObjectPath GraphAsset;
	UPROPERTY() FName GraphNodeId;
	UPROPERTY() FName Chapter;
	UPROPERTY() FName AnchorLabel;
	UPROPERTY() int32 AnchorOffset = 0;
	UPROPERTY() TArray<FName> LocalKeys;     // 局部黑板快照
	UPROPERTY() TArray<FLeoSavedValue> LocalValues;
	UPROPERTY() int64 TimestampTicks = 0;    // 保存时间（Ticks）
};
