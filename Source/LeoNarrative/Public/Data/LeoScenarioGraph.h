// ScenarioGraph：章节编排图（数据先行，轻量专属编辑器 T2）。
// 节点类型化：Chapter（跑章节）/ Branch（纯条件分流）/ Ending（结局终态）/ Subgraph（子图复用）；
// 边 = 条件（.leo 表达式语法）+ 优先级 + 转移副作用（set/setg 语义的黑板赋值）。
// 玩家流程图 UI 未来从同一份数据渲染。
#pragma once

#include "Engine/DataAsset.h"
#include "LeoScenarioGraph.generated.h"

UENUM()
enum class ELeoScenarioNodeType : uint8
{
	Chapter,   // 进节点 = 跑 Chapter 章（可选 Label 入口），章末走出边
	Branch,    // 不跑章节：立即按出边条件分流（jumpif 链的图形态）
	Ending,    // 终态：报告 EndingId（解锁/成就/鉴赏的数据源）；子图内 = 子图收束
	Subgraph,  // 进节点 = 跑 SubGraph 子图，子图收束后回到本节点继续走出边
};

// 转移副作用：转移发生时对黑板的一次赋值（等价 set/setg）
USTRUCT()
struct FLeoEdgeAction
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Key;                      // 黑板键
	UPROPERTY(EditAnywhere)
	bool bGlobal = false;           // true = setg（写全局层）
	UPROPERTY(EditAnywhere)
	FString Op = TEXT("=");         // = += -= *= /=
	UPROPERTY(EditAnywhere)
	FString Expr;                   // 值表达式（.leo 表达式）
};

USTRUCT()
struct FLeoScenarioEdge
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName To;                       // 目标节点 Id
	UPROPERTY(EditAnywhere)
	FString Condition;              // .leo 表达式；空串 = 恒真
	UPROPERTY(EditAnywhere)
	int32 Priority = 0;             // 大者先评估（同优先级按数组序）
	UPROPERTY(EditAnywhere)
	TArray<FLeoEdgeAction> Actions; // 转移副作用（选中边触发时执行）
};

USTRUCT()
struct FLeoScenarioNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Id;
	UPROPERTY(EditAnywhere)
	ELeoScenarioNodeType Type = ELeoScenarioNodeType::Chapter; // 默认旧语义，零迁移
	UPROPERTY(EditAnywhere)
	FText Caption;                  // 流程图显示用
	// ---- 按类型显隐（EditConditionHide）：非本类型的字段不出现在 Details ----
	UPROPERTY(EditAnywhere, meta = (EditConditionHide, EditCondition = "Type == ELeoScenarioNodeType::Chapter"))
	FName Chapter;                  // 章节名（= Content/Scripts/<Chapter>.leo）
	UPROPERTY(EditAnywhere, meta = (EditConditionHide, EditCondition = "Type == ELeoScenarioNodeType::Chapter"))
	FName Label;                    // 起始 label（NAME_None = 从头）
	UPROPERTY(EditAnywhere, meta = (EditConditionHide, EditCondition = "Type == ELeoScenarioNodeType::Ending"))
	FName EndingId;                 // 结局标识（外层图到达时经 OnGraphFinished 广播）
	UPROPERTY(EditAnywhere, meta = (EditConditionHide, EditCondition = "Type == ELeoScenarioNodeType::Subgraph"))
	TObjectPtr<ULeoScenarioGraph> SubGraph; // 子图资产（收束后回到本节点出边）
	// 连线由画布交互编辑（拖拽建边/点击选边），坐标由拖动管理——Details 不再展示
	UPROPERTY(EditAnywhere, meta = (EditConditionHide, EditCondition = "false"))
	TArray<FLeoScenarioEdge> Edges;
	UPROPERTY(EditAnywhere, meta = (EditConditionHide, EditCondition = "false"))
	FVector2D EditorPos = FVector2D(80, 60);
};

UCLASS()
class LEONARRATIVE_API ULeoScenarioGraph : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FName EntryNode;

	UPROPERTY(EditAnywhere)
	TArray<FLeoScenarioNode> Nodes;

	const FLeoScenarioNode* FindNode(FName Id) const
	{
		for (const FLeoScenarioNode& N : Nodes)
		{
			if (N.Id == Id) { return &N; }
		}
		return nullptr;
	}
};
