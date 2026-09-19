// Audio 适配器：BGM/SE/Voice → 清单解析 → PlaySound2D。
// 使用点先查内存（章节预载常驻时零加载），未预载才同步加载兜底（缺失时每逻辑名只警告一次）。
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
