// LeoVM：.leo 命令解释器。载入时编译的 FLeoProgram 在此逐条执行；
// 阻塞点：text(等点击) / wait(等计时) / choice(等选择)。
// 存档锚点 = label + 偏移，对文本插行鲁棒（spec §8 同源设计）。
#pragma once

#include "CoreMinimal.h"
#include "Blackboard/NarrativeBlackboard.h"
#include "Script/LeoProgram.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "VM/LeoEvents.h"
#include "LeoVM.generated.h"

namespace ELeoVMState
{
	// WaitClick/WaitTimer/WaitChoice 是三个内置断点的语法糖；
	// WaitExternal = 任意注册玩法断点（Suspend/ResumeWith），ADV 非 VN 玩法段的挂起点
	enum Type : uint8_t { Idle, Running, WaitClick, WaitTimer, WaitChoice, WaitExternal, Finished };
}

// 自定义命令处理结果：瞬时完成 / 挂起为外部断点 / 终止章节
enum class ELeoCustomResult : uint8_t { Next, Suspend, Halt };

UCLASS()
class LEONARRATIVE_API ULeoVM : public UObject
{
	GENERATED_BODY()

public:
	// 自定义命令处理器（Command 模式的扩展点：命令是数据，行为注册进表）
	// 返回 Next=瞬时完成；Suspend=已调用 Suspend() 挂起等玩法结果；Halt=终止章节
	using FCustomHandler = TFunction<ELeoCustomResult(ULeoVM&, const leo::FLeoCommand&)>;

	void Init(FName InChapter, const TSharedPtr<leo::FLeoProgram>& InProgram,
	          UNarrativeBlackboard* InLocal, UNarrativeBlackboard* InGlobal);

	// 推进执行；在阻塞命令处返回。由子系统每帧驱动。
	void Tick(float DeltaSeconds);

	// WaitClick → 恢复运行（玩家点击/auto/skip）
	bool Advance();
	// WaitChoice → 记录选择到黑板 last_choice，跳转到选项目标
	bool Choose(int32 Index);

	// ---- 外部断点（玩法段挂起/恢复）----
	// 处理器内调用：把 VM 挂起为命名断点（token 须为标识符）
	bool Suspend(FName Token);
	// 玩法层完成时经子系统调用：payload 写入局部黑板键 <token>，脚本用 jumpif 分流；
	// 与 choice→last_choice 同一模式——VM 指针不被外部驱动
	bool ResumeWith(FName Token, const leo::FLeoValue& Payload);
	FName GetSuspendToken() const { return SuspendToken; }

	// 广播 Custom 事件（CustomName + ExtraParams 自动从命令参数袋填充）
	void EmitCustomEvent(const leo::FLeoCommand& C);

	ELeoVMState::Type GetState() const { return State; }
	FName GetChapter() const { return Chapter; }
	int32 GetPC() const { return PC; }
	UNarrativeBlackboard* GetLocalBlackboard() const { return LocalBB; }
	UNarrativeBlackboard* GetGlobalBlackboard() const { return GlobalBB; }

	// ---- 调试支持（编辑器调试器 Tab 只读消费）----
	static const TCHAR* StateName(ELeoVMState::Type S);   // "WaitClick" 等展示名
	int32 GetCurrentLine() const;                          // 当前命令源行号
	float GetWaitRemaining() const { return WaitRemaining; }
	FString DescribeCurrentCommand() const;                // 当前命令一行摘要

	// 状态锚点（存档/恢复）
	bool GetAnchor(FName& OutLabel, int32& OutOffset) const;
	bool RestoreAnchor(FName Label, int32 Offset);

	// 全局自定义命令注册（进程级；命令名须在编译剧本前注册）
	// 严格模式：带编译期参数校验（spec）；宽松模式：只注册名字
	static void RegisterCustomCommand(FName Name, const LeoBridge::FLeoCmdSpec& Spec, FCustomHandler Handler);
	static void RegisterCustomHandler(FName Name, FCustomHandler Handler);
	// 供注册表在编译前取用（返回 FString 便于注册表直接消费）
	static TArray<FString> GetLenientCommandNames();
	static TArray<LeoBridge::FLeoCmdSpec> GetStrictCommandSpecs();

	// 事件出口（子系统转发给表现层）
	FLeoEventSignature OnEvent;

private:
	enum class EResult : uint8_t { Next, Jumped, Block, Halt };
	using FHandler = EResult (ULeoVM::*)(const leo::FLeoCommand&);
	static TMap<leo::ELeoCmd, FHandler>& HandlerTable();

	struct FLeoCustomEntry
	{
		bool bStrict = false;
		LeoBridge::FLeoCmdSpec Spec;
		FCustomHandler Handler;
	};
	static TMap<FName, FLeoCustomEntry>& CustomCommands();

	EResult HandleNop(const leo::FLeoCommand& C);
	EResult HandleText(const leo::FLeoCommand& C);
	EResult HandleBg(const leo::FLeoCommand& C);
	EResult HandleChar(const leo::FLeoCommand& C);
	EResult HandleBgm(const leo::FLeoCommand& C);
	EResult HandleSe(const leo::FLeoCommand& C);
	EResult HandleVoice(const leo::FLeoCommand& C);
	EResult HandleWait(const leo::FLeoCommand& C);
	EResult HandleJump(const leo::FLeoCommand& C);
	EResult HandleJumpIf(const leo::FLeoCommand& C);
	EResult HandleSet(const leo::FLeoCommand& C);
	EResult HandleChoice(const leo::FLeoCommand& C);
	EResult HandleEnd(const leo::FLeoCommand& C);
	EResult HandleCustom(const leo::FLeoCommand& C);

	bool EvalExpr(int32 ExprIndex, leo::FLeoValue& Out, leo::ELeoDiag& OutCode, std::string& OutMsg);
	bool ApplySetOp(UNarrativeBlackboard* Scope, const leo::FLeoCommand& C);
	void Broadcast(FLeoEvent&& Ev);
	void RuntimeError(leo::ELeoDiag Code, int32 Line, const std::string& Msg);
	// 最近 label 与 label 内 text 序号（TextId 用）
	void ComputeTextAnchor(int32 TextPC, FName& OutLabel, int32& OutSeq) const;

	TSharedPtr<leo::FLeoProgram> Program;
	FName Chapter;
	int32 PC = 0;
	ELeoVMState::Type State = ELeoVMState::Idle;
	float WaitRemaining = 0.f;
	// 当前挂起断点标识：内置 "click"/"timer"/"choice"，外部为注册的玩法 token
	FName SuspendToken;
	// 阻塞恢复语义：PC 停在阻塞命令上（存档锚点重放该命令），
	// 恢复运行时需跳过它一次，避免重复执行
	bool bNeedSkipCurrent = false;
	// 条件过滤后的可见选项（Choose 索引针对此列表）
	TArray<leo::FLeoOption> ActiveOptions;

	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> LocalBB;
	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> GlobalBB;
};
