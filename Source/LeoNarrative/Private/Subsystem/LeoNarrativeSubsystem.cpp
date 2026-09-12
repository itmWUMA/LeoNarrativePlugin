#include "Subsystem/LeoNarrativeSubsystem.h"
#include "VM/LeoVM.h"
#include "Audio/LeoAudioAdapter.h"
#include "Data/LeoAssetManifest.h"
#include "Data/LeoScenarioGraph.h"
#include "Presentation/LeoDialogueWidget.h"
#include "Save/LeoSaveGame.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "Stage/LeoStage.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoNarrative, Log, All);

void ULeoNarrativeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GlobalBB = NewObject<UNarrativeBlackboard>(this);
	GlobalBB->AddToRoot(); // 全局黑板跨章节存活，防 GC
	Registry = NewObject<ULeoScriptRegistry>(this);
	// 模块启动期已注册的自定义命令（严格 spec / 宽松名单）先同步给注册表，再首次编译
	Registry->CustomCommandSpecs = ULeoVM::GetStrictCommandSpecs();
	Registry->CustomCommandNames = ULeoVM::GetLenientCommandNames();
	Registry->LoadAndCompileAll();

	// 表现层（事件订阅者；VM 广播 → 子系统转发 → 这里消费）
	Stage = NewObject<ULeoStage>(this);
	Audio = NewObject<ULeoAudioAdapter>(this);
	Audio->SetWorldContext(GetGameInstance());
	OnLeoEvent.AddUObject(Stage, &ULeoStage::HandleEvent);
	OnLeoEvent.AddUObject(Audio, &ULeoAudioAdapter::HandleEvent);

	// GameInstanceSubsystem 没有 Tick，用核心 Ticker 驱动 VM（游戏线程）
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULeoNarrativeSubsystem::TickVM));
	UE_LOG(LogLeoNarrative, Log, TEXT("LeoNarrative 子系统初始化完成"));
}

void ULeoNarrativeSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	if (GlobalBB) { GlobalBB->RemoveFromRoot(); }
	Stop();
	Super::Deinitialize();
}

bool ULeoNarrativeSubsystem::StartChapter(FName Chapter)
{
	return StartChapterInternal(Chapter, NAME_None, 0);
}

bool ULeoNarrativeSubsystem::StartChapterAt(FName Chapter, FName Label, int32 Offset)
{
	return StartChapterInternal(Chapter, Label, Offset);
}

bool ULeoNarrativeSubsystem::StartChapterInternal(FName Chapter, FName Label, int32 Offset)
{
	if (!Registry)
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("注册表未初始化"));
		return false;
	}
	TSharedPtr<leo::FLeoProgram> Program;
	if (!Registry->TryGetProgram(Chapter, Program))
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("章节不存在或编译失败: %s"), *Chapter.ToString());
		return false;
	}
	Stop();

	// 局部黑板每章新建，父链指向全局
	UNarrativeBlackboard* LocalBB = NewObject<UNarrativeBlackboard>(this);
	LocalBB->Parent = GlobalBB;

	ActiveVM = NewObject<ULeoVM>(this);
	ActiveVM->Init(Chapter, Program, LocalBB, GlobalBB);
	ActiveVM->OnEvent.AddUObject(this, &ULeoNarrativeSubsystem::HandleVMEvent);

	if (Label != NAME_None)
	{
		if (!ActiveVM->RestoreAnchor(Label, Offset))
		{
			UE_LOG(LogLeoNarrative, Warning, TEXT("锚点恢复失败: %s/%s+%d，从头开始"), *Chapter.ToString(), *Label.ToString(), Offset);
		}
	}
	ActiveVM->Tick(0.f); // 立即执行到第一个阻塞点
	return true;
}

void ULeoNarrativeSubsystem::Stop()
{
	if (ActiveVM)
	{
		ActiveVM->OnEvent.RemoveAll(this);
		ActiveVM = nullptr;
	}
}

bool ULeoNarrativeSubsystem::Advance()
{
	return ActiveVM && ActiveVM->Advance();
}

bool ULeoNarrativeSubsystem::Choose(int32 Index)
{
	return ActiveVM && ActiveVM->Choose(Index);
}

bool ULeoNarrativeSubsystem::ReloadScripts()
{
	if (!Registry) { return false; }
	return Registry->LoadAndCompileAll() > 0 || Registry->GetChapterNames().Num() == 0;
}

void ULeoNarrativeSubsystem::RegisterCommand(FName Name, const LeoBridge::FLeoCmdSpec& Spec, ULeoVM::FCustomHandler Handler)
{
	ULeoVM::RegisterCustomCommand(Name, Spec, std::move(Handler));
	RefreshCommandRegistry();
	RegisteredCommandNames.AddUnique(Name.ToString());
}

void ULeoNarrativeSubsystem::RegisterCommandHandler(FName Name, ULeoVM::FCustomHandler Handler)
{
	ULeoVM::RegisterCustomHandler(Name, std::move(Handler));
	RefreshCommandRegistry();
	RegisteredCommandNames.AddUnique(Name.ToString());
}

void ULeoNarrativeSubsystem::RefreshCommandRegistry()
{
	if (Registry)
	{
		Registry->CustomCommandSpecs = ULeoVM::GetStrictCommandSpecs();
		Registry->CustomCommandNames = ULeoVM::GetLenientCommandNames();
	}
}

bool ULeoNarrativeSubsystem::ResumeWith(FName Token, const leo::FLeoValue& Payload)
{
	return ActiveVM && ActiveVM->ResumeWith(Token, Payload);
}

void ULeoNarrativeSubsystem::HandleVMEvent(const FLeoEvent& Ev)
{
	// 关键事件留 Display 级日志：无 UI 场景（命令行/自动化）也能看到叙事流
	switch (Ev.Kind)
	{
	case ELeoEventKind::ChapterStart:
		UE_LOG(LogLeoNarrative, Display, TEXT("── 章节开始: %s ──"), *Ev.Chapter.ToString());
		break;
	case ELeoEventKind::Text:
		ReadTextIds.Add(Ev.TextId);
		UE_LOG(LogLeoNarrative, Display, TEXT("[%s] %s"), *Ev.Speaker, *Ev.Text);
		break;
	case ELeoEventKind::ChoiceShown:
	{
		FString Joined;
		for (int32 i = 0; i < Ev.Options.Num(); ++i)
		{
			Joined += FString::Printf(TEXT("\n  %d. %s -> %s"), i, *Ev.Options[i].Text, *Ev.Options[i].TargetLabel);
		}
		UE_LOG(LogLeoNarrative, Display, TEXT("[选项]%s"), *Joined);
		break;
	}
	case ELeoEventKind::ChoiceMade:
		UE_LOG(LogLeoNarrative, Display, TEXT("[选择] %d -> %s"), Ev.ChoiceIndex, *Ev.TextId);
		break;
	case ELeoEventKind::ChapterEnd:
		UE_LOG(LogLeoNarrative, Display, TEXT("── 章节结束: %s ──"), *Ev.Chapter.ToString());
		if (ActiveGraph)
		{
			bGraphAdvancePending = true; // 延迟到帧末推进：当前正处于 VM 事件广播内
		}
		break;
	default:
		break;
	}
	OnLeoEvent.Broadcast(Ev);
}

bool ULeoNarrativeSubsystem::TickVM(float DeltaSeconds)
{
	if (ActiveVM)
	{
		ActiveVM->Tick(DeltaSeconds);
	}
	if (bGraphAdvancePending)
	{
		bGraphAdvancePending = false;
		AdvanceGraph();
	}
	return true; // 持续 ticking
}

// ---- ScenarioGraph 编排 ----

void ULeoNarrativeSubsystem::StartGraph(ULeoScenarioGraph* Graph, FName StartNode)
{
	if (!Graph)
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("StartGraph: 图资产为空"));
		return;
	}
	ActiveGraph = Graph;
	EdgeExprCache.Reset();
	RunGraphNode(StartNode.IsNone() ? Graph->EntryNode : StartNode);
}

void ULeoNarrativeSubsystem::RunGraphNode(FName NodeId)
{
	if (!ActiveGraph)
	{
		return;
	}
	const FLeoScenarioNode* Node = ActiveGraph->FindNode(NodeId);
	if (!Node)
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("图节点不存在: %s —— 图终止"), *NodeId.ToString());
		ActiveGraph = nullptr;
		return;
	}
	CurrentGraphNodeId = NodeId;
	UE_LOG(LogLeoNarrative, Display, TEXT("── 图节点: %s → %s@%s ──"), *NodeId.ToString(),
		*Node->Chapter.ToString(), *Node->Label.ToString());
	StartChapterAt(Node->Chapter, Node->Label, 0);
}

void ULeoNarrativeSubsystem::AdvanceGraph()
{
	if (!ActiveGraph)
	{
		return;
	}
	const FLeoScenarioNode* Node = ActiveGraph->FindNode(CurrentGraphNodeId);
	if (!Node)
	{
		ActiveGraph = nullptr;
		return;
	}
	// 出边按优先级降序（稳定）
	TArray<const FLeoScenarioEdge*> Edges;
	for (const FLeoScenarioEdge& E : Node->Edges) { Edges.Add(&E); }
	Edges.Sort([](const FLeoScenarioEdge& A, const FLeoScenarioEdge& B) { return A.Priority > B.Priority; });

	for (const FLeoScenarioEdge* E : Edges)
	{
		if (!EvalEdgeCondition(E->Condition)) { continue; }
		if (!ActiveGraph->FindNode(E->To))
		{
			UE_LOG(LogLeoNarrative, Warning, TEXT("边指向不存在的节点: %s，跳过"), *E->To.ToString());
			continue;
		}
		RunGraphNode(E->To);
		return;
	}
	UE_LOG(LogLeoNarrative, Display, TEXT("── 图完结（节点 %s 无满足条件的出边）──"), *CurrentGraphNodeId.ToString());
	ActiveGraph = nullptr;
}

bool ULeoNarrativeSubsystem::EvalEdgeCondition(const FString& Condition)
{
	if (Condition.TrimStartAndEnd().IsEmpty()) { return true; } // 空条件恒真

	leo::FLeoExprPtr Expr = EdgeExprCache.FindRef(Condition);
	if (!Expr)
	{
		leo::FLeoDiag D;
		Expr = LeoBridge::CompileExpr(Condition, D);
		if (!Expr)
		{
			UE_LOG(LogLeoNarrative, Error, TEXT("边条件编译失败: %s (%s) —— 视为假"),
				*Condition, LeoBridge::DiagName(D.Code));
			return false;
		}
		EdgeExprCache.Add(Condition, Expr);
	}
	// 变量解析：局部（若在）→ 全局
	const leo::FLeoVarResolver Resolver = [this](const std::string& Name, leo::FLeoValue& OutV) -> bool
	{
		const FName Key(Name.c_str());
		if (ActiveVM && ActiveVM->GetLocalBlackboard() && ActiveVM->GetLocalBlackboard()->GetValue(Key, OutV)) { return true; }
		if (GlobalBB && GlobalBB->GetValue(Key, OutV)) { return true; }
		return false;
	};
	leo::FLeoValue V;
	leo::ELeoDiag Code;
	std::string Msg;
	if (!leo::LeoEval(*Expr, Resolver, V, Code, Msg))
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("边条件求值失败: %s —— 视为假"), *Condition);
		return false;
	}
	if (V.Kind != leo::FLeoValue::EKind::Bool)
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("边条件结果非 Bool: %s —— 视为假"), *Condition);
		return false;
	}
	return V.B;
}

// ---- 双档体系 ----

namespace
{
	const TCHAR* GGlobalSlot = TEXT("LeoNarrative/Global");
	const TCHAR* GProgressSlot = TEXT("LeoNarrative/Progress");
}

bool ULeoNarrativeSubsystem::SaveGlobal()
{
	ULeoGlobalSaveGame* G = Cast<ULeoGlobalSaveGame>(UGameplayStatics::CreateSaveGameObject(ULeoGlobalSaveGame::StaticClass()));
	G->ReadTextIds = ReadTextIds;
	if (GlobalBB)
	{
		TMap<FName, leo::FLeoValue> Vars;
		GlobalBB->DumpToMap(Vars);
		for (const TPair<FName, leo::FLeoValue>& KV : Vars)
		{
			G->VarKeys.Add(KV.Key);
			G->VarValues.Add(FLeoSavedValue::From(KV.Value));
		}
	}
	const bool bOk = UGameplayStatics::SaveGameToSlot(G, GGlobalSlot, 0);
	UE_LOG(LogLeoNarrative, Display, TEXT("全局档保存%s（已读 %d 条 / 全局变量 %d 个）"),
		bOk ? TEXT("成功") : TEXT("失败"), G->ReadTextIds.Num(), G->VarKeys.Num());
	return bOk;
}

bool ULeoNarrativeSubsystem::LoadGlobal()
{
	ULeoGlobalSaveGame* G = Cast<ULeoGlobalSaveGame>(UGameplayStatics::LoadGameFromSlot(GGlobalSlot, 0));
	if (!G)
	{
		UE_LOG(LogLeoNarrative, Warning, TEXT("全局档不存在或读取失败"));
		return false;
	}
	ReadTextIds = G->ReadTextIds;
	if (GlobalBB)
	{
		TMap<FName, leo::FLeoValue> Vars;
		const int32 N = FMath::Min(G->VarKeys.Num(), G->VarValues.Num());
		for (int32 i = 0; i < N; ++i)
		{
			Vars.Add(G->VarKeys[i], FLeoSavedValue::To(G->VarValues[i]));
		}
		GlobalBB->RestoreFromMap(Vars);
	}
	UE_LOG(LogLeoNarrative, Display, TEXT("全局档读取成功（已读 %d 条 / 全局变量 %d 个）"), ReadTextIds.Num(), G->VarKeys.Num());
	return true;
}

bool ULeoNarrativeSubsystem::SaveProgress()
{
	if (!ActiveVM)
	{
		UE_LOG(LogLeoNarrative, Warning, TEXT("没有活跃会话，进度档未保存"));
		return false;
	}
	ULeoProgressSaveGame* P = Cast<ULeoProgressSaveGame>(UGameplayStatics::CreateSaveGameObject(ULeoProgressSaveGame::StaticClass()));
	P->GraphAsset = ActiveGraph ? FSoftObjectPath(ActiveGraph) : FSoftObjectPath();
	P->GraphNodeId = CurrentGraphNodeId;
	P->Chapter = ActiveVM->GetChapter();
	if (!ActiveVM->GetAnchor(P->AnchorLabel, P->AnchorOffset))
	{
		P->AnchorLabel = NAME_None;
		P->AnchorOffset = 0;
	}
	if (UNarrativeBlackboard* Local = ActiveVM->GetLocalBlackboard())
	{
		TMap<FName, leo::FLeoValue> Vars;
		Local->DumpToMap(Vars);
		for (const TPair<FName, leo::FLeoValue>& KV : Vars)
		{
			P->LocalKeys.Add(KV.Key);
			P->LocalValues.Add(FLeoSavedValue::From(KV.Value));
		}
	}
	P->TimestampTicks = FDateTime::UtcNow().GetTicks();
	const bool bOk = UGameplayStatics::SaveGameToSlot(P, GProgressSlot, 0);
	UE_LOG(LogLeoNarrative, Display, TEXT("进度档保存%s（%s@%s+%d）"), bOk ? TEXT("成功") : TEXT("失败"),
		*P->Chapter.ToString(), *P->AnchorLabel.ToString(), P->AnchorOffset);
	return bOk;
}

bool ULeoNarrativeSubsystem::LoadProgressAndResume()
{
	ULeoProgressSaveGame* P = Cast<ULeoProgressSaveGame>(UGameplayStatics::LoadGameFromSlot(GProgressSlot, 0));
	if (!P)
	{
		UE_LOG(LogLeoNarrative, Warning, TEXT("进度档不存在或读取失败"));
		return false;
	}
	LoadGlobal(); // 全局黑板（好感度等）先就位，边条件才有依据

	if (P->GraphAsset.IsValid())
	{
		ULeoScenarioGraph* Graph = Cast<ULeoScenarioGraph>(P->GraphAsset.TryLoad());
		if (Graph)
		{
			ActiveGraph = Graph;
			CurrentGraphNodeId = P->GraphNodeId;
			EdgeExprCache.Reset();
		}
	}
	else
	{
		ActiveGraph = nullptr;
	}

	if (!StartChapterAt(P->Chapter, P->AnchorLabel, P->AnchorOffset))
	{
		return false;
	}
	// 恢复局部黑板快照
	if (ULeoVM* VM = GetActiveVM())
	{
		if (UNarrativeBlackboard* Local = VM->GetLocalBlackboard())
		{
			TMap<FName, leo::FLeoValue> Vars;
			const int32 N = FMath::Min(P->LocalKeys.Num(), P->LocalValues.Num());
			for (int32 i = 0; i < N; ++i)
			{
				Vars.Add(P->LocalKeys[i], FLeoSavedValue::To(P->LocalValues[i]));
			}
			Local->RestoreFromMap(Vars);
		}
	}
	UE_LOG(LogLeoNarrative, Display, TEXT("进度档恢复完成，续跑 %s@%s+%d"), *P->Chapter.ToString(), *P->AnchorLabel.ToString(), P->AnchorOffset);
	return true;
}

void ULeoNarrativeSubsystem::SetManifest(ULeoAssetManifest* InManifest)
{
	Manifest = InManifest;
	if (Stage) { Stage->SetManifest(InManifest); }
	if (Audio) { Audio->SetManifest(InManifest); }
}

void ULeoNarrativeSubsystem::ShowDialogueUI(bool bShow)
{
	if (bShow)
	{
		if (!DialogueWidget)
		{
			APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
			if (!PC)
			{
				UE_LOG(LogLeoNarrative, Warning, TEXT("没有本地 PlayerController，无法创建对话 UI"));
				return;
			}
			DialogueWidget = CreateWidget<ULeoDialogueWidget>(PC, ULeoDialogueWidget::StaticClass());
		}
		if (DialogueWidget)
		{
			DialogueWidget->AddToViewport(10);
		}
	}
	else if (DialogueWidget)
	{
		DialogueWidget->RemoveFromParent();
		DialogueWidget = nullptr;
	}
}
