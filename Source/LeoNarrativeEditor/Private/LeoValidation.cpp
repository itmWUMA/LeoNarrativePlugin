#include "LeoValidation.h"

#include "ScriptRuntime/LeoScriptBridge.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoValidate, Log, All);

namespace LeoValidation
{

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

	bool CompileFile(const FString& Path, const FString& Source, const FString& Name, leo::FLeoProgram& OutProgram, bool bDiagAsError)
	{
		OutProgram = LeoBridge::CompileChapter(Source, Name);
		for (const leo::FLeoDiag& D : OutProgram.Diags)
		{
			const FString Msg = FString::Printf(TEXT("%s(%d): %s  %s"), *FPaths::GetCleanFilename(Path), D.Line,
				LeoBridge::DiagName(D.Code), *LeoBridge::ToFString(D.Msg));
			// 错误语料（bDiagAsError=false）的诊断是预期产物，用 Warning 级：
			// 引擎会在 Main 返回 0 但全局错误计数 >0 时强制退出码 1（LaunchEngineLoop.cpp:4220）
			if (bDiagAsError && leo::IsLeoError(D.Code))
			{
				UE_LOG(LogLeoValidate, Error, TEXT("%s"), *Msg);
			}
			else
			{
				UE_LOG(LogLeoValidate, Warning, TEXT("%s"), *Msg);
			}
		}
		return OutProgram.Ok;
	}
} // namespace

bool ValidateFile(const FString& Path, bool bExpectClean)
{
	std::string Source;
	if (!LoadUtf8File(Path, Source))
	{
		UE_LOG(LogLeoValidate, Error, TEXT("%s: 读取失败"), *Path);
		return false;
	}
	const FString Name = FPaths::GetBaseFilename(Path);
	leo::FLeoProgram Program;
	const bool bClean = CompileFile(Path, LeoBridge::ToFString(Source), Name, Program, /*bDiagAsError=*/bExpectClean);

	if (bExpectClean && !bClean)
	{
		UE_LOG(LogLeoValidate, Error, TEXT("[FAIL] %s  应当零错误"), *Path);
		return false;
	}
	if (!bExpectClean && bClean)
	{
		UE_LOG(LogLeoValidate, Error, TEXT("[FAIL] %s  这是错误语料，却编译通过了（错误检测回归！）"), *Path);
		return false;
	}
	UE_LOG(LogLeoValidate, Display, TEXT("[PASS] %s"), *Path);
	return true;
}

int32 ValidateAll()
{
	int32 Failures = 0;
	int32 Total = 0;

	auto ValidateDir = [&](const FString& Dir, bool bExpectClean)
	{
		if (!FPaths::DirectoryExists(Dir)) { return; }
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.leo")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			++Total;
			if (!ValidateFile(Dir / File, bExpectClean)) { ++Failures; }
		}
	};

	// 宿主工程剧本
	ValidateDir(FPaths::ProjectContentDir() / TEXT("Scripts"), true);

	// 插件 golden 语料（pass 零错误 / fail 必须报错）
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LeoNarrative"));
	if (Plugin.IsValid())
	{
		const FString GoldenRoot = Plugin->GetBaseDir() / TEXT("tests/golden");
		ValidateDir(GoldenRoot / TEXT("pass"), true);
		ValidateDir(GoldenRoot / TEXT("fail"), false);
	}

	UE_LOG(LogLeoValidate, Display, TEXT("LeoValidate 完成: %d 个文件, %d 个不符合预期"), Total, Failures);
	return Failures;
}

} // namespace LeoValidation
