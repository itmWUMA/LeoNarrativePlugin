// Leo 编排图 Schema：规则与菜单的声明处（语义对齐 UAnimationStateMachineSchema）。
// - 节点输出 → 节点输入 = CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE：中点生成边节点（拖线建边）；
// - 入口 → 节点 = 独占（BREAK_OTHERS_A），连上即设为资产 EntryNode；
// - 断线（Alt 点线 / 右键 Break Link / BreakNodeLinks）= 删资产边；
// - 右键空白 = 引擎 SGraphActionMenu（搜索式，BT 同款）列 4 类节点；
// - 连线绘制策略 = FLeoGraphConnectionDrawingPolicy（状态机复刻）。
#pragma once

#include "CoreMinimal.h"
#include "Data/LeoScenarioGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "LeoEdGraphSchema.generated.h"

// 右键菜单 action：新增指定类型节点（可从 pin 拖到空白弹出并自动接线）
class FLeoSchemaAction_NewNode : public FEdGraphSchemaAction
{
public:
	FLeoSchemaAction_NewNode() : Type(ELeoScenarioNodeType::Chapter) {}
	FLeoSchemaAction_NewNode(ELeoScenarioNodeType InType, const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip)
		: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, 0)
		, Type(InType)
	{
	}

	ELeoScenarioNodeType Type;

	// ---- FEdGraphSchemaAction ----
	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, class UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override;
	// ---- end ----
};

UCLASS(MinimalAPI)
class ULeoEdGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()
public:
	// ---- 连接规则 ----
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual bool CreateAutomaticConversionNodeAndConnections(UEdGraphPin* A, UEdGraphPin* B) const override;

	// ---- 断线 → 删资产边 ----
	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotif) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	// ---- 拖线落到节点本体（未精准命中 pin 条）的回退：落卡片任意处即连 ----
	virtual bool SupportsDropPinOnNode(UEdGraphNode* InTargetNode, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection, FText& OutErrorMessage) const override;
	virtual UEdGraphPin* DropPinOnNode(UEdGraphNode* InTargetNode, const FName& InSourcePinName, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection) const override;

	// ---- 菜单 ----
	virtual void GetGraphContextActions(struct FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

	// ---- 外观 ----
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual class FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
		float InZoomFactor, const class FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements,
		class UEdGraph* InGraphObj) const override;
};
