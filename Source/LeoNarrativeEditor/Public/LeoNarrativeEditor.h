// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/**
 * LeoNarrative 的编辑器模块。
 * 承载 .leo 导入校验、剧本编辑辅助、ScenarioGraph 图编辑器与运行时调试器等编辑期功能。
 * 依赖方向单向：本模块依赖 LeoNarrative（Runtime），Runtime 严禁反向依赖本模块。
 */
class FLeoNarrativeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
