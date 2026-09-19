// ULeoNarrativeSettings：框架全局配置（Project Settings → Game | LeoNarrative）。
// 目标：换宿主项目零代码改动——目录、档名、内置表现层开关、UI 类注入、清单引用集中于此。
// 只改“从哪里读文件/内置实现怎么建”，不承载任何内容语义（文本仍是唯一事实源）。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LeoNarrativeSettings.generated.h"

class ULeoAssetManifest;
class ULeoDialogueWidget;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Leo Narrative（叙事框架）"))
class LEONARRATIVE_API ULeoNarrativeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ---- 文件通道（相对 Content 根；v1 纯文本发布形态）----
	// 改剧本目录需同步调整打包的 +DirectoriesToAlwaysStageAsUFS
	UPROPERTY(Config, EditAnywhere, Category = "Paths", meta = (ToolTip = "剧本目录名（相对 Content 根）"))
	FString ScriptsDirName = TEXT("Scripts");

	UPROPERTY(Config, EditAnywhere, Category = "Paths", meta = (ToolTip = "译文目录名（相对 Content 根），结构 <目录>/<culture>/<章节>.csv"))
	FString L10nDirName = TEXT("L10n");

	// 绝对路径（Content 根 + 目录名）；运行时注册表与编辑器 watcher/校验共用同一取值
	FString GetScriptsDirPath() const;
	FString GetL10nDirPath() const;

	// ---- 存档槽 ----
	UPROPERTY(Config, EditAnywhere, Category = "Save")
	FString GlobalSlotName = TEXT("LeoNarrative/Global");

	UPROPERTY(Config, EditAnywhere, Category = "Save")
	FString ProgressSlotName = TEXT("LeoNarrative/Progress");

	// ---- 资产清单 ----
	// 项目默认清单；空 = 无清单（表现层占位/静音），运行时 SetManifest 仍可覆盖
	UPROPERTY(Config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<ULeoAssetManifest> DefaultManifest;

	// ---- 内置表现层（事件订阅者；整体关闭后框架只广播事件，演出由项目自建——铁律 #3 的接管面）----
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ToolTip = "内置舞台（背景/立绘状态）。关闭后 bg/char 事件仅广播"))
	bool bCreateBuiltinStage = true;

	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ToolTip = "内置音频（BGM/SE/Voice）。关闭后音频事件仅广播"))
	bool bCreateBuiltinAudio = true;

	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ToolTip = "内置对话 UI。关闭后由项目自建 UI 订阅 OnLeoEventBP 驱动 Advance/Choose"))
	bool bCreateBuiltinDialogueUI = true;

	// 对话 UI 类注入：空 = 内置纯 C++ ULeoDialogueWidget；填子类换皮（C++/BP 子类均可）
	UPROPERTY(Config, EditAnywhere, Category = "Presentation")
	TSoftClassPtr<ULeoDialogueWidget> DialogueWidgetClass;

	// ---- 播放节奏（内置 UI 的 auto/skip 推进间隔，秒）----
	UPROPERTY(Config, EditAnywhere, Category = "Playback", meta = (ClampMin = "0.01"))
	float AutoAdvanceDelay = 2.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Playback", meta = (ClampMin = "0.0"))
	float SkipAdvanceDelay = 0.05f;

	static const ULeoNarrativeSettings* Get() { return GetDefault<ULeoNarrativeSettings>(); }
};
