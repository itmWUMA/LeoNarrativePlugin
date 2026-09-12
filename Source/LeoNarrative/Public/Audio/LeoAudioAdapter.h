// Audio 适配器：BGM/SE/Voice → 清单解析 → PlaySound2D。
// v0.1 同步加载（缺失时对每个逻辑名只警告一次）。
#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundBase.h"
#include "VM/LeoEvents.h"
#include "Components/AudioComponent.h"
#include "LeoAudioAdapter.generated.h"

UCLASS()
class LEONARRATIVE_API ULeoAudioAdapter : public UObject
{
	GENERATED_BODY()

public:
	void HandleEvent(const FLeoEvent& Ev);
	void SetManifest(class ULeoAssetManifest* InManifest) { Manifest = InManifest; }
	void StopBgm();
	void SetWorldContext(UObject* InCtx) { WorldContext = InCtx; }

private:
	USoundBase* ResolveSound(FName LogicalId);
	void PlayBgm(FName LogicalId, float Volume, float Fade);

	UPROPERTY()
	TObjectPtr<ULeoAssetManifest> Manifest;
	UPROPERTY()
	TObjectPtr<UAudioComponent> BgmComponent;   // 当前 BGM（切歌/停止用）
	UPROPERTY()
	TObjectPtr<UAudioComponent> LastVoice;      // 防 GC

	UPROPERTY()
	TObjectPtr<UObject> WorldContext;

	TMap<FName, TObjectPtr<USoundBase>> SoundCache;
	TSet<FName> WarnedIds; // 缺失警告去重
};
