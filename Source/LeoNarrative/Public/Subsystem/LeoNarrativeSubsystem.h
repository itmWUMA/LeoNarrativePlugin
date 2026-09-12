// ULeoNarrativeSubsystem：会话管理门面（Facade）。
// 持有全局黑板 / 剧本注册表 / 活跃 VM；每帧驱动 VM；把 VM 事件转发给表现层订阅者。
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Blackboard/NarrativeBlackboard.h"
#include "ScriptRuntime/LeoScriptRegistry.h"
#include "VM/LeoEvents.h"
#include "VM/LeoVM.h"
#include "LeoNarrativeSubsystem.generated.h"

class ULeoAssetManifest;
class ULeoAudioAdapter;
class ULeoDialogueWidget;
class ULeoScenarioGraph;
class ULeoSequencerPerformer;
class ULeoStage;

// ---- 调试快照（编辑器调试器 Tab 只读消费；跨模块导出）----
struct LEONARRATIVE_API FLeoDebugVar
{
	FString Key;
	FString Value;
};

struct LEONARRATIVE_API FLeoDebugSnapshot
{
	bool bActive = false;          // 有活跃 VM
	FString StateName;             // WaitClick 等
	FString Chapter;
	FString CommandDesc;           // 当前命令一行摘要
	FString Anchor;                // "label+offset"
	FName SuspendToken;
	int32 PC = 0;
	int32 Line = 0;
	float WaitRemaining = 0.f;
	bool bAuto = false;
	bool bSkip = false;
	int32 ReadTextCount = 0;
	bool bGraphActive = false;
	FString GraphNode;
	TArray<FLeoDebugVar> LocalVars;
	TArray<FLeoDebugVar> GlobalVars;
	TArray<FString> EventLog;      // 最近事件（正序，上限 64）
	TArray<FString> CustomCommands;
};

UCLASS()
class LEONARRATIVE_API ULeoNarrativeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- 会话控制 ----
	bool StartChapter(FName Chapter);
	// 从指定 label+偏移恢复（读档路径）
	bool StartChapterAt(FName Chapter, FName Label, int32 Offset);
	void Stop();
	bool Advance();
	bool Choose(int32 Index);

	// ---- 播放模式（表现层读取；auto 定时推进 / skip 快进，choice 处都停下）----
	void SetAuto(bool bOn) { bAuto = bOn; }
	bool IsAuto() const { return bAuto; }
	void SetSkip(bool bOn) { bSkip = bOn; }
	bool IsSkip() const { return bSkip; }

	// ---- 访问 ----
	ULeoVM* GetActiveVM() const { return ActiveVM; }
	UNarrativeBlackboard* GetGlobalBlackboard() const { return GlobalBB; }
	ULeoScriptRegistry* GetRegistry() const { return Registry; }
	ULeoStage* GetStage() const { return Stage; }
	ULeoAudioAdapter* GetAudio() const { return Audio; }
	ULeoSequencerPerformer* GetSequencer() const { return Sequencer; }

	// 跳过当前过场：停掉所有序列播放，若阻塞在 seq 断点则以 0 恢复（脚本走兜底分支）
	void SkipSequences();

	// 逻辑名清单（可选；未设置时表现层降级为占位/静音）
	void SetManifest(ULeoAssetManifest* InManifest);
	ULeoAssetManifest* GetManifest() const { return Manifest; }

	// 对话 UI 开关（纯 C++ Slate，无需编辑器资产）
	void ShowDialogueUI(bool bShow);

	// ---- ScenarioGraph 编排 ----
	// 从图的节点跑章节；章末按出边条件（优先级降序）选下一节点，无路可走 = 图完结
	void StartGraph(ULeoScenarioGraph* Graph, FName StartNode = NAME_None);
	bool IsGraphActive() const { return ActiveGraph != nullptr; }
	FName GetCurrentGraphNode() const { return CurrentGraphNodeId; }

	// ---- 自定义命令与玩法断点 ----
	// 严格注册：编译期按 spec 校验参数（编辑期报错带行号）
	void RegisterCommand(FName Name, const LeoBridge::FLeoCmdSpec& Spec, ULeoVM::FCustomHandler Handler);
	// 宽松注册（只登记名字，参数不校验）
	void RegisterCommandHandler(FName Name, ULeoVM::FCustomHandler Handler);
	// 玩法断点恢复：payload 写局部黑板键 <token>，脚本 jumpif 分流
	bool ResumeWith(FName Token, const leo::FLeoValue& Payload);

	// ---- 双档体系 ----
	bool SaveGlobal();             // 全局档：已读文本 ID + 全局黑板
	bool LoadGlobal();
	bool SaveProgress();           // 进度档：图位置 + VM 锚点 + 局部黑板快照
	bool LoadProgressAndResume();  // 全局档 + 进度档一并恢复并续跑

	// 重新扫描编译剧本（编辑器热重载）
	bool ReloadScripts();

	// ---- 调试支持 ----
	void GetDebugSnapshot(FLeoDebugSnapshot& Out) const; // 只读快照（调试器 Tab 轮询）
	const TArray<FString>& GetEventLog() const { return EventLog; }
	static const TCHAR* GlobalSlotName()   { return TEXT("LeoNarrative/Global"); }
	static const TCHAR* ProgressSlotName() { return TEXT("LeoNarrative/Progress"); }

	// 表现层订阅入口（UI/Stage/Audio 全部从这里拿事件）
	FLeoEventSignature OnLeoEvent;

	// 已读文本 ID（进全局档）
	const TSet<FString>& GetReadTextIds() const { return ReadTextIds; }

private:
	bool StartChapterInternal(FName Chapter, FName Label, int32 Offset);
	void HandleVMEvent(const FLeoEvent& Ev);
	bool TickVM(float DeltaSeconds);
	void RunGraphNode(FName NodeId);
	void AdvanceGraph();
	bool EvalEdgeCondition(const FString& Condition);
	void RefreshCommandRegistry(); // 把 VM 静态命令表同步给编译注册表

	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> GlobalBB;
	UPROPERTY()
	TObjectPtr<ULeoScriptRegistry> Registry;
	UPROPERTY()
	TObjectPtr<ULeoVM> ActiveVM;
	UPROPERTY()
	TObjectPtr<ULeoStage> Stage;
	UPROPERTY()
	TObjectPtr<ULeoAudioAdapter> Audio;
	UPROPERTY()
	TObjectPtr<ULeoSequencerPerformer> Sequencer;
	UPROPERTY()
	TObjectPtr<ULeoDialogueWidget> DialogueWidget;
	UPROPERTY()
	TObjectPtr<ULeoAssetManifest> Manifest;

	UPROPERTY()
	TObjectPtr<ULeoScenarioGraph> ActiveGraph;
	FName CurrentGraphNodeId;
	TMap<FString, leo::FLeoExprPtr> EdgeExprCache; // 图边条件编译缓存（非反射）
	bool bGraphAdvancePending = false;

	FTSTicker::FDelegateHandle TickerHandle;
	bool bAuto = false;
	bool bSkip = false;
	TSet<FString> ReadTextIds;
	TArray<FString> RegisteredCommandNames; // 已注册的自定义命令名（编译前应用）
	TArray<FString> EventLog;               // 调试器事件环形缓冲（上限 64）
	void AppendEventLog(const FLeoEvent& Ev);
	static constexpr int32 EventLogCapacity = 64;
};
