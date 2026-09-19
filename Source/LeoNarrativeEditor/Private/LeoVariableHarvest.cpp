#include "LeoVariableHarvest.h"
#include "Settings/LeoNarrativeSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/LeoScenarioGraph.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScriptRuntime/LeoScriptBridge.h"

namespace
{
	FString KindToString(leo::FLeoValue::EKind Kind)
	{
		switch (Kind)
		{
		case leo::FLeoValue::EKind::Bool:   return TEXT("Bool");
		case leo::FLeoValue::EKind::Int:    return TEXT("Int");
		case leo::FLeoValue::EKind::Float:  return TEXT("Float");
		case leo::FLeoValue::EKind::String: return TEXT("String");
		default:                            return FString();
		}
	}

	void MergeVar(TMap<FName, FLeoKnownVar>& Out, const FName& Name, bool bWritten, bool bGlobal, bool bRead,
		const FString& Kind, const FName& Source)
	{
		FLeoKnownVar& V = Out.FindOrAdd(Name);
		if (Name != V.Name) { V.Name = Name; V.Source = Source; } // 首见来源
		if (bWritten) { V.bWritten = true; }
		if (bGlobal)  { V.bGlobal = true; }
		if (bRead)    { V.bRead = true; }
		if (!Kind.IsEmpty() && V.Kind.IsEmpty()) { V.Kind = Kind; }
	}
}

FLeoVariableHarvest& FLeoVariableHarvest::Get()
{
	static FLeoVariableHarvest Instance;
	return Instance;
}

const TMap<FName, FLeoKnownVar>& FLeoVariableHarvest::GetVars()
{
	if (bStale)
	{
		Rebuild();
		bStale = false;
	}
	return Vars;
}

TArray<FName> FLeoVariableHarvest::GetSortedNames() const
{
	TArray<FName> Names;
	Vars.GetKeys(Names);
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Names;
}

void FLeoVariableHarvest::Rebuild()
{
	Vars.Reset();

	// 脚本：Content/Scripts/*.leo 逐个编译收割（与 LeoValidate 同一加载路径，不依赖注册表状态）
	const FString ScriptsDir = ULeoNarrativeSettings::Get()->GetScriptsDirPath();
	if (FPaths::DirectoryExists(ScriptsDir))
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(ScriptsDir / TEXT("*.leo")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			HarvestScript(ScriptsDir / File);
		}
	}

	HarvestGraphAssets();

	UE_LOG(LogTemp, Verbose, TEXT("[Leo] 变量收割: %d 个（脚本 + 图资产）"), Vars.Num());
}

void FLeoVariableHarvest::HarvestScript(const FString& Path)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return;
	}
	int32 Start = 0;
	if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF) { Start = 3; } // UTF-8 BOM
	const std::string Source(reinterpret_cast<const char*>(Bytes.GetData()) + Start, Bytes.Num() - Start);

	const FName Chapter(*FPaths::GetBaseFilename(Path));
	// 经桥接编译（内核符号不直接跨模块导出，铁律见 LeoScriptBridge.h）
	leo::FLeoProgram Program = LeoBridge::CompileChapter(LeoBridge::ToFString(Source), Chapter.ToString());
	TArray<LeoBridge::FLeoVarUsageInfo> Usages;
	LeoBridge::CollectProgramVarUsage(Program, Usages);
	for (const LeoBridge::FLeoVarUsageInfo& U : Usages)
	{
		MergeVar(Vars, FName(*U.Name), U.bWritten, U.bGlobal, U.bRead, KindToString(U.LitKind), Chapter);
	}
}

void FLeoVariableHarvest::HarvestGraphAssets()
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry* Reg = &ARM.GetRegistry();
	TArray<FAssetData> Found;
	Reg->GetAssetsByClass(ULeoScenarioGraph::StaticClass()->GetClassPathName(), Found, true);
	for (const FAssetData& Data : Found)
	{
		const ULeoScenarioGraph* G = Cast<ULeoScenarioGraph>(
			StaticLoadObject(ULeoScenarioGraph::StaticClass(), nullptr, *Data.GetObjectPathString()));
		if (!G) { continue; }
		const FName Source(*Data.AssetName.ToString());
		for (const FLeoScenarioNode& N : G->Nodes)
		{
			for (const FLeoScenarioEdge& E : N.Edges)
			{
				TArray<FString> Reads;
				if (!E.Condition.TrimStartAndEnd().IsEmpty())
				{
					LeoBridge::CollectExprReads(E.Condition, Reads);
				}
				for (const FString& R : Reads)
				{
					MergeVar(Vars, FName(*R), false, false, true, FString(), Source);
				}
				for (const FLeoEdgeAction& A : E.Actions)
				{
					TArray<FString> ActionReads;
					LeoBridge::CollectExprReads(A.Expr, ActionReads);
					const bool bRead = ActionReads.Contains(A.Key.ToString());
					MergeVar(Vars, A.Key, true, A.bGlobal, bRead, FString(), Source);
					for (const FString& R : ActionReads)
					{
						MergeVar(Vars, FName(*R), false, false, true, FString(), Source);
					}
				}
			}
		}
	}
}
