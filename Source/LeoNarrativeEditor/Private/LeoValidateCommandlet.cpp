#include "LeoValidateCommandlet.h"

#include "LeoValidation.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoValidateCmd, Log, All);

int32 ULeoValidateCommandlet::Main(const FString& Params)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true; // -stdout 时直接打到控制台

	UE_LOG(LogLeoValidateCmd, Display, TEXT("=== LeoValidate: .leo 剧本校验开始 ==="));
	const int32 Failures = LeoValidation::ValidateAll();
	UE_LOG(LogLeoValidateCmd, Display, TEXT("=== LeoValidate 结束: %s (%d 项失败) ==="),
		Failures == 0 ? TEXT("全绿") : TEXT("有失败"), Failures);
	return Failures > 0 ? 1 : 0;
}
