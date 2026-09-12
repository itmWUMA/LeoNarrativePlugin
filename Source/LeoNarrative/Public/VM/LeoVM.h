// LeoVM：.leo 命令解释器。载入时编译的 FLeoProgram 在此逐条执行；
// 阻塞点：text(等点击) / wait(等计时) / choice(等选择)。
// 存档锚点 = label + 偏移，对文本插行鲁棒（spec §8 同源设计）。
#pragma once

#include "CoreMinimal.h"
#include "Blackboard/NarrativeBlackboard.h"
#include "Script/LeoProgram.h"
#include "VM/LeoEvents.h"
#include "LeoVM.generated.h"

namespace ELeoVMState
{
	enum Type : uint8_t { Idle, Running, WaitClick, WaitTimer, WaitChoice, Finished };
}

UCLASS()
class LEONARRATIVE_API ULeoVM : public UObject
{
	GENERATED_BODY()

public:
	// 自定义命令处理器（Command 模式的扩展点：命令是数据，行为注册进表）
	using FCustomHandler = TFunction<void(ULeoVM&, const leo::FLeoCommand&)>;

	void Init(FName InChapter, const TSharedPtr<leo::FLeoProgram>& InProgram,
	          UNarrativeBlackboard* InLocal, UNarrativeBlackboard* InGlobal);

	// 推进执行；在阻塞命令处返回。由子系统每帧驱动。
	void Tick(float DeltaSeconds);

	// WaitClick → 恢复运行（玩家点击/auto/skip）
	bool Advance();
	// WaitChoice → 记录选择到黑板 last_choice，跳转到选项目标
	bool Choose(int32 Index);

	ELeoVMState::Type GetState() const { return State; }
	FName GetChapter() const { return Chapter; }
	int32 GetPC() const { return PC; }
	UNarrativeBlackboard* GetLocalBlackboard() const { return LocalBB; }
	UNarrativeBlackboard* GetGlobalBlackboard() const { return GlobalBB; }

	// 状态锚点（存档/恢复）
	bool GetAnchor(FName& OutLabel, int32& OutOffset) const;
	bool RestoreAnchor(FName Label, int32 Offset);

	// 全局自定义命令注册（进程级；须在编译剧本前注册名字）
	static void RegisterCustomHandler(FName Name, FCustomHandler Handler);

	// 事件出口（子系统转发给表现层）
	FLeoEventSignature OnEvent;

private:
	enum class EResult : uint8_t { Next, Jumped, Block, Halt };
	using FHandler = EResult (ULeoVM::*)(const leo::FLeoCommand&);
	static TMap<leo::ELeoCmd, FHandler>& HandlerTable();
	static TMap<FName, FCustomHandler>& CustomTable();

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
