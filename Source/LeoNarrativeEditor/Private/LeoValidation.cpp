#include "LeoValidation.h"

#include "Data/LeoAssetManifest.h"
#include "Data/LeoScenarioGraph.h"
#include "LeoConditionCodec.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "LevelSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoValidate, Log, All);

namespace LeoValidation
{

FLeoOnScriptsRevalidated OnScriptsRevalidated;

namespace
{
	// 读文件为 UTF-8 字节（去 BOM），不经过 TCHAR 往返，避免转换损耗
	bool LoadUtf8File(const FString& Path, std::string& Out)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return false;
		}
		int32 Start = 0;
		if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
		{
			Start = 3; // UTF-8 BOM
		}
		Out.assign(reinterpret_cast<const char*>(Bytes.GetData()) + Start, Bytes.Num() - Start);
		return true;
	}

	FLeoCheckItem MakeItem(const FString& File, int32 Line, const FString& Code, const FString& Message, bool bError)
	{
		FLeoCheckItem It;
		It.File = File; It.Line = Line; It.Code = Code; It.Message = Message; It.bError = bError;
		return It;
	}

	void LogItem(const FLeoCheckItem& It)
	{
		// 语料的预期诊断用 Warning 级：引擎会在错误计数 >0 时强制退出码 1，
		// 即使 Main 返回 0（LaunchEngineLoop.cpp:4220）
		const FString Loc = It.Line > 0
			? FString::Printf(TEXT("%s(%d): "), *It.File, It.Line)
			: It.File + TEXT(": ");
		const FString Msg = FString::Printf(TEXT("%s%s  %s"), *Loc, *It.Code, *It.Message);
		if (It.bError)
		{
			UE_LOG(LogLeoValidate, Error, TEXT("%s"), *Msg);
		}
		else
		{
			UE_LOG(LogLeoValidate, Warning, TEXT("%s"), *Msg);
		}
	}

	// 编译单文件 → 结构化结果（诊断分级：期待干净时错误=Error，语料预期错误=Warning）
	void CompileFileToResult(const FString& Path, bool bExpectClean, FLeoFileResult& Out,
		leo::FLeoProgram* OutProgram = nullptr)
	{
		Out.Path = Path;
		Out.bExpectClean = bExpectClean;
		Out.bPass = true;

		std::string Source;
		if (!LoadUtf8File(Path, Source))
		{
			Out.Items.Add(MakeItem(Path, 0, TEXT("READ_FAIL"), TEXT("读取失败"), true));
			Out.bPass = false;
			return;
		}
		const FString Name = FPaths::GetBaseFilename(Path);
		leo::FLeoProgram Program = LeoBridge::CompileChapter(LeoBridge::ToFString(Source), Name);
		for (const leo::FLeoDiag& D : Program.Diags)
		{
			const bool bErr = leo::IsLeoError(D.Code);
			Out.Items.Add(MakeItem(Path, D.Line, LeoBridge::DiagName(D.Code),
				LeoBridge::ToFString(D.Msg), bExpectClean && bErr));
		}
		if (bExpectClean && !Program.Ok) { Out.bPass = false; }
		if (!bExpectClean && Program.Ok)
		{
			Out.Items.Add(MakeItem(Path, 0, TEXT("REGRESSION"),
				TEXT("错误语料却编译通过——错误检测能力回归"), true));
			Out.bPass = false;
		}
		if (OutProgram) { *OutProgram = MoveTemp(Program); }
	}

	// ---- 资产引用核对 ----

	// 一个逻辑名的全部使用点（首个位置用于报错定位）
	struct FLeoAssetUsage
	{
		FString FirstFile;
		int32 FirstLine = 0;
		bool bWantsSound = false; // bgm/se/voice → USoundBase
		bool bWantsSequence = false; // seq → ULevelSequence
	};

	// 从编译产物提取资源引用（只扫工程剧本；golden 语料用的是假名，不参与）
	void CollectAssetUsages(const leo::FLeoProgram& Program, const FString& Path,
		TMap<FName, FLeoAssetUsage>& Out)
	{
		for (const leo::FLeoCommand& C : Program.Commands)
		{
			FName LogicalId;
			FLeoAssetUsage Usage;
			Usage.FirstFile = FPaths::GetCleanFilename(Path);
			Usage.FirstLine = C.Line;
			switch (C.Kind)
			{
			case leo::ELeoCmd::Bgm:
			case leo::ELeoCmd::Se:
			case leo::ELeoCmd::Voice:
				LogicalId = FName(C.AssetId.c_str());
				Usage.bWantsSound = true;
				break;
			case leo::ELeoCmd::Bg:
			case leo::ELeoCmd::Char:
				LogicalId = FName(C.AssetId.c_str());
				break; // 背景立绘类型由项目表现层定，只做存在性核对
			case leo::ELeoCmd::Custom:
				if (C.CustomName == "seq" && !C.CustomArgs.empty())
				{
					LogicalId = FName(C.CustomArgs[0].c_str());
					Usage.bWantsSequence = true;
				}
				break;
			default: break;
			}
			if (LogicalId.IsNone() || LogicalId == TEXT("-")) { continue; } // "-" = 移除/停止
			FLeoAssetUsage& U = Out.FindOrAdd(LogicalId);
			if (U.FirstFile.IsEmpty()) { U = Usage; }
			else
			{
				U.bWantsSound |= Usage.bWantsSound;
				U.bWantsSequence |= Usage.bWantsSequence;
			}
		}
	}

	// 自动发现工程内的 ULeoAssetManifest（唯一的直接用；多个时取字典序第一个并提示）
	ULeoAssetManifest* DiscoverManifest(FString& OutAssetPath, FString& OutNote)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry* Reg = &ARM.GetRegistry();
		if (!Reg) { return nullptr; }

		TArray<FAssetData> Found;
		Reg->GetAssetsByClass(ULeoAssetManifest::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses=*/true);
		if (Found.Num() == 0)
		{
			// 命令行环境 registry 可能尚未扫完工程目录，强制补扫一次
			Reg->ScanPathsSynchronous({ TEXT("/Game") }, /*bForceRescan=*/false);
			Reg->GetAssetsByClass(ULeoAssetManifest::StaticClass()->GetClassPathName(), Found, true);
		}
		if (Found.Num() == 0) { return nullptr; }
		Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
		if (Found.Num() > 1)
		{
			OutNote = FString::Printf(TEXT("工程内有 %d 个清单资产，取 %s（可用 -manifest= 指定）"),
				Found.Num(), *Found[0].GetObjectPathString());
		}
		OutAssetPath = Found[0].GetObjectPathString();
		return Cast<ULeoAssetManifest>(StaticLoadObject(ULeoAssetManifest::StaticClass(), nullptr, *OutAssetPath));
	}

	// 清单条目 → 资产存在性 + 类型核对（经 AssetRegistry，不加载资产本体）
	void CheckManifestEntries(const ULeoAssetManifest* Manifest,
		const TMap<FName, FLeoAssetUsage>& Usages, TArray<FLeoCheckItem>& Out, int32& OutErrors)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry* Reg = &ARM.GetRegistry();

		for (const TPair<FName, FSoftObjectPath>& KV : Manifest->Assets)
		{
			const FAssetData Data = Reg->GetAssetByObjectPath(KV.Value, /*bIncludeOnlyOnDiskAssets=*/true);
			if (!Data.IsValid())
			{
				Out.Add(MakeItem(Manifest->GetName(), 0, TEXT("ASSET_NOT_FOUND"),
					FString::Printf(TEXT("%s → %s"), *KV.Key.ToString(), *KV.Value.ToString()), true));
				++OutErrors;
				continue;
			}
			// 类型核对：按脚本用途（bgm/se/voice→声音，seq→序列）；
			// UMetaSoundSource 等是 USoundBase 子类，IsChildOf 天然覆盖
			const FLeoAssetUsage* Usage = Usages.Find(KV.Key);
			if (!Usage) { continue; }
			UClass* AssetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *Data.AssetClassPath.ToString());
			const UClass* WantClass = Usage->bWantsSequence ? ULevelSequence::StaticClass()
				: Usage->bWantsSound ? USoundBase::StaticClass() : nullptr;
			if (WantClass && (!AssetClass || !AssetClass->IsChildOf(WantClass)))
			{
				Out.Add(MakeItem(Manifest->GetName(), 0, TEXT("ASSET_WRONG_TYPE"),
					FString::Printf(TEXT("%s 需要 %s，实际 %s"), *KV.Key.ToString(),
						*WantClass->GetName(), *Data.AssetClassPath.ToString()), true));
				++OutErrors;
			}
		}
	}

	// ---- 编排图核对 ----

	const TCHAR* NodeType_name(ELeoScenarioNodeType T)
	{
		switch (T)
		{
		case ELeoScenarioNodeType::Chapter:   return TEXT("Chapter");
		case ELeoScenarioNodeType::Branch:    return TEXT("Branch");
		case ELeoScenarioNodeType::Ending:    return TEXT("Ending");
		case ELeoScenarioNodeType::Subgraph:  return TEXT("Subgraph");
		default: return TEXT("?");
		}
	}

	// 单图结构核对（纯函数：正常图与破损图都走这里，自测复用）
	// WrittenVars：全工程已知"有写入"的变量集合（脚本 set/setg + 全部图的边副作用 Key）——拼写检查基准
	void CheckGraph(const ULeoScenarioGraph* G, const FString& DisplayName,
		const TMap<FName, TSet<FName>>& ChapterLabels, const TSet<FName>& WrittenVars,
		TArray<FLeoCheckItem>& Out, int32& OutErrors)
	{
		auto Add = [&](int32 NodeIdx, const FString& Code, const FString& Msg, bool bError)
		{
			Out.Add(MakeItem(DisplayName, 0, Code,
				FString::Printf(TEXT("%s%s"), NodeIdx >= 0 ? *FString::Printf(TEXT("节点[%d] "), NodeIdx) : TEXT(""), *Msg),
				bError));
			if (bError) { ++OutErrors; }
		};

		if (!G->EntryNode.IsNone() && !G->FindNode(G->EntryNode))
		{
			Add(-1, TEXT("GRAPH_BAD_ENTRY"), TEXT("入口节点不存在"), true);
		}

		// Id 唯一性 + 逐节点检查
		TSet<FName> SeenIds;
		for (int32 i = 0; i < G->Nodes.Num(); ++i)
		{
			const FLeoScenarioNode& N = G->Nodes[i];
			if (N.Id.IsNone()) { Add(i, TEXT("GRAPH_NO_ID"), TEXT("节点缺少 Id"), true); continue; }
			if (SeenIds.Contains(N.Id)) { Add(i, TEXT("GRAPH_DUP_ID"), FString::Printf(TEXT("节点 Id 重复: %s"), *N.Id.ToString()), true); }
			SeenIds.Add(N.Id);

			// 出边存在性 + 表达式可编译 + 变量拼写检查
			for (const FLeoScenarioEdge& E : N.Edges)
			{
				if (!G->FindNode(E.To))
				{
					Add(i, TEXT("GRAPH_DANGLING_EDGE"), FString::Printf(TEXT("边指向不存在的节点: %s"), *E.To.ToString()), true);
				}
				TArray<FString> Reads;
				if (!E.Condition.TrimStartAndEnd().IsEmpty())
				{
					leo::FLeoDiag D;
					const leo::FLeoExprPtr Expr = LeoBridge::CompileExpr(E.Condition, D);
					if (!Expr)
					{
						Add(i, TEXT("GRAPH_BAD_EXPR"), FString::Printf(TEXT("边条件编译失败: %s (%s)"),
							*E.Condition, LeoBridge::DiagName(D.Code)), true);
					}
					else
					{
						LeoBridge::CollectExprReadsFrom(Expr, Reads);
					}
				}
				for (const FLeoEdgeAction& A : E.Actions)
				{
					leo::FLeoDiag D;
					const leo::FLeoExprPtr ActionExpr = LeoBridge::CompileExpr(A.Expr, D);
					if (A.Key.IsNone() || !ActionExpr)
					{
						Add(i, TEXT("GRAPH_BAD_ACTION"), FString::Printf(TEXT("边副作用不合法: %s %s %s"),
							*A.Key.ToString(), LeoEdgeOpString(A.Operation), *A.Expr), true);
					}
					else
					{
						LeoBridge::CollectExprReadsFrom(ActionExpr, Reads);
					}
				}
				// 未知变量：从未被任何 set/setg/边副作用写入——游戏代码写入的变量可忽略，此处只警告
				for (const FString& Read : Reads)
				{
					if (!WrittenVars.Contains(FName(*Read)))
					{
						Add(i, TEXT("GRAPH_UNKNOWN_VAR"), FString::Printf(
							TEXT("变量 %s 从未被写入（set/setg/边副作用均无）——拼写错误？由游戏代码写入可忽略"),
							*Read), false);
					}
				}
			}

			switch (N.Type)
			{
			case ELeoScenarioNodeType::Chapter:
			{
				const TSet<FName>* Labels = ChapterLabels.Find(N.Chapter);
				if (!Labels) { Add(i, TEXT("GRAPH_NO_CHAPTER"), FString::Printf(TEXT("章节不存在或编译失败: %s"), *N.Chapter.ToString()), true); }
				else if (!N.Label.IsNone() && !Labels->Contains(N.Label))
				{
					Add(i, TEXT("GRAPH_NO_LABEL"), FString::Printf(TEXT("label 不存在: %s@%s"), *N.Chapter.ToString(), *N.Label.ToString()), true);
				}
				break;
			}
			case ELeoScenarioNodeType::Subgraph:
				if (!N.SubGraph) { Add(i, TEXT("GRAPH_NO_SUBGRAPH"), TEXT("Subgraph 节点未配置子图"), true); }
				else if (N.SubGraph == G) { Add(i, TEXT("GRAPH_SELF_SUBGRAPH"), TEXT("子图引用自身（运行期嵌套上限保护）"), false); }
				break;
			case ELeoScenarioNodeType::Ending:
				if (N.EndingId.IsNone()) { Add(i, TEXT("GRAPH_UNNAMED_ENDING"), TEXT("Ending 未设 EndingId"), false); }
				break;
			default: break;
			}

			// 死端：非 Ending 节点必须有出边
			if (N.Type != ELeoScenarioNodeType::Ending && N.Edges.Num() == 0)
			{
				Add(i, TEXT("GRAPH_DEAD_END"),
					FString::Printf(TEXT("%s 节点无出边（玩家会在此卡死）"), NodeType_name(N.Type)), true);
			}
		}

		// 结构可达性（忽略条件）：入口 BFS；无可达 Ending = 错误；不可达节点 = 警告
		if (!G->EntryNode.IsNone() && G->FindNode(G->EntryNode))
		{
			TSet<FName> Visited;
			TArray<FName> Queue = { G->EntryNode };
			bool bEndingReachable = false;
			while (!Queue.IsEmpty())
			{
				const FName Cur = Queue.Pop();
				if (Visited.Contains(Cur)) { continue; }
				Visited.Add(Cur);
				const FLeoScenarioNode* N = G->FindNode(Cur);
				if (!N) { continue; }
				if (N->Type == ELeoScenarioNodeType::Ending) { bEndingReachable = true; }
				for (const FLeoScenarioEdge& E : N->Edges)
				{
					if (!Visited.Contains(E.To)) { Queue.Add(E.To); }
				}
			}
			if (!bEndingReachable)
			{
				Add(-1, TEXT("GRAPH_NO_ENDING"), TEXT("从入口结构上无法到达任何 Ending 节点"), true);
			}
			for (int32 i = 0; i < G->Nodes.Num(); ++i)
			{
				if (!G->Nodes[i].Id.IsNone() && !Visited.Contains(G->Nodes[i].Id))
				{
					Add(i, TEXT("GRAPH_UNREACHABLE"), TEXT("节点从入口不可达"), false);
				}
			}
		}
	}

	// 发现 /Game 下的编排图资产并核对（无图 = 跳过）。
	// 先收齐全部图的边副作用 Key 再逐图核对——跨图写入也算已知变量
	void CheckAllGraphAssets(const TMap<FName, TSet<FName>>& ChapterLabels, const TSet<FName>& ScriptWrittenVars,
		TArray<FLeoCheckItem>& Out, int32& OutErrors)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry* Reg = &ARM.GetRegistry();
		TArray<FAssetData> Found;
		Reg->GetAssetsByClass(ULeoScenarioGraph::StaticClass()->GetClassPathName(), Found, true);

		TArray<TPair<FString, ULeoScenarioGraph*>> Graphs;
		TSet<FName> WrittenVars = ScriptWrittenVars;
		for (const FAssetData& Data : Found)
		{
			if (!Data.GetObjectPathString().StartsWith(TEXT("/Game"))) { continue; }
			ULeoScenarioGraph* G = Cast<ULeoScenarioGraph>(StaticLoadObject(ULeoScenarioGraph::StaticClass(), nullptr, *Data.GetObjectPathString()));
			if (!G) { continue; }
			Graphs.Emplace(Data.GetObjectPathString(), G);
			for (const FLeoScenarioNode& N : G->Nodes)
			{
				for (const FLeoScenarioEdge& E : N.Edges)
				{
					for (const FLeoEdgeAction& A : E.Actions)
					{
						if (!A.Key.IsNone()) { WrittenVars.Add(A.Key); }
					}
				}
			}
		}
		for (const TPair<FString, ULeoScenarioGraph*>& KV : Graphs)
		{
			CheckGraph(KV.Value, KV.Key, ChapterLabels, WrittenVars, Out, OutErrors);
		}
	}
} // namespace

FLeoValidateSummary ValidateAllStructured(const FString& ManifestAssetPath)
{
	FLeoValidateSummary Summary;

	auto ValidateDir = [&](const FString& Dir, bool bExpectClean)
	{
		if (!FPaths::DirectoryExists(Dir)) { return; }
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.leo")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FLeoFileResult R;
			CompileFileToResult(Dir / File, bExpectClean, R);
			if (!R.bPass) { ++Summary.Failures; }
			Summary.Files.Add(MoveTemp(R));
		}
	};

	// 宿主工程剧本（同时提取资源引用 + 章节 label 表 + 变量写入集合）
		TMap<FName, FLeoAssetUsage> Usages;
		TMap<FName, TSet<FName>> ChapterLabels;
		TSet<FName> ScriptWrittenVars;
		const FString ScriptsDir = FPaths::ProjectContentDir() / TEXT("Scripts");
	if (FPaths::DirectoryExists(ScriptsDir))
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(ScriptsDir / TEXT("*.leo")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			const FString Path = ScriptsDir / File;
			FLeoFileResult R;
			leo::FLeoProgram Program;
			CompileFileToResult(Path, true, R, &Program);
			if (R.bPass && Program.Ok)
			{
				TSet<FName>& Labels = ChapterLabels.FindOrAdd(FName(*FPaths::GetBaseFilename(Path)));
				for (const auto& KV : Program.LabelIndex)
				{
					Labels.Add(FName(KV.first.c_str()));
				}
			}
			if (R.bPass && Program.Ok)
			{
				CollectAssetUsages(Program, Path, Usages);
				TArray<LeoBridge::FLeoVarUsageInfo> VarUsages;
				LeoBridge::CollectProgramVarUsage(Program, VarUsages);
				for (const LeoBridge::FLeoVarUsageInfo& V : VarUsages)
				{
					if (V.bWritten) { ScriptWrittenVars.Add(FName(*V.Name)); }
				}
			}
			if (!R.bPass) { ++Summary.Failures; }
			Summary.Files.Add(MoveTemp(R));
		}
	}

	// 插件 golden 语料（pass 零错误 / fail 必须报错）
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LeoNarrative"));
	if (Plugin.IsValid())
	{
		const FString GoldenRoot = Plugin->GetBaseDir() / TEXT("tests/golden");
		ValidateDir(GoldenRoot / TEXT("pass"), true);
		ValidateDir(GoldenRoot / TEXT("fail"), false);
	}

	// ---- 资产引用核对（有清单才执行）----
	ULeoAssetManifest* Manifest = nullptr;
	if (!ManifestAssetPath.IsEmpty())
	{
		Manifest = Cast<ULeoAssetManifest>(StaticLoadObject(ULeoAssetManifest::StaticClass(), nullptr, *ManifestAssetPath));
		Summary.ManifestPath = ManifestAssetPath;
		if (!Manifest)
		{
			Summary.AssetItems.Add(MakeItem(ManifestAssetPath, 0, TEXT("MANIFEST_LOAD_FAIL"), TEXT("指定清单加载失败"), true));
			++Summary.AssetErrors;
		}
	}
	else
	{
		FString Note;
		Manifest = DiscoverManifest(Summary.ManifestPath, Note);
		if (Manifest && !Note.IsEmpty())
		{
			Summary.AssetItems.Add(MakeItem(Summary.ManifestPath, 0, TEXT("MANIFEST_AMBIGUOUS"), Note, false));
		}
	}

	if (Manifest)
	{
		// 脚本引用了清单没有的逻辑名
		for (const TPair<FName, FLeoAssetUsage>& KV : Usages)
		{
			if (!Manifest->Assets.Contains(KV.Key))
			{
				Summary.AssetItems.Add(MakeItem(KV.Value.FirstFile, KV.Value.FirstLine, TEXT("NO_MANIFEST_ENTRY"),
					FString::Printf(TEXT("逻辑名 %s 无清单映射"), *KV.Key.ToString()), true));
				++Summary.AssetErrors;
			}
		}
		CheckManifestEntries(Manifest, Usages, Summary.AssetItems, Summary.AssetErrors);
	}

	// ---- 编排图核对（无图资产时跳过）----
	CheckAllGraphAssets(ChapterLabels, ScriptWrittenVars, Summary.GraphItems, Summary.GraphErrors);

	return Summary;
}

int32 SelfTestGraphChecks()
{
	// 内存构造正常图与破损图，走同一个核对器验证检出能力
	int32 Failures = 0;
	TMap<FName, TSet<FName>> Labels;
	Labels.Add(TEXT("chapter01"), { TEXT("lab_start"), TEXT("lab_ending") });
	TSet<FName> NoVars; // 自测无脚本上下文：空写入集（未知变量警告不计数，只行使代码路径）

	auto CountErrors = [](TArray<FLeoCheckItem>& Items)
	{
		int32 N = 0;
		for (const FLeoCheckItem& It : Items) { if (It.bError) { ++N; } }
		return N;
	};

	// 正常图：Chapter → Branch → Subgraph → Ending，0 错误
	{
		ULeoScenarioGraph* G = NewObject<ULeoScenarioGraph>(GetTransientPackage());
		G->EntryNode = TEXT("n1");
		FLeoScenarioNode N1; N1.Id = TEXT("n1"); N1.Type = ELeoScenarioNodeType::Chapter; N1.Chapter = TEXT("chapter01");
		FLeoScenarioEdge E1; E1.To = TEXT("e1"); N1.Edges = { E1 };
		FLeoScenarioNode N2; N2.Id = TEXT("e1"); N2.Type = ELeoScenarioNodeType::Ending; N2.EndingId = TEXT("end1");
		G->Nodes = { N1, N2 };
		TArray<FLeoCheckItem> Items; int32 Errors = 0;
		CheckGraph(G, TEXT("selftest-good"), Labels, NoVars, Items, Errors);
		if (Errors != 0) { UE_LOG(LogLeoValidate, Error, TEXT("[selftest-graph] 正常图应 0 错误，实得 %d"), Errors); ++Failures; }
		else { UE_LOG(LogLeoValidate, Display, TEXT("[selftest-graph] 正常图 0 错误 ✓")); }
	}

	// 破损图：入口缺失 / 悬空边 / 死端 Chapter / 章节不存在 / 无 Ending 可达 —— 各 1 错误
	{
		ULeoScenarioGraph* G = NewObject<ULeoScenarioGraph>(GetTransientPackage());
		G->EntryNode = TEXT("missing_entry");
		FLeoScenarioNode N1; N1.Id = TEXT("n1"); N1.Type = ELeoScenarioNodeType::Chapter; N1.Chapter = TEXT("no_such_chapter");
		FLeoScenarioEdge E1; E1.To = TEXT("ghost");
		FLeoScenarioEdge E2; E2.To = TEXT("n2"); E2.Condition = TEXT("typo_var >= 1"); // 未知变量 → 警告（不计入错误数）
		N1.Edges = { E1, E2 };
		FLeoScenarioNode N2; N2.Id = TEXT("n2"); N2.Type = ELeoScenarioNodeType::Branch; // 无出边 = 死端
		G->Nodes = { N1, N2 };
		TArray<FLeoCheckItem> Items; int32 Errors = 0;
		CheckGraph(G, TEXT("selftest-bad"), Labels, NoVars, Items, Errors);
		// 预期：入口缺失 1 + 悬空边 1 + 死端 1 + 章节不存在 1 = 4（无 Ending 可达暂不计入——入口不可达时跳过可达性分析）
		if (Errors != 4) { UE_LOG(LogLeoValidate, Error, TEXT("[selftest-graph] 破损图应 4 错误，实得 %d"), Errors); ++Failures; }
		else { UE_LOG(LogLeoValidate, Display, TEXT("[selftest-graph] 破损图 4 错误全检出 ✓")); }
		// 未知变量拼写检查：typo_var 不在写入集 → 恰好 1 条警告
		int32 UnknownVars = 0;
		for (const FLeoCheckItem& It : Items)
		{
			if (It.Code == TEXT("GRAPH_UNKNOWN_VAR")) { ++UnknownVars; }
		}
		if (UnknownVars == 1) { UE_LOG(LogLeoValidate, Display, TEXT("[selftest-graph] 未知变量警告 1 条检出 ✓")); }
		else { UE_LOG(LogLeoValidate, Error, TEXT("[selftest-graph] 未知变量警告应 1 条，实得 %d"), UnknownVars); ++Failures; }
	}

	// 条件编解码往返（下拉模式与表达式同源的保单：拆解 → 重生成必须还原原文）
	{
		auto CondCheck = [&Failures](bool bOk, const TCHAR* What)
		{
			if (bOk) { UE_LOG(LogLeoValidate, Display, TEXT("[selftest-graph] cond: %s ✓"), What); }
			else { UE_LOG(LogLeoValidate, Error, TEXT("[selftest-graph] cond: %s 失败"), What); ++Failures; }
		};
		TArray<FLeoCondRow> Rows;
		CondCheck(LeoConditionCodec::Parse(TEXT("affection >= 1 && saw_secret"), Rows) && Rows.Num() == 2
			&& Rows[0].Var == TEXT("affection") && Rows[0].Op == ELeoCondOp::Ge && Rows[0].Value == TEXT("1")
			&& Rows[1].Var == TEXT("saw_secret") && Rows[1].Op == ELeoCondOp::IsTrue,
			TEXT("拆解 && 链为条件行"));
		CondCheck(LeoConditionCodec::Generate(Rows) == TEXT("affection >= 1 && saw_secret"),
			TEXT("行→表达式往返一致"));
		CondCheck(LeoConditionCodec::Parse(TEXT(""), Rows) && Rows.Num() == 0 && LeoConditionCodec::Generate(Rows).IsEmpty(),
			TEXT("空条件 = 0 行恒真"));
		CondCheck(LeoConditionCodec::Parse(TEXT("!flag"), Rows) && Rows.Num() == 1
			&& Rows[0].Op == ELeoCondOp::IsFalse && LeoConditionCodec::Generate(Rows) == TEXT("!flag"),
			TEXT("否定裸变量往返"));
		CondCheck(!LeoConditionCodec::Parse(TEXT("a || b"), Rows), TEXT("|| 不拆解（留自定义模式）"));
		CondCheck(!LeoConditionCodec::Parse(TEXT("x >= y"), Rows), TEXT("变量对变量不拆解"));
		CondCheck(!LeoConditionCodec::Parse(TEXT("1 <= x"), Rows), TEXT("字面量在左不拆解"));
		CondCheck(LeoConditionCodec::Parse(TEXT("name == \"mi ka\""), Rows)
			&& LeoConditionCodec::Generate(Rows) == TEXT("name == \"mi ka\""),
			TEXT("字符串字面量往返"));
		CondCheck(LeoConditionCodec::Parse(TEXT("rate != 0.5"), Rows)
			&& Rows[0].Value == TEXT("0.5") && LeoConditionCodec::Generate(Rows) == TEXT("rate != 0.5"),
			TEXT("浮点字面量往返"));
	}

	UE_LOG(LogLeoValidate, Display, TEXT("[selftest-graph] %s"), Failures == 0 ? TEXT("通过") : TEXT("失败"));
	return Failures;
}

bool ValidateFile(const FString& Path, bool bExpectClean)
{
	FLeoFileResult R;
	CompileFileToResult(Path, bExpectClean, R);
	for (const FLeoCheckItem& It : R.Items) { LogItem(It); }
	if (!R.bPass)
	{
		UE_LOG(LogLeoValidate, Error, TEXT("[FAIL] %s"), *Path);
		return false;
	}
	UE_LOG(LogLeoValidate, Display, TEXT("[PASS] %s"), *Path);
	return true;
}

int32 ValidateAll(const FString& ManifestAssetPath)
{
	const FLeoValidateSummary S = ValidateAllStructured(ManifestAssetPath);
	for (const FLeoFileResult& R : S.Files)
	{
		for (const FLeoCheckItem& It : R.Items) { LogItem(It); }
		if (!R.bPass)
		{
			UE_LOG(LogLeoValidate, Error, TEXT("[FAIL] %s"), *R.Path);
		}
	}
	for (const FLeoCheckItem& It : S.AssetItems) { LogItem(It); }
	for (const FLeoCheckItem& It : S.GraphItems) { LogItem(It); }

	int32 Total = S.Files.Num();
	if (!S.ManifestPath.IsEmpty())
	{
		UE_LOG(LogLeoValidate, Display, TEXT("清单核对: %s（%d 项错误）"), *S.ManifestPath, S.AssetErrors);
	}
	else
	{
		UE_LOG(LogLeoValidate, Display, TEXT("清单核对: 未找到 ULeoAssetManifest，跳过（工程无清单时为正常）"));
	}
	UE_LOG(LogLeoValidate, Display, TEXT("编排图核对: %d 项错误"), S.GraphErrors);
	UE_LOG(LogLeoValidate, Display, TEXT("LeoValidate 完成: %d 个文件, %d 个不符 + %d 项资产错误 + %d 项图错误"),
		Total, S.Failures, S.AssetErrors, S.GraphErrors);
	return S.Failures + S.AssetErrors + S.GraphErrors;
}

} // namespace LeoValidation
