#include "LeoValidateCommandlet.h"

#include "LeoValidation.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoValidateCmd, Log, All);

int32 ULeoValidateCommandlet::Main(const FString& Params)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true; // -stdout 时直接打到控制台

	// -selftest-graph：仅跑图核对自测（内存构造正常/破损图），用于 CI
	if (FParse::Param(*Params, TEXT("selftest-graph")))
	{
		const int32 SelfFailures = LeoValidation::SelfTestGraphChecks();
		UE_LOG(LogLeoValidateCmd, Display, TEXT("=== LeoValidate selftest-graph: %s ==="),
			SelfFailures == 0 ? TEXT("通过") : TEXT("失败"));
		return SelfFailures > 0 ? 1 : 0;
	}

	UE_LOG(LogLeoValidateCmd, Display, TEXT("=== LeoValidate: .leo 剧本校验开始 ==="));
	// 可选 -manifest=<资产路径> 指定核对用清单；缺省自动发现工程内 ULeoAssetManifest
	FString ManifestPath;
	FParse::Value(*Params, TEXT("manifest="), ManifestPath);
	const int32 Failures = LeoValidation::ValidateAll(ManifestPath);
	UE_LOG(LogLeoValidateCmd, Display, TEXT("=== LeoValidate 结束: %s (%d 项失败) ==="),
		Failures == 0 ? TEXT("全绿") : TEXT("有失败"), Failures);
	return Failures > 0 ? 1 : 0;
}
