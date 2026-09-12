#include "ScriptRuntime/LeoScriptBridge.h"

namespace LeoBridge
{

std::string ToUtf8(const FString& Str)
{
	const FTCHARToUTF8 Conv(*Str);
	return std::string(Conv.Get(), Conv.Length());
}

FString ToFString(const std::string& Utf8)
{
	const FUTF8ToTCHAR Conv(Utf8.c_str());
	return FString(Conv.Length(), Conv.Get());
}

leo::FLeoProgram CompileChapter(const FString& SourceUtf8, const FString& SourceName)
{
	return leo::CompileChapter(ToUtf8(SourceUtf8), ToUtf8(SourceName));
}

leo::FLeoExprPtr CompileExpr(const FString& ExprSrc, leo::FLeoDiag& OutDiag)
{
	return leo::CompileExprSrc(ToUtf8(ExprSrc), OutDiag);
}

const TCHAR* DiagName(leo::ELeoDiag Code)
{
	// UTF8_TO_TCHAR 返回临时转换器，不能直接外传指针——拷贝到静态缓冲
	static TCHAR Buf[32];
	const FUTF8ToTCHAR Conv(leo::LeoDiagName(Code));
	FCString::Strncpy(Buf, Conv.Get(), UE_ARRAY_COUNT(Buf) - 1);
	return Buf;
}

void SetCustomCommandNames(const TArray<FString>& Names)
{
	std::vector<std::string> StdNames;
	StdNames.reserve(Names.Num());
	for (const FString& N : Names)
	{
		StdNames.push_back(ToUtf8(N));
	}
	leo::SetCustomCommandNames(StdNames);
}

void SetCustomCommandSpecs(const TArray<FLeoCmdSpec>& Specs)
{
	std::vector<leo::FLeoCommandSpec> StdSpecs;
	StdSpecs.reserve(Specs.Num());
	for (const FLeoCmdSpec& S : Specs)
	{
		leo::FLeoCommandSpec Std;
		Std.Name = ToUtf8(S.Name);
		Std.MinArgs = S.MinArgs;
		Std.MaxArgs = S.MaxArgs;
		Std.AllowedParams.reserve(S.AllowedParams.Num());
		for (const FString& P : S.AllowedParams)
		{
			Std.AllowedParams.push_back(ToUtf8(P));
		}
		StdSpecs.push_back(std::move(Std));
	}
	leo::SetCustomCommandSpecs(StdSpecs);
}

} // namespace LeoBridge
