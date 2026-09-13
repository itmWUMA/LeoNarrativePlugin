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
	TEXT("leo.graph"), TEXT("编排图: leo.graph run <图资产软路径>（运行指定图资产）| leo.graph demo（内存演示图，自动播完）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() >= 2 && Args[0] == TEXT("run"))
		{
			ULeoNarrativeSubsystem* S = GetLeoSubsystem();
			if (!S) { return; }
			if (ULeoScenarioGraph* Graph = LoadObject<ULeoScenarioGraph>(nullptr, *Args[1]))
			{
				UE_LOG(LogTemp, Display, TEXT("[leo.graph] 启动图资产: %s"), *Args[1]);
				S->StartGraph(Graph);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[leo.graph] 图资产加载失败: %s"), *Args[1]);
			}
			return;
		}
		if (Args.Num() < 1 || Args[0] != TEXT("demo"))
		{
			UE_LOG(LogTemp, Warning, TEXT("用法: leo.graph run <图资产软路径> | leo.graph demo"));
			return;
		}
		ULeoNarrativeSubsystem* S = GetLeoSubsystem();
		if (!S) { return; }
		// 演示章节（内存注入，不依赖 Content/Scripts 的用户内容）
		const TCHAR* DemoChapter = TEXT(R"LEO(
label start
setg affection = 0
text - | 图演示：做出选择。
choice
    微笑 -> yes
    沉默 -> no

label yes
setg affection += 1
text - | 你选择了微笑。
end

label no
text - | 你保持了沉默。
end
)LEO");
		S->GetRegistry()->CompileMemory(TEXT("demo_graph_ch"), DemoChapter);
		// 内存构建类型化演示图：
		// n_intro(Chapter 整章) → n_route(Branch 好感度分流，边副作用写 route)
		//   ├─ affection>=1 → n_recall(Subgraph 子图) → e_true(Ending true_end)
		//   └─ 其他        → e_normal(Ending normal_end)
		// 子图 G2: n_r1(Chapter demo_graph_ch@no) → e_inner(Ending，仅收束子图)
		ULeoScenarioGraph* G2 = NewObject<ULeoScenarioGraph>(S);
		G2->EntryNode = TEXT("n_r1");
		{
			FLeoScenarioNode R1;
			R1.Id = TEXT("n_r1"); R1.Type = ELeoScenarioNodeType::Chapter;
			R1.Chapter = TEXT("demo_graph_ch"); R1.Label = TEXT("no"); R1.Caption = INVTEXT("回忆片段");
			FLeoScenarioEdge RE; RE.To = TEXT("e_inner");
			R1.Edges = { RE };
			FLeoScenarioNode R2;
			R2.Id = TEXT("e_inner"); R2.Type = ELeoScenarioNodeType::Ending; R2.EndingId = TEXT("inner");
			G2->Nodes = { R1, R2 };
		}

		ULeoScenarioGraph* G = NewObject<ULeoScenarioGraph>(S);
		G->EntryNode = TEXT("n_intro");
		{
			FLeoScenarioNode N1;
			N1.Id = TEXT("n_intro"); N1.Type = ELeoScenarioNodeType::Chapter;
			N1.Chapter = TEXT("demo_graph_ch"); N1.Caption = INVTEXT("序章：整章（含选项）");
			FLeoScenarioEdge E1; E1.To = TEXT("n_route"); E1.Priority = 0;
			N1.Edges = { E1 };

			FLeoScenarioNode N2;
			N2.Id = TEXT("n_route"); N2.Type = ELeoScenarioNodeType::Branch; N2.Caption = INVTEXT("好感度分流");
			FLeoScenarioEdge B1;
			B1.To = TEXT("n_recall"); B1.Condition = TEXT("affection >= 1"); B1.Priority = 10;
			FLeoEdgeAction A1; A1.Key = TEXT("route"); A1.bGlobal = true; A1.Operation = ELeoEdgeOp::Assign; A1.Expr = TEXT("\"recall\"");
			B1.Actions = { A1 };
			FLeoScenarioEdge B2;
			B2.To = TEXT("e_normal"); B2.Priority = 0;
			FLeoEdgeAction A2; A2.Key = TEXT("route"); A2.bGlobal = true; A2.Operation = ELeoEdgeOp::Assign; A2.Expr = TEXT("\"normal\"");
			B2.Actions = { A2 };
			N2.Edges = { B1, B2 };

			FLeoScenarioNode N3;
			N3.Id = TEXT("n_recall"); N3.Type = ELeoScenarioNodeType::Subgraph;
			N3.SubGraph = G2; N3.Caption = INVTEXT("回忆篇（子图）");
			FLeoScenarioEdge E3; E3.To = TEXT("e_true"); E3.Priority = 0;
			N3.Edges = { E3 };

			FLeoScenarioNode N4;
			N4.Id = TEXT("e_true"); N4.Type = ELeoScenarioNodeType::Ending; N4.EndingId = TEXT("true_end");
			FLeoScenarioNode N5;
			N5.Id = TEXT("e_normal"); N5.Type = ELeoScenarioNodeType::Ending; N5.EndingId = TEXT("normal_end");

			G->Nodes = { N1, N2, N3, N4, N5 };
		}
		// 完结报告（去重注册，重复运行 demo 不累积）
		static FDelegateHandle DemoGraphFinishHandle;
		S->OnGraphFinished.Remove(DemoGraphFinishHandle);
		DemoGraphFinishHandle = S->OnGraphFinished.AddLambda([](FName EndingId)
		{
			UE_LOG(LogTemp, Display, TEXT("[demo-graph] 图完结，结局 = %s"),
				EndingId.IsNone() ? TEXT("(无)") : *EndingId.ToString());
		});
		S->StartGraph(G);

		// 自动播完（含外部断点处理），便于无头验证
		RunWhenSubsystemReady([S](ULeoNarrativeSubsystem*)
		{
			static FTSTicker::FDelegateHandle GraphDemoHandle;
			FTSTicker::GetCoreTicker().RemoveTicker(GraphDemoHandle);
			GraphDemoHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float Dt)
			{
				ULeoVM* VM = S->GetActiveVM();
				if (!VM || !S->IsGraphActive()) { return false; }
				switch (VM->GetState())
				{
				case ELeoVMState::WaitClick: S->Advance(); break;
				case ELeoVMState::WaitChoice: S->Choose(0); break; // 选 0 → branch_yes → affection=1
				case ELeoVMState::WaitExternal:
					S->ResumeWith(VM->GetSuspendToken(), leo::FLeoValue::MakeInt(0));
					break;
				case ELeoVMState::Finished: break; // 章末由子系统接管推进图
				default: break;
				}
				return true;
			}));
		});
	}));

// ---- 本地化（CSV 译文表）----

// leo.lang：无参 = 显示当前语言/译文表/可用语言；带参 = 切语言并重载译文表
static FAutoConsoleCommand GLeoLang(
	TEXT("leo.lang"), TEXT("叙事语言: leo.lang | leo.lang <culture>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		RunWhenSubsystemReady([Args](ULeoNarrativeSubsystem* S)
		{
			if (Args.Num() == 0)
			{
				TArray<FString> Cultures;
				FLeoL10nTable::ListAvailableCultures(Cultures);
				UE_LOG(LogTemp, Display, TEXT("[语言] 当前=%s 译文=%d 条（目录 %s）可用: %s"),
					*S->GetCurrentLanguage(), S->GetL10n().NumEntries(),
					S->GetL10n().GetLoadedCultureDir().IsEmpty() ? TEXT("(无)") : *S->GetL10n().GetLoadedCultureDir(),
					*FString::Join(Cultures, TEXT(", ")));
				return;
			}
			S->SetLanguage(Args[0]);
		});
	}));

// leo.demolang：内存注入演示章节 + 假想英文译文，验证「命中替换 / 缺译回落」两条路径
static FAutoConsoleCommand GLeoDemoLang(
	TEXT("leo.demolang"), TEXT("本地化演示: 注入章节+英文译文自动播完并断言"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		RunWhenSubsystemReady([](ULeoNarrativeSubsystem* S)
		{
			S->GetRegistry()->CompileMemory(TEXT("demo_l10n_ch"), TEXT(R"LEO(
text - | 自动 ID 的句子，等译文。
text 李雷 | 显式 ID 的句子，等译文。 id=demo/l10n/hello
choice
    选项甲 -> lab_a id=demo/l10n/opt
    选项乙 -> lab_b
label lab_a
text - | 分支甲，未提供译文。
end
label lab_b
text - | 分支乙。
end
)LEO"));
			// 注入译文：自动 ID / 显式 ID / 选项各一条；分支甲故意不译（回落原文）
			FLeoL10nTable& T = S->GetL10n();
			T.AddEntry(TEXT("demo_l10n_ch/_root/0"), TEXT("Auto-ID line, localized."));
			T.AddEntry(TEXT("demo/l10n/hello"), TEXT("Explicit-ID line, localized."));
			T.AddEntry(TEXT("demo/l10n/opt"), TEXT("Option A (EN)"));

			static FTSTicker::FDelegateHandle DemoLangHandle;
			FTSTicker::GetCoreTicker().RemoveTicker(DemoLangHandle);
			DemoLangHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float)
			{
				ULeoVM* VM = S->GetActiveVM();
				if (!VM) { return false; }
				switch (VM->GetState())
				{
				case ELeoVMState::WaitClick: S->Advance(); break;
				case ELeoVMState::WaitChoice: S->Choose(0); break; // 选 0 → lab_a（未译分支）
				case ELeoVMState::Finished: break;
				default: break;
				}
				if (VM->GetState() != ELeoVMState::Finished) { return true; }

				// 完结断言：事件流里必须有三条英文与一条中文回落
				int32 Ok = 0;
				const FString Joined = FString::Join(S->GetEventLog(), TEXT("\n"));
				Ok += Joined.Contains(TEXT("Auto-ID line, localized.")) ? 1 : 0;
				Ok += Joined.Contains(TEXT("Explicit-ID line, localized.")) ? 1 : 0;
				Ok += Joined.Contains(TEXT("Option A (EN)")) ? 1 : 0;
				Ok += Joined.Contains(TEXT("分支甲，未提供译文。")) ? 1 : 0;
				if (Ok == 4)
				{
					UE_LOG(LogTemp, Display, TEXT("[demolang] PASS（命中替换×3 + 缺译回落×1）"));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[demolang] FAIL（断言 %d/4，检查事件流）"), Ok);
				}
				return false;
			}));
			if (!S->StartChapter(TEXT("demo_l10n_ch")))
			{
				UE_LOG(LogTemp, Error, TEXT("[demolang] 章节启动失败"));
			}
		});
	}));
