#include "Audio/LeoAudioAdapter.h"
#include "Data/LeoAssetManifest.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoAudio, Log, All);

USoundBase* ULeoAudioAdapter::ResolveSound(FName LogicalId)
{
	if (const TObjectPtr<USoundBase>* Cached = SoundCache.Find(LogicalId))
	{
		return *Cached;
	}
	FSoftObjectPath Path;
	if (!Manifest || !Manifest->TryResolve(LogicalId, Path))
	{
		if (!WarnedIds.Contains(LogicalId))
		{
			WarnedIds.Add(LogicalId);
			UE_LOG(LogLeoAudio, Warning, TEXT("清单里没有音频逻辑名 %s（或未设置清单资产）——跳过播放"), *LogicalId.ToString());
		}
		return nullptr;
	}
	USoundBase* Sound = Cast<USoundBase>(Path.ResolveObject());
	if (!Sound)
	{
		Sound = Cast<USoundBase>(Path.TryLoad());
	}
	SoundCache.Add(LogicalId, Sound);
	return Sound;
}

void ULeoAudioAdapter::PlayBgm(FName LogicalId, float Volume, float Fade)
{
	StopBgm();
	if (LogicalId == TEXT("-")) { return; } // bgm - = 停止
	USoundBase* Sound = ResolveSound(LogicalId);
	if (!Sound || !WorldContext) { return; }
	BgmComponent = UGameplayStatics::CreateSound2D(WorldContext, Sound);
	if (BgmComponent)
	{
		BgmComponent->SetVolumeMultiplier(Volume);
		BgmComponent->Play();
		// fade 参数留给音频中间件参数化；v0.1 直接起播
	}
}

void ULeoAudioAdapter::StopBgm()
{
	if (BgmComponent)
	{
		BgmComponent->Stop();
		BgmComponent = nullptr;
	}
}

void ULeoAudioAdapter::HandleEvent(const FLeoEvent& Ev)
{
	switch (Ev.Kind)
	{
	case ELeoEventKind::Bgm:
		PlayBgm(Ev.AssetId, Ev.Volume, Ev.Fade);
		break;

	case ELeoEventKind::Se:
		if (USoundBase* Sound = ResolveSound(Ev.AssetId))
		{
			UGameplayStatics::PlaySound2D(WorldContext, Sound, Ev.Volume);
		}
		break;

	case ELeoEventKind::Voice:
		if (USoundBase* Sound = ResolveSound(Ev.AssetId))
		{
			LastVoice = UGameplayStatics::CreateSound2D(WorldContext, Sound);
			if (LastVoice) { LastVoice->Play(); }
		}
		break;

	case ELeoEventKind::ChapterEnd:
		StopBgm();
		break;

	default:
		break;
	}
}
