// Copyright Epic Games, Inc. All Rights Reserved.

#include "LeoNarrative.h"

#include "Examples/LeoInvestigationDemo.h"
#include "Stage/LeoSequencerPerformer.h"

#define LOCTEXT_NAMESPACE "FLeoNarrativeModule"

void FLeoNarrativeModule::StartupModule()
{
	// 框架预注册演出命令：seq（Level Sequencer 过场，须早于任何剧本编译）
	ULeoSequencerPerformer::RegisterSeqCommand();
	// 扩展示例：注册 investigate 自定义命令
	LeoExamples::RegisterInvestigationDemo();
}

void FLeoNarrativeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLeoNarrativeModule, LeoNarrative)