// VM → 表现层的事件模型（VM 只广播事件，不触碰 Widget/资产——铁律 #3）
// 结构体已反射化（USTRUCT/UENUM）：蓝图经子系统 OnLeoEventBP 订阅同一事件流；
// 新演出/玩法维度仍只往 ExtraParams 塞数据，事件结构体保持冻结。
#pragma once

#include "CoreMinimal.h"
#include "LeoEvents.generated.h"

UENUM()
enum class ELeoEventKind : uint8
{
	ChapterStart UMETA(DisplayName = "章节开始"),
	Text        UMETA(DisplayName = "对话文本"),   // 一条对话（阻塞等点击）
	Bg          UMETA(DisplayName = "背景"),
	Char        UMETA(DisplayName = "立绘"),
	Bgm         UMETA(DisplayName = "BGM"),
	Se          UMETA(DisplayName = "音效"),
	Voice       UMETA(DisplayName = "语音"),
	ChoiceShown UMETA(DisplayName = "选项出现"),   // 选项列表（阻塞等选择）
	ChoiceMade  UMETA(DisplayName = "选择完成"),   // 玩家已选择
	ChapterEnd  UMETA(DisplayName = "章节结束"),
	RuntimeError UMETA(DisplayName = "运行时错误"),
	Custom      UMETA(DisplayName = "自定义"),     // 自定义命令/玩法事件：CustomName + ExtraParams 参数袋
};

USTRUCT()
struct FLeoEventOption
{
	GENERATED_BODY()

	UPROPERTY() FString Text;
	UPROPERTY() FString TextId;       // 本地化/已读锚点（显式 id= 或自动 章节/label/c序号/选项号）
	UPROPERTY() FString TargetLabel;
};

USTRUCT()
struct FLeoEvent
{
	GENERATED_BODY()

	UPROPERTY() ELeoEventKind Kind = ELeoEventKind::ChapterStart;
	UPROPERTY() int32 Line = 0;
	UPROPERTY() FName Chapter;

	// Text
	UPROPERTY() FString Speaker;   // 空 = 旁白
	UPROPERTY() FString Text;
	UPROPERTY() FString TextId;    // 章节/label/序号，本地化与已读跟踪锚点

	// 演出（bg/char/bgm/se/voice）
	UPROPERTY() FName AssetId;     // 逻辑名；char/bgm 的 "-" 表示移除/停止
	UPROPERTY() FString Slot;      // char 槽位
	UPROPERTY() FString At;
	UPROPERTY() FString Pose;
	UPROPERTY() FString Motion;
	UPROPERTY() FString Transition;
	UPROPERTY() float Duration = 0.5f; // 转场/淡入秒数
	UPROPERTY() float Volume = 1.f;
	UPROPERTY() float Fade = 0.f;

	// choice
	UPROPERTY() TArray<FLeoEventOption> Options;
	UPROPERTY() int32 ChoiceIndex = -1;

	// RuntimeError
	UPROPERTY() FString DiagCode;
	UPROPERTY() FString DiagMsg;

	// Custom（自定义命令/玩法事件）：命令名 + 通用参数袋。
	// 新演出/玩法维度只往 ExtraParams 塞数据，事件结构体保持冻结。
	UPROPERTY() FName CustomName;
	UPROPERTY() TMap<FName, FString> ExtraParams;
};

// C++ 原生订阅者（内置 UI/Stage/Audio）走这条快速通道
DECLARE_MULTICAST_DELEGATE_OneParam(FLeoEventSignature, const FLeoEvent&);

// 蓝图订阅通道（子系统 OnLeoEventBP；与原生委托同流广播）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLeoEventSignatureBP, const FLeoEvent&, Ev);
