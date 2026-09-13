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
			"AssetRegistry",   // P0: 清单资产发现与存在性核对
			"LevelSequence",   // P0: seq 逻辑名 → ULevelSequence 类型核对
			"WorkspaceMenuStructure", // P0/P1: Nomad Tab 分组（Tools）
			"PropertyEditor",   // T2: 图编辑器 Details 面板
			"AssetDefinition",  // T2: UAssetDefinition（Gameplay→Narrative 子菜单入口）
			"GraphEditor",      // T2: SGraphEditor/SGraphNode/FConnectionDrawingPolicy（画布 1:1 复刻 BT 卡片 + 状态机连线）
			"EditorFramework",  // T2: FGenericCommands（图面板 Delete 命令）
		}
		);
	}
}
