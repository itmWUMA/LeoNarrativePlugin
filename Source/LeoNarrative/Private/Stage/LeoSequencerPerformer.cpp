// Sequencer 适配器实现。链路：seq 事件 → 清单/测试缝解析 → CreateLevelSequencePlayer
// → OnFinished → ResumeWith("seq", 1)。缺资源以 0 恢复（永不软锁，脚本走兜底分支）。
#include "Stage/LeoSequencerPerformer.h"

#include "Data/LeoAssetManifest.h"
#include "Subsystem/LeoNarrativeSubsystem.h"
#include "VM/LeoVM.h"

#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoSequencer, Log, All);

namespace
{
	// ExtraParams 里的 "1"/"0" 风格布尔
	bool ParseFlag(const TMap<FName, FString>& Params, FName Key, bool Default)
	{
		const FString* V = Params.Find(Key);
		if (!V) { return Default; }
		return !(*V == TEXT("0") || *V == TEXT("false") || V->IsEmpty());
	}

	float ParseFloat(const TMap<FName, FString>& Params, FName Key, float Default)
	{
		const FString* V = Params.Find(Key);
		float Out = Default;
		return (V && V->TrimStartAndEnd().IsNumeric()) ? FCString::Atof(**V) : Default;
	}
}

void ULeoSequencerPerformer::RegisterSeqCommand()
{
	// 处理器只做纯 VM 操作（广播事件 + 挂起）；播放与恢复在本适配器（表现层）
	const auto HandleSeq = [](ULeoVM& VM, const leo::FLeoCommand& C) -> ELeoCustomResult
	{
		bool bWait = true; // ADV 默认：过场播完才继续
		for (const leo::FLeoParam& P : C.Params)
		{
			if (P.Key == "wait") { bWait = (P.Value != "0" && P.Value != "false"); }
		}
		VM.EmitCustomEvent(C);
		if (bWait)
		{
			VM.Suspend(TEXT("seq"));
			return ELeoCustomResult::Suspend;
		}
		return ELeoCustomResult::Next;
	};

	LeoBridge::FLeoCmdSpec Spec;
	Spec.Name = TEXT("seq");
	Spec.MinArgs = 1; // 序列逻辑名（清单映射到 ULevelSequence 资产）
	Spec.MaxArgs = 1;
	Spec.AllowedParams = { TEXT("wait"), TEXT("rate"), TEXT("start"), TEXT("loop") };
	ULeoVM::RegisterCustomCommand(TEXT("seq"), Spec, HandleSeq);
}

void ULeoSequencerPerformer::HandleEvent(const FLeoEvent& Ev)
{
	if (Ev.Kind == ELeoEventKind::ChapterEnd)
	{
		StopBlocking();
		return;
	}
	if (Ev.Kind != ELeoEventKind::Custom || Ev.CustomName != TEXT("seq")) { return; }

	const FName LogicalId(*Ev.ExtraParams.FindRef(TEXT("arg0")));
	const bool bWait = ParseFlag(Ev.ExtraParams, TEXT("wait"), true);
	const bool bLoop = ParseFlag(Ev.ExtraParams, TEXT("loop"), false);
	const float Rate = ParseFloat(Ev.ExtraParams, TEXT("rate"), 1.f);
	const float StartTime = ParseFloat(Ev.ExtraParams, TEXT("start"), 0.f);

	ULevelSequence* Seq = ResolveSequence(LogicalId);
	if (!Seq)
	{
		if (!WarnedIds.Contains(LogicalId))
		{
			WarnedIds.Add(LogicalId);
			UE_LOG(LogLeoSequencer, Warning, TEXT("序列逻辑名无清单映射或资产加载失败: %s"), *LogicalId.ToString());
		}
		if (bWait) { DeferResume(TEXT("seq"), 0); } // 缺资源不软锁：走脚本兜底分支
		return;
	}

	// 同名重播：先停旧的
	if (FActiveSeq* Old = Active.Find(LogicalId))
	{
		if (ULevelSequencePlayer* P = Old->Player.Get()) { P->Stop(); }
		if (ALevelSequenceActor* A = Old->Actor.Get()) { A->Destroy(); }
	}

	FMovieSceneSequencePlaybackSettings Settings;
	Settings.PlayRate = Rate > 0.f ? Rate : 1.f;
	Settings.LoopCount.Value = bLoop ? -1 : 0; // 0=不循环，-1=无限循环
	Settings.StartTime = StartTime;

	ALevelSequenceActor* Actor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(WorldContext, Seq, Settings, Actor);
	if (!Player || !Actor)
	{
		UE_LOG(LogLeoSequencer, Warning, TEXT("CreateLevelSequencePlayer 失败: %s"), *LogicalId.ToString());
		if (bWait) { DeferResume(TEXT("seq"), 0); }
		return;
	}

	FActiveSeq& A = Active.Add(LogicalId);
	A.Player = Player;
	A.Actor = Actor;
	A.bBlocking = bWait;
	if (bWait)
	{
		PendingResumeToken = TEXT("seq");
		Player->OnFinished.AddDynamic(this, &ULeoSequencerPerformer::OnSequenceFinished);
	}
	Player->Play();
	UE_LOG(LogLeoSequencer, Display, TEXT("▶ 序列 %s（wait=%d rate=%.2f loop=%d）"),
		*LogicalId.ToString(), bWait ? 1 : 0, Settings.PlayRate, bLoop ? 1 : 0);
}

void ULeoSequencerPerformer::OnSequenceFinished()
{
	if (PendingResumeToken.IsNone()) { return; } // StopAll 已接管 / 非自然完成
	DeferResume(PendingResumeToken, 1); // 1 = 自然播完
}

void ULeoSequencerPerformer::StopAll(bool bResumeVM)
{
	const FName TokenToResume = (bResumeVM && !PendingResumeToken.IsNone()) ? PendingResumeToken : NAME_None;
	PendingResumeToken = NAME_None;
	for (TPair<FName, FActiveSeq>& KV : Active)
	{
		if (ULevelSequencePlayer* P = KV.Value.Player.Get()) { P->Stop(); }
		if (ALevelSequenceActor* A = KV.Value.Actor.Get()) { A->Destroy(); }
	}
	Active.Reset();
	if (!TokenToResume.IsNone())
	{
		// 跳过过场：StopAll 由游戏侧调用（不在处理器内），可直接恢复
		ResumePending(TokenToResume, 0);
	}
}

void ULeoSequencerPerformer::StopBlocking()
{
	for (TPair<FName, FActiveSeq>& KV : Active)
	{
		if (!KV.Value.bBlocking) { continue; }
		if (ULevelSequencePlayer* P = KV.Value.Player.Get()) { P->Stop(); }
		if (ALevelSequenceActor* A = KV.Value.Actor.Get()) { A->Destroy(); }
		KV.Value = {};
	}
	PendingResumeToken = NAME_None;
}

bool ULeoSequencerPerformer::IsPlaying(FName LogicalId) const
{
	const FActiveSeq* A = Active.Find(LogicalId);
	return A && A->Player.IsValid() && !A->Player->IsPaused();
}

void ULeoSequencerPerformer::TickSequencesManually(float DeltaSeconds)
{
	for (TPair<FName, FActiveSeq>& KV : Active)
	{
		if (ULevelSequencePlayer* P = KV.Value.Player.Get()) { P->Update(DeltaSeconds); }
	}
}

ULevelSequence* ULeoSequencerPerformer::ResolveSequence(FName LogicalId)
{
	if (TObjectPtr<ULevelSequence>* Override = SequenceOverrides.Find(LogicalId))
	{
		return *Override;
	}
	if (Manifest)
	{
		FSoftObjectPath Path;
		if (Manifest->TryResolve(LogicalId, Path))
		{
			return Cast<ULevelSequence>(Path.TryLoad());
		}
	}
	return nullptr;
}

void ULeoSequencerPerformer::DeferResume(FName Token, int32 Payload)
{
	TWeakObjectPtr<ULeoSequencerPerformer> WeakThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis, Token, Payload](float)
		{
			if (ULeoSequencerPerformer* P = WeakThis.Get())
			{
				P->ResumePending(Token, Payload);
			}
			return false; // 一次性
		}));
}

void ULeoSequencerPerformer::ResumePending(FName Token, int32 Payload)
{
	PendingResumeToken = NAME_None;
	if (!Owner) { return; }
	const leo::FLeoValue V = leo::FLeoValue::MakeInt(Payload);
	if (Owner->ResumeWith(Token, V))
	{
		UE_LOG(LogLeoSequencer, Display, TEXT("■ 序列断点 %s 恢复（结果 %d）"), *Token.ToString(), Payload);
	}
	else
	{
		UE_LOG(LogLeoSequencer, Verbose, TEXT("断点 %s 恢复被忽略（VM 未在等待）"), *Token.ToString());
	}
}

// ---- 演示运行器：chapter03 成功分支（合成 0.5s 序列，无需编辑器资产）----
// leo.autotest chapter03 走缺资源兜底分支（工程无清单）；本命令注入合成序列验证成功分支。
static ULeoNarrativeSubsystem* GetLeoSubsystemForSeqDemo()
{
	if (!GEngine) { return nullptr; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.OwningGameInstance)
		{
			return Ctx.OwningGameInstance->GetSubsystem<ULeoNarrativeSubsystem>();
		}
	}
	return nullptr;
}

static void RunSeqDemo(ULeoNarrativeSubsystem* S, ULeoSequencerPerformer* Perf)
{
	if (!S->StartChapter(TEXT("chapter03"))) { return; }

	// 合成序列：空轨道 + 0.5s 播放区间（测试缝注入，不经过清单）
	ULevelSequence* Seq = NewObject<ULevelSequence>();
	Seq->Initialize();
	if (UMovieScene* MS = Seq->GetMovieScene())
	{
		const int32 Duration = MS->GetTickResolution().AsFrameTime(0.5f).RoundToFrame().Value;
		MS->SetPlaybackRange(FFrameNumber(0), Duration);
	}
	Perf->SetSequenceOverride(TEXT("lab_intro_cut"), Seq);
	UE_LOG(LogLeoSequencer, Display, TEXT("[demo-seq] chapter03 开始：合成序列 0.5s 后自然完成"));

	static FTSTicker::FDelegateHandle DemoHandle;
	FTSTicker::GetCoreTicker().RemoveTicker(DemoHandle);
	DemoHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S, Perf](float Dt)
	{
		ULeoVM* VM = S->GetActiveVM();
		if (!VM) { return false; }
		static float ExtWait = 0.f;
		switch (VM->GetState())
		{
		case ELeoVMState::WaitClick:
			ExtWait = 0.f;
			S->Advance();
			break;
		case ELeoVMState::WaitExternal:
			// 等序列自然完成（不代打 ResumeWith）；无头环境世界不 tick，手动泵播放器
			Perf->TickSequencesManually(Dt);
			ExtWait += Dt;
			if (ExtWait >= 5.f)
			{
				UE_LOG(LogLeoSequencer, Warning, TEXT("[demo-seq] 序列超时，以 0 恢复"));
				S->ResumeWith(TEXT("seq"), leo::FLeoValue::MakeInt(0));
				ExtWait = 0.f;
			}
			break;
		case ELeoVMState::Finished:
		{
			leo::FLeoValue V;
			const bool bOk = VM->GetLocalBlackboard() && VM->GetLocalBlackboard()->GetValue(TEXT("seq"), V) && V.AsDouble() >= 1.0;
			UE_LOG(LogLeoSequencer, Display, TEXT("[demo-seq] chapter03 完结（%s 分支）"),
				bOk ? TEXT("过场播完 cut_done") : TEXT("兜底"));
			return false;
		}
		default: break;
		}
		return true;
	}));
}

static FAutoConsoleCommand GLeoDemoSeq(
	TEXT("leo.demoseq"), TEXT("运行 chapter03 过场示例（合成 0.5s 序列，验证 OnFinished→ResumeWith 链路）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		// 子系统可能未就绪（-ExecCmds frame 0 触发）：ticker 轮询到就绪再跑
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			ULeoNarrativeSubsystem* S = GetLeoSubsystemForSeqDemo();
			ULeoSequencerPerformer* Perf = S ? S->GetSequencer() : nullptr;
			if (!S || !Perf) { return true; }
			RunSeqDemo(S, Perf);
			return false; // 单次
		}));
	}));
