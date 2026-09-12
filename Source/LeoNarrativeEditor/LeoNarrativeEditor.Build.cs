// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LeoNarrativeEditor : ModuleRules
{
	public LeoNarrativeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
			// 编辑期工具直接使用运行时模块里的编译器内核与数据类型
			"LeoNarrative",
			"UnrealEd",
			"Projects",        // IPluginManager：定位插件目录下的 golden 语料
			"DirectoryWatcher",// M5: Content/Scripts 热校验
			"ToolMenus",       // M5: 编辑器菜单入口
		}
		);
	}
}
