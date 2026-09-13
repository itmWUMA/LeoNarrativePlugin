// VM → 表现层的事件模型（VM 只广播事件，不触碰 Widget/资产——铁律 #3）
#pragma once

#include "CoreMinimal.h"

enum class ELeoEventKind : uint8_t
{
	ChapterStart,
	Text,        // 一条对话（阻塞等点击）
	Bg,
	Char,
	Bgm,
	Se,
	Voice,
	ChoiceShown, // 选项列表（阻塞等选择）
	ChoiceMade,  // 玩家已选择
	ChapterEnd,
	RuntimeError,
	Custom,      // 自定义命令/玩法事件：CustomName + ExtraParams 参数袋
};

struct FLeoEventOption
{
	FString Text;
	FString TextId;       // 本地化/已读锚点（显式 id= 或自动 章节/label/c序号/选项号）
	FString TargetLabel;
};

struct FLeoEvent
{
	ELeoEventKind Kind = ELeoEventKind::ChapterStart;
	int32 Line = 0;
	FName Chapter;

	// Text
	FString Speaker;   // 空 = 旁白
	FString Text;
	FString TextId;    // 章节/label/序号，本地化与已读跟踪锚点

	// 演出（bg/char/bgm/se/voice）
	FName AssetId;     // 逻辑名；char/bgm 的 "-" 表示移除/停止
	FString Slot;      // char 槽位
	FString At, Pose, Motion, Transition;
	float Duration = 0.5f; // 转场/淡入秒数
	float Volume = 1.f;
	float Fade = 0.f;

	// choice
	TArray<FLeoEventOption> Options;
	int32 ChoiceIndex = -1;

	// RuntimeError
	FString DiagCode, DiagMsg;

	// Custom（自定义命令/玩法事件）：命令名 + 通用参数袋。
	// 新演出/玩法维度只往 ExtraParams 塞数据，事件结构体保持冻结。
	FName CustomName;
	TMap<FName, FString> ExtraParams;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FLeoEventSignature, const FLeoEvent&);
