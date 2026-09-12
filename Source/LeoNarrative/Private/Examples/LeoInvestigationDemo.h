// 调查模式扩展示例的注册入口
#pragma once

namespace LeoExamples
{
	// 注册 investigate 自定义命令（严格 spec + 外部断点处理器）。
	// 在 FLeoNarrativeModule::StartupModule 调用——须早于任何剧本编译。
	void RegisterInvestigationDemo();
}
