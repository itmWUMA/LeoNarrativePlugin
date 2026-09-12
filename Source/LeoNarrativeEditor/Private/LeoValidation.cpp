#include "LeoValidation.h"

#include "Data/LeoAssetManifest.h"
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

	// 宿主工程剧本（同时提取资源引用）
	TMap<FName, FLeoAssetUsage> Usages;
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
				CollectAssetUsages(Program, Path, Usages);
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

	return Summary;
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

	int32 Total = S.Files.Num();
	if (!S.ManifestPath.IsEmpty())
	{
		UE_LOG(LogLeoValidate, Display, TEXT("清单核对: %s（%d 项错误）"), *S.ManifestPath, S.AssetErrors);
	}
	else
	{
		UE_LOG(LogLeoValidate, Display, TEXT("清单核对: 未找到 ULeoAssetManifest，跳过（工程无清单时为正常）"));
	}
	UE_LOG(LogLeoValidate, Display, TEXT("LeoValidate 完成: %d 个文件, %d 个不符 + %d 项资产错误"),
		Total, S.Failures, S.AssetErrors);
	return S.Failures + S.AssetErrors;
}

} // namespace LeoValidation
