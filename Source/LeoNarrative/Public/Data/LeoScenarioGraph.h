// ScenarioGraph：章节编排图（数据优先，图编辑器后置）。
// 节点 = 跑哪个章节的哪个 label；边 = 条件（.leo 表达式语法）+ 优先级。
// 玩家流程图 UI 未来从同一份数据渲染。
#pragma once

#include "Engine/DataAsset.h"
#include "LeoScenarioGraph.generated.h"

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
};

USTRUCT()
struct FLeoScenarioNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere)
	FName Id;
	UPROPERTY(EditAnywhere)
	FText Caption;                  // 流程图显示用
	UPROPERTY(EditAnywhere)
	FName Chapter;                  // 章节名（= Content/Scripts/<Chapter>.leo）
	UPROPERTY(EditAnywhere)
	FName Label;                    // 起始 label（NAME_None = 从头）
	UPROPERTY(EditAnywhere)
	TArray<FLeoScenarioEdge> Edges; // 出边：章末按优先级评估条件
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
