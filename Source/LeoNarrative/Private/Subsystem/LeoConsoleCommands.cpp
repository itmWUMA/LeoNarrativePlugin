// 运行时控制台命令：无 UI 也能驱动与观测叙事会话（PIE/游戏内 ~ 控制台）
#include "Subsystem/LeoNarrativeSubsystem.h"
#include "Data/LeoScenarioGraph.h"
#include "VM/LeoVM.h"

static ULeoNarrativeSubsystem* GetLeoSubsystem()
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

// 等子系统就绪后再执行（-ExecCmds 在 frame 0 触发，GameInstance 可能尚未创建）
static void RunWhenSubsystemReady(TFunction<void(ULeoNarrativeSubsystem*)> Fn)
{
	if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { Fn(S); return; }
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Fn = MoveTemp(Fn)](float)
		{
			if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { Fn(S); return false; }
			return true; // 未就绪，继续等
		}));
}

static FAutoConsoleCommand GLeoStart(
	TEXT("leo.start"), TEXT("开始章节: leo.start <chapter>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("用法: leo.start <chapter>")); return; }
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->StartChapter(*Args[0]); }
	}));

static FAutoConsoleCommand GLeoStop(
	TEXT("leo.stop"), TEXT("停止当前叙事会话"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->Stop(); }
	}));

static FAutoConsoleCommand GLeoClick(
	TEXT("leo.click"), TEXT("等价点击推进（leo.advance 同义）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->Advance(); }
	}));

static FAutoConsoleCommand GLeoChoose(
	TEXT("leo.choose"), TEXT("选择选项: leo.choose <index>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("用法: leo.choose <index>")); return; }
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->Choose(FCString::Atoi(*Args[0])); }
	}));

static FAutoConsoleCommand GLeoAuto(
	TEXT("leo.auto"), TEXT("切换自动播放"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->SetAuto(!S->IsAuto()); UE_LOG(LogTemp, Display, TEXT("auto = %s"), S->IsAuto() ? TEXT("on") : TEXT("off")); }
	}));

static FAutoConsoleCommand GLeoSkip(
	TEXT("leo.skip"), TEXT("切换跳过模式"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->SetSkip(!S->IsSkip()); UE_LOG(LogTemp, Display, TEXT("skip = %s"), S->IsSkip() ? TEXT("on") : TEXT("off")); }
	}));

static FAutoConsoleCommand GLeoState(
	TEXT("leo.state"), TEXT("打印当前 VM 状态"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		ULeoNarrativeSubsystem* S = GetLeoSubsystem();
		if (!S || !S->GetActiveVM()) { UE_LOG(LogTemp, Display, TEXT("无活跃会话")); return; }
		ULeoVM* VM = S->GetActiveVM();
		FName Label; int32 Offset = 0;
		VM->GetAnchor(Label, Offset);
		const TCHAR* StateName[] = { TEXT("Idle"), TEXT("Running"), TEXT("WaitClick"), TEXT("WaitTimer"), TEXT("WaitChoice"), TEXT("WaitExternal"), TEXT("Finished") };
		const FString Extra = VM->GetState() == ELeoVMState::WaitExternal
			? FString::Printf(TEXT(" token=%s"), *VM->GetSuspendToken().ToString())
			: FString();
		UE_LOG(LogTemp, Display, TEXT("chapter=%s pc=%d anchor=%s+%d state=%s%s"),
			*VM->GetChapter().ToString(), VM->GetPC(), *Label.ToString(), Offset, StateName[VM->GetState()], *Extra);
	}));

static FAutoConsoleCommand GLeoReload(
	TEXT("leo.reload"), TEXT("重扫描编译 Content/Scripts"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->ReloadScripts(); }
	}));

// 自动播放整章（点击全推进、选项默认选 0）——无头回归测试用
// leo.autotest resume：不新开章节，只挂自动推进（配合 leo.load 验证读档续跑）
static FAutoConsoleCommand GLeoAutoTest(
	TEXT("leo.autotest"), TEXT("自动播完章节: leo.autotest <chapter>|resume"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("用法: leo.autotest <chapter>|resume")); return; }
		RunWhenSubsystemReady([Args](ULeoNarrativeSubsystem* S)
		{
			if (Args[0] != TEXT("resume"))
			{
				if (!S->StartChapter(*Args[0])) { return; }
			}
			UE_LOG(LogTemp, Display, TEXT("[autotest] 开始: %s"), *Args[0]);

			static FTSTicker::FDelegateHandle AutotestHandle;
			FTSTicker::GetCoreTicker().RemoveTicker(AutotestHandle);
			AutotestHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float Dt)
			{
				ULeoVM* VM = S->GetActiveVM();
				if (!VM)
				{
					UE_LOG(LogTemp, Display, TEXT("[autotest] 会话结束"));
					return false; // 停止 ticker
				}
				static float ExternalWait = 0.f;
				switch (VM->GetState())
				{
				case ELeoVMState::WaitClick:
					ExternalWait = 0.f;
					S->Advance();
					break;
				case ELeoVMState::WaitChoice:
					ExternalWait = 0.f;
					S->Choose(0);
					break;
				case ELeoVMState::WaitExternal:
					// 外部断点：0.3s 后以默认 payload 0 恢复（含玩法段的章节也能整章回归；
					// 非默认结果值的分支用 leo.demo <名称> 或游戏侧测试驱动）
					ExternalWait += Dt;
					if (ExternalWait >= 0.3f)
					{
						ExternalWait = 0.f;
						UE_LOG(LogTemp, Display, TEXT("[autotest] 外部断点 %s 以默认值 0 恢复"), *VM->GetSuspendToken().ToString());
						S->ResumeWith(VM->GetSuspendToken(), leo::FLeoValue::MakeInt(0));
					}
					break;
				case ELeoVMState::Finished:
					ExternalWait = 0.f;
					UE_LOG(LogTemp, Display, TEXT("[autotest] 完结: %s"), *VM->GetChapter().ToString());
					return false;
				default: break;
				}
				return true;
			}));
		});
	}));

static FAutoConsoleCommand GLeoUI(
	TEXT("leo.ui"), TEXT("切换纯 C++ 对话 UI"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem())
		{
			static bool bShown = false;
			bShown = !bShown;
			S->ShowDialogueUI(bShown);
		}
	}));

static FAutoConsoleCommand GLeoSave(
	TEXT("leo.save"), TEXT("保存进度档+全局档"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->SaveProgress(); S->SaveGlobal(); }
	}));

static FAutoConsoleCommand GLeoLoad(
	TEXT("leo.load"), TEXT("读取进度档并续跑"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->LoadProgressAndResume(); }
	}));

static FAutoConsoleCommand GLeoGraph(
	TEXT("leo.graph"), TEXT("运行演示编排图: leo.graph demo（复用 chapter01 的 label 组成多节点图）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1 || Args[0] != TEXT("demo"))
		{
			UE_LOG(LogTemp, Warning, TEXT("用法: leo.graph demo"));
			return;
		}
		ULeoNarrativeSubsystem* S = GetLeoSubsystem();
		if (!S) { return; }
		// 内存中构建演示图：intro → (好感度分支) → good/bad → finale
		ULeoScenarioGraph* G = NewObject<ULeoScenarioGraph>(S);
		G->EntryNode = TEXT("n_intro");

		FLeoScenarioNode N1;
		N1.Id = TEXT("n_intro"); N1.Chapter = TEXT("chapter01"); N1.Caption = INVTEXT("序章：整章（含选项）");
		FLeoScenarioEdge E1; E1.To = TEXT("n_good"); E1.Condition = TEXT("affection >= 1"); E1.Priority = 10;
		FLeoScenarioEdge E2; E2.To = TEXT("n_bad");  E2.Condition = TEXT(""); E2.Priority = 0;
		N1.Edges = { E1, E2 };

		FLeoScenarioNode N2;
		N2.Id = TEXT("n_good"); N2.Chapter = TEXT("chapter01"); N2.Label = TEXT("branch_yes"); N2.Caption = INVTEXT("好感线");
		FLeoScenarioEdge E3; E3.To = TEXT("n_finale"); E3.Priority = 0;
		N2.Edges = { E3 };

		FLeoScenarioNode N3;
		N3.Id = TEXT("n_bad"); N3.Chapter = TEXT("chapter01"); N3.Label = TEXT("branch_silent"); N3.Caption = INVTEXT("沉默线");
		FLeoScenarioEdge E4; E4.To = TEXT("n_finale"); E4.Priority = 0;
		N3.Edges = { E4 };

		FLeoScenarioNode N4;
		N4.Id = TEXT("n_finale"); N4.Chapter = TEXT("chapter01"); N4.Label = TEXT("lab_ending"); N4.Caption = INVTEXT("尾声");

		G->Nodes = { N1, N2, N3, N4 };
		S->StartGraph(G);
	}));
