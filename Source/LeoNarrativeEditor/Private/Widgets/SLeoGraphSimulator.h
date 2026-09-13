// 编排图干跑模拟器：沙箱黑板 + 单步/跑到底 + 每条出边条件真假显示。
// 图求值与运行时共用 LeoGraphEval（语义单源）；Chapter 节点停一步显示"将进入章节"。
#pragma once

#include "CoreMinimal.h"
#include "Blackboard/NarrativeBlackboard.h"
#include "Data/LeoGraphEval.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "SLeoGraphSimulator.generated.h"

class ULeoScenarioGraph;
class SEditableTextBox;

DECLARE_DELEGATE_RetVal(FName, FLeoSimGetSelectedNode);
DECLARE_DELEGATE_OneParam(FLeoSimCurrentNodeEvent, FName /*当前节点Id；NAME_None=不在本图/已完结*/);

// 沙箱黑板宿主（防 GC）
UCLASS()
class ULeoGraphSimContext : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> Global;
	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> Local;
};

class SLeoGraphSimulator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLeoGraphSimulator) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ULeoScenarioGraph>, Graph)
		SLATE_EVENT(FLeoSimGetSelectedNode, GetSelectedNodeId)
		SLATE_EVENT(FLeoSimCurrentNodeEvent, OnCurrentNodeChanged) // 干跑当前节点 → 画布高亮
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLeoGraphSimulator();

	// 图数据被外部改动后重置
	void NotifyGraphChanged();

private:
	struct FSimFrame
	{
		TWeakObjectPtr<ULeoScenarioGraph> Graph;
		FName NodeId;
	};

	void Reset();          // 回到入口（或编辑器选中节点）
	void Step();           // 单步
	void RunToEnd();       // 跑到底（步数上限保护）
	void RunNode(FName NodeId);         // 镜像子系统分派（纯图语义）
	void TransitionFromCurrent();       // 出边转移（含子图弹栈）
	void Finish(FName EndingId);
	void ApplyVarsToBoard();
	void RefreshViews();
	void NotifyCurrentNode(FName NodeId); // 画布高亮上报（子图内节点上报 NAME_None）

	FText BuildStatusText() const;
	FString DescribeNode(const FLeoScenarioNode& N) const;
	const FLeoScenarioNode* TopNode() const;

	TWeakObjectPtr<ULeoScenarioGraph> Graph;
	FLeoSimGetSelectedNode GetSelectedNodeId;
	FLeoSimCurrentNodeEvent OnCurrentNodeEvent;
	TStrongObjectPtr<ULeoGraphSimContext> SimCtx;
	LeoGraphEval::FExprCache ExprCache;

	// 模拟状态
	TArray<FSimFrame> Stack;
	bool bInsideChapter = false;         // Chapter 节点已进入，下一步做转移
	bool bDone = false;
	FName LastEnding;
	FString StatusExtra;

	// 每边求值结果（当前转移展示）
	TArray<TPair<FString, bool>> LastEdgeResults;

	// 变量编辑行
	struct FVarRow { FString Key; FString Expr; };
	TArray<TSharedRef<FVarRow>> VarRows;
	void BuildVarRows();  // 重建变量行编辑控件
	TSharedPtr<class SVerticalBox> VarBox;

	// 转移结果列表
	TArray<TSharedPtr<FString>> ResultRows;
	TSharedPtr<class SListView<TSharedPtr<FString>>> ResultList;
};
