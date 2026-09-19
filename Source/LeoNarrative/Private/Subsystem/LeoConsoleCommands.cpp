// 运行时控制台命令：无 UI 也能驱动与观测叙事会话（PIE/游戏内 ~ 控制台）
#include "Subsystem/LeoNarrativeSubsystem.h"
#include "Settings/LeoNarrativeSettings.h"
#include "Presentation/LeoDialogueWidget.h"
#include "Data/LeoAssetManifest.h"
#include "Data/LeoScenarioGraph.h"
#include "Stage/LeoSequencerPerformer.h"
#include "VM/LeoVM.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

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

// 框架配置转储（验证 Project Settings → Game|LeoNarrative 链路；无头可跑）
static FAutoConsoleCommand GLeoSettings(
	TEXT("leo.settings"), TEXT("打印框架配置（目录/档名/内置表现层开关/UI 类/清单/auto-skip 节奏）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		const ULeoNarrativeSettings* C = ULeoNarrativeSettings::Get();
		UE_LOG(LogTemp, Display, TEXT("[LeoNarrative 配置]"));
		UE_LOG(LogTemp, Display, TEXT("  剧本目录: %s"), *C->GetScriptsDirPath());
		UE_LOG(LogTemp, Display, TEXT("  译文目录: %s"), *C->GetL10nDirPath());
		UE_LOG(LogTemp, Display, TEXT("  全局档槽: %s / 进度档槽: %s"), *C->GlobalSlotName, *C->ProgressSlotName);
		UE_LOG(LogTemp, Display, TEXT("  内置订阅者: Stage=%s Audio=%s DialogueUI=%s"),
			C->bCreateBuiltinStage ? TEXT("开") : TEXT("关"),
			C->bCreateBuiltinAudio ? TEXT("开") : TEXT("关"),
			C->bCreateBuiltinDialogueUI ? TEXT("开") : TEXT("关"));
		UE_LOG(LogTemp, Display, TEXT("  对话 UI 类: %s"),
			C->DialogueWidgetClass.IsValid() ? *C->DialogueWidgetClass.ToString() : TEXT("(内置纯 C++ 实现)"));
		UE_LOG(LogTemp, Display, TEXT("  默认清单: %s"),
			*C->DefaultManifest.ToSoftObjectPath().ToString());
		UE_LOG(LogTemp, Display, TEXT("  auto=%.2fs skip=%.3fs"), C->AutoAdvanceDelay, C->SkipAdvanceDelay);
	}));

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

// ---- 章节资产预载 ----

// leo.preload <chapter>：静态收割本章资产逻辑名 → 清单解析 → 异步批量预载
// （转场窗口调用；完成后 OnPreloadComplete 广播 / IsPreloadComplete 为真）
static FAutoConsoleCommand GLeoPreload(
	TEXT("leo.preload"), TEXT("预载章节资产: leo.preload <chapter>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("用法: leo.preload <chapter>")); return; }
		RunWhenSubsystemReady([Args](ULeoNarrativeSubsystem* S)
		{
			S->PreloadChapter(*Args[0]);
		});
	}));

static FAutoConsoleCommand GLeoReleasePreload(
	TEXT("leo.releasepreload"), TEXT("释放预载句柄（资产交还 GC）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		if (ULeoNarrativeSubsystem* S = GetLeoSubsystem()) { S->ReleasePreloadedAssets(); }
	}));

// leo.demopreload：内存清单 + 内存章节验证整条预载链路（收割→分表解析→异步加载→常驻），
// 不跑 VM，无头可回归。断言：引擎贴图 ResolveObject 命中 + 未命中逻辑名清单正确。
static FAutoConsoleCommand GLeoDemoPreload(
	TEXT("leo.demopreload"), TEXT("章节预载链路演示（内存清单+引擎贴图，自动断言）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
	{
		RunWhenSubsystemReady([](ULeoNarrativeSubsystem* S)
		{
			// bg_engine 命中（Bg 分表 → 引擎自带贴图）；hero/bgm_missing/cut_intro 故意缺清单
			S->GetRegistry()->CompileMemory(TEXT("demo_preload_ch"), TEXT(R"LEO(
bg bg_engine
char center hero
bgm bgm_missing
seq cut_intro
text - | 预载演示（不推进）。
end
)LEO"));
			ULeoAssetManifest* M = NewObject<ULeoAssetManifest>(S);
			FLeoManifestCategory& BgCat = M->Categories.AddDefaulted_GetRef();
			BgCat.Name = ULeoAssetManifest::BgCategory();
			BgCat.Assets.Add(TEXT("bg_engine"), FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
			S->SetManifest(M);

			const bool bStarted = S->PreloadChapter(TEXT("demo_preload_ch"));
			static FTSTicker::FDelegateHandle DemoPreloadHandle;
			FTSTicker::GetCoreTicker().RemoveTicker(DemoPreloadHandle);
			DemoPreloadHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S, bStarted](float)
			{
				if (!S->IsPreloadComplete()) { return true; } // 等异步加载收口

				const FSoftObjectPath TexPath(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
				const bool bTexResident = TexPath.ResolveObject() != nullptr;
				const TArray<FName>& Missed = S->GetPreloadMissedIds();
				const int32 Ok =
					(bStarted ? 1 : 0)
					+ (S->GetPreloadAssetCount() == 1 ? 1 : 0)
					+ (bTexResident ? 1 : 0)
					+ (Missed.Contains(TEXT("bgm_missing")) && Missed.Contains(TEXT("cut_intro"))
						&& Missed.Contains(TEXT("hero")) && !Missed.Contains(TEXT("bg_engine")) ? 1 : 0);
				if (Ok == 4)
				{
					UE_LOG(LogTemp, Display, TEXT("[demopreload] PASS（收割 4 命中 1、贴图常驻、缺失清单 3 项正确）"));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[demopreload] FAIL（断言 %d/4：发起=%d 命中=%d 常驻=%d 缺失表=%d）"),
						Ok, bStarted ? 1 : 0, S->GetPreloadAssetCount(), bTexResident ? 1 : 0,
						Missed.Contains(TEXT("bgm_missing")) && Missed.Contains(TEXT("cut_intro")) && Missed.Contains(TEXT("hero")) ? 1 : 0);
				}
				return false;
			}));
		});
	}));

// ---- 清单工具 ----

// leo.manifest build <源清单软路径> [新资产名=源名_Categorized]
// 从旧平表清单构建「类别节」新清单（源资产原样保留，不复用不迁移）：
// 分拣依据 = 全工程脚本的使用类别（命令语义权威，与校验同源）；
// 无脚本引用的条目入 uncategorized 节待人工归类；完成后经 AssetRegistry 核对存在性
// 并保存到 /Game/Leo/Data/<新资产名>。无头可跑（LeoRun），编辑器里同样可用。
static FAutoConsoleCommand GLeoManifest(
	TEXT("leo.manifest"), TEXT("清单工具: leo.manifest build <源清单软路径> [新资产名]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		RunWhenSubsystemReady([Args](ULeoNarrativeSubsystem* S)
		{
			if (Args.Num() < 2 || Args[0] != TEXT("build"))
			{
				UE_LOG(LogTemp, Warning, TEXT("用法: leo.manifest build <源清单软路径> [新资产名]"));
				return;
			}
			ULeoAssetManifest* Src = LoadObject<ULeoAssetManifest>(nullptr, *Args[1]);
			if (!Src) { UE_LOG(LogTemp, Error, TEXT("[manifest] 源清单加载失败: %s"), *Args[1]); return; }
			if (Src->Assets.Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[manifest] 源清单旧平表为空（已是类别节形态则无需构建）"));
				return;
			}
			const FString NewName = Args.Num() >= 3 ? Args[2]
				: FPaths::GetBaseFilename(Args[1]) + TEXT("_Categorized");

			// 1) 全章节引用收集：id → 首个使用类别（与校验同源：内核收集器 + seq 框架声明）
			TMap<FName, FName> KindOf;
			{
				TArray<LeoBridge::FLeoCustomAssetArgInfo> FrameworkArgs;
				ULeoSequencerPerformer::GetFrameworkAssetArgs(FrameworkArgs);
				for (const FName Chapter : S->GetRegistry()->GetChapterNames())
				{
					TSharedPtr<leo::FLeoProgram> Program;
					if (!S->GetRegistry()->TryGetProgram(Chapter, Program)) { continue; }
					TArray<LeoBridge::FLeoAssetRefInfo> Refs;
					LeoBridge::CollectProgramAssetRefs(*Program, FrameworkArgs, Refs);
					for (const LeoBridge::FLeoAssetRefInfo& R : Refs)
					{
						const FName Id(*R.Id);
						if (!KindOf.Contains(Id)) { KindOf.Add(Id, R.Kind); }
					}
				}
			}

			// 2) 新清单：预填 6 内置类别节 + uncategorized，按使用类别分拣旧平表条目
			const FString PkgPath = FString::Printf(TEXT("/Game/Leo/Data/%s"), *NewName);
			UPackage* Pkg = CreatePackage(*PkgPath);
			ULeoAssetManifest* New = NewObject<ULeoAssetManifest>(Pkg, *NewName,
				RF_Public | RF_Standalone | RF_Transactional);
			auto EnsureCategory = [New](FName Name) -> FLeoManifestCategory&
			{
				if (FLeoManifestCategory* C = New->FindCategory(Name)) { return *C; }
				FLeoManifestCategory& C = New->Categories.AddDefaulted_GetRef();
				C.Name = Name;
				return C;
			};
			for (const FName Builtin : {
				ULeoAssetManifest::BgmCategory(), ULeoAssetManifest::SeCategory(), ULeoAssetManifest::VoiceCategory(),
				ULeoAssetManifest::BgCategory(), ULeoAssetManifest::CharCategory(), ULeoAssetManifest::SeqCategory(),
			})
			{
				EnsureCategory(Builtin);
			}
			FLeoManifestCategory* Uncat = nullptr; // 懒创建：只在确有未归类条目时建节
			auto UncatSlot = [&Uncat, &EnsureCategory]() -> FLeoManifestCategory&
			{
				if (!Uncat) { Uncat = &EnsureCategory(TEXT("uncategorized")); }
				return *Uncat;
			};

			int32 FromScript = 0, FromPrefix = 0, Unreferenced = 0;
			// 无脚本引用时按名字前缀回退（注意 bgm_ 先于 bg_，避免短前缀截胡）
			const TPair<const TCHAR*, FName> Prefixes[] = {
				{ TEXT("bgm_"),   ULeoAssetManifest::BgmCategory() },
				{ TEXT("voice_"), ULeoAssetManifest::VoiceCategory() },
				{ TEXT("bg_"),    ULeoAssetManifest::BgCategory() },
				{ TEXT("char_"),  ULeoAssetManifest::CharCategory() },
				{ TEXT("seq_"),   ULeoAssetManifest::SeqCategory() },
				{ TEXT("se_"),    ULeoAssetManifest::SeCategory() },
			};
			for (const TPair<FName, FSoftObjectPath>& KV : Src->Assets)
			{
				const FString Id = KV.Key.ToString();
				if (const FName* Kind = KindOf.Find(KV.Key))
				{
					EnsureCategory(*Kind).Assets.Add(KV.Key, KV.Value);
					++FromScript;
					UE_LOG(LogTemp, Display, TEXT("[manifest] %s → %s"), *Id, *Kind->ToString());
					continue;
				}
				FName PrefixKind = NAME_None;
				for (const TPair<const TCHAR*, FName>& P : Prefixes)
				{
					if (Id.StartsWith(P.Key)) { PrefixKind = P.Value; break; }
				}
				if (!PrefixKind.IsNone())
				{
					EnsureCategory(PrefixKind).Assets.Add(KV.Key, KV.Value);
					++FromPrefix;
					UE_LOG(LogTemp, Display, TEXT("[manifest] %s → %s（名字前缀推断）"), *Id, *PrefixKind.ToString());
				}
				else
				{
					UncatSlot().Assets.Add(KV.Key, KV.Value);
					++Unreferenced;
					UE_LOG(LogTemp, Warning, TEXT("[manifest] %s → uncategorized（无脚本引用且前缀无法推断，待人工归类）"), *Id);
				}
			}

			// 3) 存在性核对（AssetRegistry，不加载本体）
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry* Reg = &ARM.GetRegistry();
			int32 Missing = 0;
			for (const FLeoManifestCategory& Cat : New->Categories)
			{
				for (const TPair<FName, FSoftObjectPath>& KV : Cat.Assets)
				{
					if (!Reg->GetAssetByObjectPath(KV.Value, /*bIncludeOnlyOnDiskAssets=*/true).IsValid())
					{
						++Missing;
						UE_LOG(LogTemp, Error, TEXT("[manifest] [%s] %s → 资产不存在: %s"),
							*Cat.Name.ToString(), *KV.Key.ToString(), *KV.Value.ToString());
					}
				}
			}

			// 4) 注册并保存
			FAssetRegistryModule::AssetCreated(New);
			New->MarkPackageDirty();
			const FString FileName = FPackageName::LongPackageNameToFilename(PkgPath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			const bool bSaved = UPackage::SavePackage(Pkg, New, *FileName, SaveArgs);
			UE_LOG(LogTemp, Display, TEXT("[manifest] 构建完成: %s（脚本归类 %d / 前缀推断 %d / 未归类 %d / 缺失 %d）保存%s: %s"),
				*NewName, FromScript, FromPrefix, Unreferenced, Missing, bSaved ? TEXT("成功") : TEXT("失败"), *FileName);
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
