#include "ScriptRuntime/LeoScriptRegistry.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoRegistry, Log, All);

namespace
{
	bool ReadUtf8File(const FString& Path, FString& OutContent)
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
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()) + Start, Bytes.Num() - Start);
		OutContent = FString(Conv.Length(), Conv.Get());
		return true;
	}
}

int32 ULeoScriptRegistry::LoadAndCompileAll()
{
	// 应用自定义命令注册（须先于编译——parser 需要知道合法命令集与参数规则）
	LeoBridge::SetCustomCommandSpecs(CustomCommandSpecs);
	LeoBridge::SetCustomCommandNames(CustomCommandNames);

	Programs.Reset();
	LastDiags.Reset();

	const FString Dir = FPaths::ProjectContentDir() / TEXT("Scripts");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.leo")), true, false);
	Files.Sort();

	int32 OkCount = 0;
	for (const FString& File : Files)
	{
		if (CompileOne(Dir / File)) { ++OkCount; }
	}
	UE_LOG(LogLeoRegistry, Display, TEXT("剧本注册表: %d/%d 章编译通过 (%s)"), OkCount, Files.Num(), *Dir);
	return OkCount;
}

bool ULeoScriptRegistry::RecompileChapter(FName Chapter)
{
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Scripts");
	const FString Path = Dir / Chapter.ToString() + TEXT(".leo");
	if (!FPaths::FileExists(Path))
	{
		UE_LOG(LogLeoRegistry, Error, TEXT("重编译失败：找不到 %s"), *Path);
		return false;
	}
	LeoBridge::SetCustomCommandSpecs(CustomCommandSpecs);
	LeoBridge::SetCustomCommandNames(CustomCommandNames);
	return CompileOne(Path);
}

bool ULeoScriptRegistry::CompileMemory(FName Chapter, const FString& Source)
{
	LeoBridge::SetCustomCommandSpecs(CustomCommandSpecs);
	LeoBridge::SetCustomCommandNames(CustomCommandNames);
	leo::FLeoProgram Program = LeoBridge::CompileChapter(Source, Chapter.ToString());
	TArray<FString>& Diags = LastDiags.FindOrAdd(Chapter);
	Diags.Reset();
	for (const leo::FLeoDiag& D : Program.Diags)
	{
		const FString Line = FString::Printf(TEXT("%s(%d): %s  %s"),
			*Chapter.ToString(), D.Line, LeoBridge::DiagName(D.Code), *LeoBridge::ToFString(D.Msg));
		Diags.Add(Line);
	}
	if (!Program.Ok)
	{
		Programs.Remove(Chapter);
		UE_LOG(LogLeoRegistry, Error, TEXT("内存编译失败: %s"), *Chapter.ToString());
		return false;
	}
	Programs.Add(Chapter, MakeShared<leo::FLeoProgram>(std::move(Program)));
	UE_LOG(LogLeoRegistry, Log, TEXT("内存编译通过: %s"), *Chapter.ToString());
	return true;
}

bool ULeoScriptRegistry::CompileOne(const FString& FilePath)
{
	FString Source;
	if (!ReadUtf8File(FilePath, Source))
	{
		UE_LOG(LogLeoRegistry, Error, TEXT("读取剧本失败: %s"), *FilePath);
		return false;
	}
	const FName Chapter = *FPaths::GetBaseFilename(FilePath);
	leo::FLeoProgram Program = LeoBridge::CompileChapter(Source, Chapter.ToString());

	TArray<FString>& Diags = LastDiags.FindOrAdd(Chapter);
	Diags.Reset();
	for (const leo::FLeoDiag& D : Program.Diags)
	{
		const FString Line = FString::Printf(TEXT("%s(%d): %s  %s"),
			*Chapter.ToString(), D.Line, LeoBridge::DiagName(D.Code), *LeoBridge::ToFString(D.Msg));
		Diags.Add(Line);
		if (leo::IsLeoError(D.Code))
		{
			UE_LOG(LogLeoRegistry, Error, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogLeoRegistry, Warning, TEXT("%s"), *Line);
		}
	}
	if (!Program.Ok)
	{
		Programs.Remove(Chapter);
		return false;
	}
	Programs.Add(Chapter, MakeShared<leo::FLeoProgram>(std::move(Program)));
	UE_LOG(LogLeoRegistry, Log, TEXT("编译通过: %s（%d 命令, %d 表达式, %d label）"),
		*Chapter.ToString(),
		static_cast<int32>(Programs[Chapter]->Commands.size()),
		static_cast<int32>(Programs[Chapter]->ExprPool.size()),
		static_cast<int32>(Programs[Chapter]->LabelIndex.size()));
	return true;
}

bool ULeoScriptRegistry::TryGetProgram(FName Chapter, TSharedPtr<leo::FLeoProgram>& OutProgram) const
{
	const TSharedPtr<leo::FLeoProgram>* P = Programs.Find(Chapter);
	if (!P) { return false; }
	OutProgram = *P;
	return true;
}

bool ULeoScriptRegistry::HasChapter(FName Chapter) const
{
	return Programs.Contains(Chapter);
}

TArray<FName> ULeoScriptRegistry::GetChapterNames() const
{
	TArray<FName> Names;
	Programs.GetKeys(Names);
	return Names;
}
