// Sequencer 适配器：seq 命令 → 清单解析 → LevelSequence 播放 → OnFinished 回传断点。
// 与 LeoStage/LeoAudioAdapter 同构的表现层订阅者——M6 断点模式从"玩法"推广到"演出"。
// 脚本用法：seq <逻辑名> wait=1 rate=1.0 start=0 loop=0
//   wait=1  阻塞到播完；黑板键 seq = 1(自然播完) / 0(缺资源或被跳过)，脚本 jumpif 分流
//   wait=0  即刻继续（氛围循环，跨章节存活，由游戏侧 StopAll 收尾）
#pragma once

#include "CoreMinimal.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "VM/LeoEvents.h"
#include "LeoSequencerPerformer.generated.h"

class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class ULeoAssetManifest;
class ULeoNarrativeSubsystem;

UCLASS()
class LEONARRATIVE_API ULeoSequencerPerformer : public UObject
{
	GENERATED_BODY()

public:
	// 注册 seq 为严格自定义命令（模块启动期调用，须早于任何剧本编译）
	static void RegisterSeqCommand();

	// 框架预注册命令中携带资产逻辑名的参数位（当前仅 seq：第 0 个位置参数，类别 "seq"）。
	// 清单核对与章节预载经此取声明——seq 的知识保持单源，内核零硬编码。
	static void GetFrameworkAssetArgs(TArray<LeoBridge::FLeoCustomAssetArgInfo>& Out)
	{
		Out.Add({ TEXT("seq"), 0, TEXT("seq") });
	}

	void HandleEvent(const FLeoEvent& Ev);
	void SetManifest(ULeoAssetManifest* InManifest) { Manifest = InManifest; }
	void SetWorldContext(UObject* InCtx) { WorldContext = InCtx; }
	void SetOwner(ULeoNarrativeSubsystem* InOwner) { Owner = InOwner; }

	// 停止全部序列。bResumeVM=true：若正阻塞在 seq 断点，以 0 恢复（跳过过场）
	void StopAll(bool bResumeVM);
	bool IsPlaying(FName LogicalId) const;

	// 手动泵序列播放（正常运行靠世界 tick；无头/命令行环境世界不 tick 时由测试侧调用）
	void TickSequencesManually(float DeltaSeconds);

	// 测试缝：为逻辑名注入运行时构造的序列（合成资产无包路径，清单无法表达）
	void SetSequenceOverride(FName LogicalId, ULevelSequence* Seq)
	{
		SequenceOverrides.Add(LogicalId, Seq);
	}

private:
	UFUNCTION()
	void OnSequenceFinished();
	// 恢复统一延迟一帧：EmitCustomEvent 在处理器内同步广播，此刻 Suspend 尚未执行，
	// 同步 ResumeWith 会被状态检查拒绝（零长度序列瞬间完成也走这条保险）
	void DeferResume(FName Token, int32 Payload);
	void ResumePending(FName Token, int32 Payload);
	// 章节收束：只停阻塞型（wait=0 氛围序列跨章节存活）
	void StopBlocking();
	ULevelSequence* ResolveSequence(FName LogicalId);

	UPROPERTY()
	TObjectPtr<ULeoAssetManifest> Manifest;
	UPROPERTY()
	TObjectPtr<UObject> WorldContext;
	UPROPERTY()
	TObjectPtr<ULeoNarrativeSubsystem> Owner;

	struct FActiveSeq
	{
		TWeakObjectPtr<ULevelSequencePlayer> Player;
		TWeakObjectPtr<ALevelSequenceActor> Actor;
		bool bBlocking = false;
	};
	TMap<FName, FActiveSeq> Active;

	UPROPERTY()
	TMap<FName, TObjectPtr<ULevelSequence>> SequenceOverrides;

	// wait=1 挂起中的断点名（当前实现固定 "seq"）
	FName PendingResumeToken;
	TSet<FName> WarnedIds; // 缺失警告去重
};
