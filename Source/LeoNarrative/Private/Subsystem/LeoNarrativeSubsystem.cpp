#include "Subsystem/LeoNarrativeSubsystem.h"
#include "VM/LeoVM.h"
#include "Audio/LeoAudioAdapter.h"
#include "Data/LeoAssetManifest.h"
#include "Data/LeoScenarioGraph.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Presentation/LeoDialogueWidget.h"
#include "Save/LeoSaveGame.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "Stage/LeoSequencerPerformer.h"
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
	Sequencer = NewObject<ULeoSequencerPerformer>(this);
	Sequencer->SetOwner(this);
	Sequencer->SetWorldContext(GetGameInstance());
	OnLeoEvent.AddUObject(Sequencer, &ULeoSequencerPerformer::HandleEvent);

	// GameInstanceSubsystem 没有 Tick，用核心 Ticker 驱动 VM（游戏线程）
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULeoNarrativeSubsystem::TickVM));
	// 当前语言的剧本译文表（无对应目录 = 表空，全部回落原文）
	L10n.LoadCulture(GetCurrentLanguage());
	UE_LOG(LogLeoNarrative, Log, TEXT("LeoNarrative 子系统初始化完成"));
}

bool ULeoNarrativeSubsystem::SetLanguage(const FString& Culture)
{
	FInternationalization& I18N = FInternationalization::Get();
	if (!I18N.SetCurrentCulture(*Culture))
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("SetLanguage: 未知文化 %s"), *Culture);
		return false;
	}
	const bool bLoaded = L10n.LoadCulture(Culture);
	UE_LOG(LogLeoNarrative, Display, TEXT("[语言] %s（%d 条译文）"), *Culture, L10n.NumEntries());
	return bLoaded;
}

FString ULeoNarrativeSubsystem::GetCurrentLanguage() const
{
	return FInternationalization::Get().GetCurrentCulture()->GetName();
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

	// 对话 UI 先于 VM 启动挂载(widget 在 Construct 时订阅事件流,晚挂会丢开场首批演出)。
	// 无头环境(LeoRun/nullrhi)没有 PlayerController,静默跳过。
	ShowDialogueUI(true);

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
	// 本地化替换：只改显示文本，TextId/锚点/已读记录不动；查不到译文回落原文
	FLeoEvent Display = Ev;
	if (Display.Kind == ELeoEventKind::Text)
	{
		FString T;
		if (L10n.Resolve(Display.TextId, T)) { Display.Text = MoveTemp(T); }
	}
	else if (Display.Kind == ELeoEventKind::ChoiceShown)
	{
		for (FLeoEventOption& O : Display.Options)
		{
			FString T;
			if (L10n.Resolve(O.TextId, T)) { O.Text = MoveTemp(T); }
		}
	}
	// 调试器事件流（环形缓冲，记录玩家实际看到的文本，先于转发）
	AppendEventLog(Display);
	// 关键事件留 Display 级日志：无 UI 场景（命令行/自动化）也能看到叙事流
	switch (Display.Kind)
	{
	case ELeoEventKind::ChapterStart:
		UE_LOG(LogLeoNarrative, Display, TEXT("── 章节开始: %s ──"), *Display.Chapter.ToString());
		break;
	case ELeoEventKind::Text:
		ReadTextIds.Add(Ev.TextId);
		UE_LOG(LogLeoNarrative, Display, TEXT("[%s] %s"), *Display.Speaker, *Display.Text);
		break;
	case ELeoEventKind::ChoiceShown:
	{
		FString Joined;
		for (int32 i = 0; i < Display.Options.Num(); ++i)
		{
			Joined += FString::Printf(TEXT("\n  %d. %s -> %s"), i, *Display.Options[i].Text, *Display.Options[i].TargetLabel);
		}
		UE_LOG(LogLeoNarrative, Display, TEXT("[选项]%s"), *Joined);
		break;
	}
	case ELeoEventKind::ChoiceMade:
		UE_LOG(LogLeoNarrative, Display, TEXT("[选择] %d -> %s"), Display.ChoiceIndex, *Display.TextId);
		break;
	case ELeoEventKind::ChapterEnd:
		UE_LOG(LogLeoNarrative, Display, TEXT("── 章节结束: %s ──"), *Display.Chapter.ToString());
		if (IsGraphActive())
		{
			bGraphAdvancePending = true; // 延迟到帧末推进：当前正处于 VM 事件广播内
		}
		break;
	default:
		break;
	}
	OnLeoEvent.Broadcast(Display);
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

// ---- ScenarioGraph 编排（节点类型化：Chapter/Branch/Ending/Subgraph）----

void ULeoNarrativeSubsystem::StartGraph(ULeoScenarioGraph* Graph, FName StartNode)
{
	if (!Graph)
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("StartGraph: 图资产为空"));
		return;
	}
	GraphStack.Reset();
	FLeoGraphFrame& Frame = GraphStack.AddDefaulted_GetRef();
	Frame.Graph = Graph;
	EdgeExprCache.Reset();
	RunGraphNode(StartNode.IsNone() ? Graph->EntryNode : StartNode);
}

void ULeoNarrativeSubsystem::RunGraphNode(FName NodeId)
{
	if (GraphStack.IsEmpty()) { return; }
	const FLeoGraphFrame& Top = GraphStack.Last();
	const FLeoScenarioNode* Node = Top.Graph ? Top.Graph->FindNode(NodeId) : nullptr;
	if (!Node)
	{
		UE_LOG(LogLeoNarrative, Error, TEXT("图节点不存在: %s —— 图终止"), *NodeId.ToString());
		GraphStack.Reset();
		OnGraphFinished.Broadcast(NAME_None);
		return;
	}
	GraphStack.Last().NodeId = NodeId;

	switch (Node->Type)
	{
	case ELeoScenarioNodeType::Chapter:
		UE_LOG(LogLeoNarrative, Display, TEXT("── 图节点: %s → %s@%s ──"), *NodeId.ToString(),
			*Node->Chapter.ToString(), *Node->Label.ToString());
		StartChapterAt(Node->Chapter, Node->Label, 0);
		break;

	case ELeoScenarioNodeType::Branch:
		// 不跑章节：立即按出边条件分流（jumpif 链的图形态）
		AdvanceFromCurrentNode();
		break;

	case ELeoScenarioNodeType::Subgraph:
	{
		if (!Node->SubGraph)
		{
			UE_LOG(LogLeoNarrative, Error, TEXT("Subgraph 节点 %s 未配置子图 —— 图终止"), *NodeId.ToString());
			GraphStack.Reset();
			OnGraphFinished.Broadcast(NAME_None);
			return;
		}
		if (GraphStack.Num() >= MaxGraphDepth)
		{
			UE_LOG(LogLeoNarrative, Error, TEXT("子图嵌套超过 %d 层（疑似自引用循环）—— 图终止"), MaxGraphDepth);
			GraphStack.Reset();
			OnGraphFinished.Broadcast(NAME_None);
			return;
		}
		UE_LOG(LogLeoNarrative, Display, TEXT("── 子图进入: %s ──"), *Node->SubGraph->GetName());
		FLeoGraphFrame& Sub = GraphStack.AddDefaulted_GetRef();
		Sub.Graph = Node->SubGraph;
		RunGraphNode(Node->SubGraph->EntryNode);
		break;
	}

	case ELeoScenarioNodeType::Ending:
	{
		const FName EndingId = Node->EndingId;
		if (GraphStack.Num() > 1)
		{
			// 子图内的 Ending = 子图正常收束：弹栈，从父层 Subgraph 节点的出边继续
			UE_LOG(LogLeoNarrative, Display, TEXT("── 子图收束（%s）──"),
				EndingId.IsNone() ? TEXT("") : *EndingId.ToString());
			GraphStack.Pop();
			AdvanceFromCurrentNode();
		}
		else
		{
			UE_LOG(LogLeoNarrative, Display, TEXT("── 结局: %s ──"),
				EndingId.IsNone() ? TEXT("(未命名)") : *EndingId.ToString());
			GraphStack.Reset();
			OnGraphFinished.Broadcast(EndingId);
		}
		break;
	}
	}
}

void ULeoNarrativeSubsystem::AdvanceGraph()
{
	AdvanceFromCurrentNode();
}

void ULeoNarrativeSubsystem::AdvanceFromCurrentNode()
{
	while (!GraphStack.IsEmpty())
	{
		const FLeoGraphFrame& Top = GraphStack.Last();
		const FLeoScenarioNode* Node = Top.Graph ? Top.Graph->FindNode(Top.NodeId) : nullptr;
		if (!Node)
		{
			UE_LOG(LogLeoNarrative, Error, TEXT("图节点丢失: %s —— 图终止"), *Top.NodeId.ToString());
			GraphStack.Reset();
			OnGraphFinished.Broadcast(NAME_None);
			return;
		}
		UNarrativeBlackboard* Local = ActiveVM ? ActiveVM->GetLocalBlackboard() : nullptr;
		const FName Next = LeoGraphEval::SelectEdge(*Node, Local, GlobalBB, EdgeExprCache);
		if (!Next.IsNone())
		{
			RunGraphNode(Next);
			return;
		}
		// 无可用出边：子层弹栈继续父层（子图收束）；外层 = 图完结
		if (GraphStack.Num() > 1)
		{
			GraphStack.Pop();
			continue;
		}
		UE_LOG(LogLeoNarrative, Display, TEXT("── 图完结（节点 %s 无满足条件的出边）──"), *Top.NodeId.ToString());
		GraphStack.Reset();
		OnGraphFinished.Broadcast(NAME_None);
		return;
	}
}

// ---- 双档体系 ----

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
	const bool bOk = UGameplayStatics::SaveGameToSlot(G, GlobalSlotName(), 0);
	UE_LOG(LogLeoNarrative, Display, TEXT("全局档保存%s（已读 %d 条 / 全局变量 %d 个）"),
		bOk ? TEXT("成功") : TEXT("失败"), G->ReadTextIds.Num(), G->VarKeys.Num());
	return bOk;
}

bool ULeoNarrativeSubsystem::LoadGlobal()
{
	ULeoGlobalSaveGame* G = Cast<ULeoGlobalSaveGame>(UGameplayStatics::LoadGameFromSlot(GlobalSlotName(), 0));
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
	for (const FLeoGraphFrame& Frame : GraphStack)
	{
		if (Frame.Graph)
		{
			P->GraphAssets.Add(FSoftObjectPath(Frame.Graph));
			P->GraphNodeIds.Add(Frame.NodeId);
		}
	}
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
	const bool bOk = UGameplayStatics::SaveGameToSlot(P, ProgressSlotName(), 0);
	UE_LOG(LogLeoNarrative, Display, TEXT("进度档保存%s（%s@%s+%d）"), bOk ? TEXT("成功") : TEXT("失败"),
		*P->Chapter.ToString(), *P->AnchorLabel.ToString(), P->AnchorOffset);
	return bOk;
}

bool ULeoNarrativeSubsystem::LoadProgressAndResume()
{
	ULeoProgressSaveGame* P = Cast<ULeoProgressSaveGame>(UGameplayStatics::LoadGameFromSlot(ProgressSlotName(), 0));
	if (!P)
	{
		UE_LOG(LogLeoNarrative, Warning, TEXT("进度档不存在或读取失败"));
		return false;
	}
	LoadGlobal(); // 全局黑板（好感度等）先就位，边条件才有依据

	GraphStack.Reset();
	{
		const int32 N = FMath::Min(P->GraphAssets.Num(), P->GraphNodeIds.Num());
		for (int32 i = 0; i < N; ++i)
		{
			if (ULeoScenarioGraph* G = Cast<ULeoScenarioGraph>(P->GraphAssets[i].TryLoad()))
			{
				FLeoGraphFrame& Frame = GraphStack.AddDefaulted_GetRef();
				Frame.Graph = G;
				Frame.NodeId = P->GraphNodeIds[i];
			}
		}
		// 旧档兼容：单图字段
		if (GraphStack.Num() == 0 && P->GraphAsset.IsValid())
		{
			if (ULeoScenarioGraph* G = Cast<ULeoScenarioGraph>(P->GraphAsset.TryLoad()))
			{
				FLeoGraphFrame& Frame = GraphStack.AddDefaulted_GetRef();
				Frame.Graph = G;
				Frame.NodeId = P->GraphNodeId;
			}
		}
		EdgeExprCache.Reset();
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
	if (Sequencer) { Sequencer->SetManifest(InManifest); }
}

void ULeoNarrativeSubsystem::SkipSequences()
{
	if (Sequencer) { Sequencer->StopAll(/*bResumeVM=*/true); }
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

// ---- 调试支持 ----

void ULeoNarrativeSubsystem::AppendEventLog(const FLeoEvent& Ev)
{
	FString S;
	switch (Ev.Kind)
	{
	case ELeoEventKind::ChapterStart: S = FString::Printf(TEXT("── 章节开始 %s"), *Ev.Chapter.ToString()); break;
	case ELeoEventKind::Text:
		S = Ev.Speaker.IsEmpty()
			? FString::Printf(TEXT("[文本] %s"), *Ev.Text)
			: FString::Printf(TEXT("[文本] %s：%s"), *Ev.Speaker, *Ev.Text);
		break;
	case ELeoEventKind::Bg:    S = FString::Printf(TEXT("[背景] %s"), *Ev.AssetId.ToString()); break;
	case ELeoEventKind::Char:  S = FString::Printf(TEXT("[立绘] %s %s"), *Ev.Slot, *Ev.AssetId.ToString()); break;
	case ELeoEventKind::Bgm:   S = FString::Printf(TEXT("[BGM] %s"), *Ev.AssetId.ToString()); break;
	case ELeoEventKind::Se:    S = FString::Printf(TEXT("[SE] %s"), *Ev.AssetId.ToString()); break;
	case ELeoEventKind::Voice: S = FString::Printf(TEXT("[语音] %s"), *Ev.AssetId.ToString()); break;
	case ELeoEventKind::ChoiceShown:
	{
		FString Texts;
		for (int32 i = 0; i < Ev.Options.Num(); ++i)
		{
			if (i > 0) { Texts += TEXT(" / "); }
			Texts += Ev.Options[i].Text;
		}
		S = FString::Printf(TEXT("[选项] %d 项: %s"), Ev.Options.Num(), *Texts);
		break;
	}
	case ELeoEventKind::ChoiceMade: S = FString::Printf(TEXT("[选择] %d"), Ev.ChoiceIndex); break;
	case ELeoEventKind::ChapterEnd: S = FString::Printf(TEXT("── 章节结束 %s"), *Ev.Chapter.ToString()); break;
	case ELeoEventKind::RuntimeError:
		S = FString::Printf(TEXT("[运行时错误 L%d] %s %s"), Ev.Line, *Ev.DiagCode, *Ev.DiagMsg);
		break;
	case ELeoEventKind::Custom:
		S = FString::Printf(TEXT("[%s]"), *Ev.CustomName.ToString());
		for (const TPair<FName, FString>& P : Ev.ExtraParams)
		{
			S += FString::Printf(TEXT(" %s=%s"), *P.Key.ToString(), *P.Value);
		}
		break;
	default: break;
	}
	if (S.IsEmpty()) { return; }
	if (S.Len() > 200) { S = S.Left(200) + TEXT("…"); }
	if (Ev.Line > 0) { S = FString::Printf(TEXT("L%-4d "), Ev.Line) + S; }
	EventLog.Add(MoveTemp(S));
	if (EventLog.Num() > EventLogCapacity) { EventLog.RemoveAt(0, EventLog.Num() - EventLogCapacity); }
}

void ULeoNarrativeSubsystem::GetDebugSnapshot(FLeoDebugSnapshot& Out) const
{
	Out = FLeoDebugSnapshot();
	Out.bAuto = bAuto;
	Out.bSkip = bSkip;
	Out.ReadTextCount = ReadTextIds.Num();
	Out.bGraphActive = IsGraphActive();
	Out.GraphNode = GetCurrentGraphNode().ToString();
	Out.CustomCommands = RegisteredCommandNames;
	Out.EventLog = EventLog;

	auto DumpBoard = [](const UNarrativeBlackboard* Board, TArray<FLeoDebugVar>& OutVars)
	{
		if (!Board) { return; }
		TMap<FName, leo::FLeoValue> Vars;
		Board->DumpToMap(Vars);
		Vars.KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const TPair<FName, leo::FLeoValue>& KV : Vars)
		{
			FLeoDebugVar V;
			V.Key = KV.Key.ToString();
			V.Value = LeoBridge::ToFString(KV.Value.ToString());
			OutVars.Add(MoveTemp(V));
		}
	};
	DumpBoard(GlobalBB, Out.GlobalVars);

	if (const ULeoVM* VM = ActiveVM)
	{
		Out.bActive = true;
		Out.StateName = ULeoVM::StateName(VM->GetState());
		Out.Chapter = VM->GetChapter().ToString();
		Out.PC = VM->GetPC();
		Out.Line = VM->GetCurrentLine();
		Out.SuspendToken = VM->GetSuspendToken();
		Out.WaitRemaining = VM->GetWaitRemaining();
		Out.CommandDesc = VM->DescribeCurrentCommand();
		FName Label; int32 Offset = 0;
		if (VM->GetAnchor(Label, Offset))
		{
			Out.Anchor = FString::Printf(TEXT("%s+%d"), *Label.ToString(), Offset);
		}
		DumpBoard(VM->GetLocalBlackboard(), Out.LocalVars);
	}
}
