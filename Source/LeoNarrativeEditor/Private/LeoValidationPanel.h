// 校验中心面板（Nomad Tab）：剧本文件列表 × 诊断明细 × 资产引用核对。
// 数据源 = LeoValidation::ValidateAllStructured；watcher 热校验后自动刷新。
#pragma once

#include "CoreMinimal.h"

// 面板注册/反注册（模块启停调用）
namespace LeoValidationPanel
{
	void RegisterTab();
	void UnregisterTab();
}
