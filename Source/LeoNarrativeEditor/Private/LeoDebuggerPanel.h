// 叙事调试器（Nomad Tab）：PIE 运行中的 VM 状态 / 双黑板 / 事件流 / 手动驱动 / 存档查看。
// 数据源 = ULeoNarrativeSubsystem::GetDebugSnapshot（只读）；驱动动作直接调用子系统 API。
#pragma once

#include "CoreMinimal.h"

namespace LeoDebuggerPanel
{
	void RegisterTab();
	void UnregisterTab();
}
