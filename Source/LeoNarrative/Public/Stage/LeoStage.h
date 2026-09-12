// Stage：背景与立绘的当前状态机。订阅事件维护状态，供 UI 渲染；
// v0.1 资产未接入时由 UI 画占位色块，接入清单后尝试加载背景贴图。
#pragma once

#include "CoreMinimal.h"
#include "VM/LeoEvents.h"
#include "LeoStage.generated.h"

USTRUCT()
struct FLeoCharState
{
	GENERATED_BODY()
	FName AssetId;     // "-" 表示已移除
	FString At = TEXT("left");
	FString Pose;
	FString Motion;
};

UCLASS()
class LEONARRATIVE_API ULeoStage : public UObject
{
	GENERATED_BODY()

public:
	void HandleEvent(const FLeoEvent& Ev);

	const FSoftObjectPath& GetCurrentBgPath() const { return CurrentBgPath; }
	FName GetCurrentBg() const { return CurrentBg; }
	const TMap<FString, FLeoCharState>& GetChars() const { return Chars; }

	void SetManifest(class ULeoAssetManifest* InManifest) { Manifest = InManifest; }

private:
	FName CurrentBg;
	FSoftObjectPath CurrentBgPath;
	TMap<FString, FLeoCharState> Chars; // Slot → 状态

	UPROPERTY()
	TObjectPtr<ULeoAssetManifest> Manifest;
};
