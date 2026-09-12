// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeoNarrative.h"

#include "Examples/LeoInvestigationDemo.h"

#define LOCTEXT_NAMESPACE "FLeoNarrativeModule"

void FLeoNarrativeModule::StartupModule()
{
	// 扩展示例：注册 investigate 自定义命令（须早于任何剧本编译）
	LeoExamples::RegisterInvestigationDemo();
}

void FLeoNarrativeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLeoNarrativeModule, LeoNarrative)