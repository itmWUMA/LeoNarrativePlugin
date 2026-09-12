// ScriptRegistry：剧本 ID(FName) → 编译产物 的唯一入口（铁律 #2）
// v1 纯文本形态：扫描 Content/Scripts/*.leo，载入即编译，产物缓存不落盘
#pragma once

#include "CoreMinimal.h"
#include "Script/LeoProgram.h"
#include "LeoScriptRegistry.generated.h"

UCLASS()
class LEONARRATIVE_API ULeoScriptRegistry : public UObject
{
	GENERATED_BODY()

public:
	// 扫描并编译全部剧本（重复调用 = 全量重载）。返回成功的章节数。
	int32 LoadAndCompileAll();
	// 单章重编译（编辑器热校验/热重载用）
	bool RecompileChapter(FName Chapter);

	bool TryGetProgram(FName Chapter, TSharedPtr<leo::FLeoProgram>& OutProgram) const;
	bool HasChapter(FName Chapter) const;
	TArray<FName> GetChapterNames() const;

	// 最近一次编译的诊断（章节名 → 诊断列表），调试用
	const TMap<FName, TArray<FString>>& GetLastDiags() const { return LastDiags; }

	// 编译前需要应用的自定义命令名（由子系统设置）
	TArray<FString> CustomCommandNames;

private:
	bool CompileOne(const FString& FilePath);

	TMap<FName, TSharedPtr<leo::FLeoProgram>> Programs; // 纯 C++ 产物，不走反射
	TMap<FName, TArray<FString>> LastDiags;
};
