// 编排图镜像节点三件套：
// - ULeoEdGraphNode       普通 Chapter/Branch/Ending/Subgraph 节点（顶入底出 pin，BT Task 式）
// - ULeoEdGraphNode_Entry 入口隧道（仅输出 pin；连到谁 = 资产 EntryNode，状态机 Entry 式）
// - ULeoEdGraphNode_Edge  连线节点（一对隐藏 pin，状态机 transition 式）：线由绘图策略画，
//                         中点小图标（SGraphNodeLeoEdge）承担点选/删除/双击。
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Data/LeoScenarioGraph.h"
#include "LeoEdGraphNodes.generated.h"

class SGraphNodeLeo;
class ULeoEdGraph;
class ULeoEdGraphNode;

UCLASS()
class ULeoEdGraphNode : public UEdGraphNode
{
	GENERATED_BODY()
public:
	// 镜像的资产节点 Id（事实源 = 资产 Nodes[]，按 Id 定位）
	FName NodeId;
	ELeoScenarioNodeType NodeType = ELeoScenarioNodeType::Chapter;

	// PIE/干跑激活态：黄色边框（BT 调试观感）+ 出边连线变橙
	bool bLeoActive = false;

	// 视图刷新通道（CreateVisualWidget 时回填，Details 编辑后定向刷新卡片）
	TWeakPtr<SGraphNodeLeo> NodeWidget;

	// ---- UEdGraphNode ----
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

	// ---- 便捷访问 ----
	const FLeoScenarioNode* FindAssetNode() const;
	FLeoScenarioNode* FindAssetNodeMutable() const;
	UEdGraphPin* GetInputPin() const;
	UEdGraphPin* GetOutputPin() const;

	// 卡片体色（BT 色板映射；入口节点覆盖为 Root 半透明灰）
	virtual FLinearColor GetBodyColor() const;

	void RefreshVisual();
};

// 入口隧道节点：一个输出 pin；拖线到某节点 = 设为 EntryNode（schema 强制独占）
UCLASS()
class ULeoEdGraphNode_Entry : public ULeoEdGraphNode
{
	GENERATED_BODY()
public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual FLinearColor GetBodyColor() const override;
};

// 连线节点：镜像 (FromNodeId, EdgeIndex) 处的资产出边
UCLASS()
class ULeoEdGraphNode_Edge : public UEdGraphNode
{
	GENERATED_BODY()
public:
	FName FromNodeId;
	int32 EdgeIndex = INDEX_NONE;

	// 干跑/PIE：源节点激活时连线与中点图标变橙
	bool bLeoActive = false;

	// 中点图标朝向（二段布局算出，非序列化）
	FVector2D CachedRotation = FVector2D(1.0f, 0.0f);

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;

	UEdGraphPin* GetInputPin() const;   // 连 From 的输出
	UEdGraphPin* GetOutputPin() const;  // 连 To 的输入
	ULeoEdGraphNode* GetFromNode() const;
	ULeoEdGraphNode* GetToNode() const;
	const FLeoScenarioEdge* FindAssetEdge() const;
};
