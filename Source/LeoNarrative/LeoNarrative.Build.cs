// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LeoNarrative : ModuleRules
{
	public LeoNarrative(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UMG",             // ULeoDialogueWidget（纯 C++ Slate 构建）
				"LevelSequence",   // seq 命令：SequencerPerformer 播放 ULevelSequence
				"MovieScene",      // 播放器基类 UMovieSceneSequencePlayer/UMovieScene（显式链接，不依赖传递）
				"DeveloperSettings", // ULeoNarrativeSettings：Project Settings 页配置对象
				// ... add private dependencies that you statically link with here ...
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
