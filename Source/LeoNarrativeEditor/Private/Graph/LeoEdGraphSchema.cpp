#include "Graph/LeoEdGraphSchema.h"

#include "EdGraph/EdGraphPin.h"
#include "Framework/Commands/GenericCommands.h"
#include "Graph/LeoConnectionDrawingPolicy.h"
#include "Graph/LeoEdGraph.h"
#include "Graph/LeoEdGraphNodes.h"
#include "Graph/LeoGraphMirror.h"
#include "GraphEditorActions.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "LeoEdGraphSchema"

namespace
{
	FLeoGraphMirror* GetMirror(const UEdGraph* Graph)
	{
		if (const ULeoEdGraph* LeoGraph = Cast<ULeoEdGraph>(Graph))
		{
			return LeoGraph->Mirror;
		}
		return nullptr;
	}
}

// ---- FLeoSchemaAction_NewNode ----

UEdGraphNode* FLeoSchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode)
{
	if (FLeoGraphMirror* Mirror = GetMirror(ParentGraph))
	{
		return Mirror->AddNode(Type, FVector2D(Location), FromPin, bSelectNewNode);
	}
	return nullptr;
}

// ---- 连接规则 ----

const FPinConnectionResponse ULeoEdGraphSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// 不允许自环
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Both are on the same node"));
	}

	const bool bPinAIsEntry = PinA->GetOwningNode()->IsA<ULeoEdGraphNode_Entry>();
	const bool bPinBIsEntry = PinB->GetOwningNode()->IsA<ULeoEdGraphNode_Entry>();
	const bool bPinAIsEdge = PinA->GetOwningNode()->IsA<ULeoEdGraphNode_Edge>();
	const bool bPinBIsEdge = PinB->GetOwningNode()->IsA<ULeoEdGraphNode_Edge>();

	// 入口独占：仅允许 入口输出 → 节点输入（换目标时自动替换旧连线）
	if (bPinAIsEntry || bPinBIsEntry)
	{
		const bool bEntryOutToNodeIn = (bPinAIsEntry && !bPinBIsEdge
			&& PinA->Direction == EGPD_Output && PinB->Direction == EGPD_Input
			&& PinB->GetOwningNode()->IsA<ULeoEdGraphNode>());
		if (bEntryOutToNodeIn)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, TEXT(""));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("入口只能连到节点输入"));
	}

	// 边节点端点不允许手工改接（删除后重建）
	if (bPinAIsEdge || bPinBIsEdge)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("选中连线中点图标后可 Delete 删除；暂不支持改接"));
	}

	// Ending 为叶子：无出边
	const ULeoEdGraphNode* NodeA = Cast<ULeoEdGraphNode>(PinA->GetOwningNode());
	const ULeoEdGraphNode* NodeB = Cast<ULeoEdGraphNode>(PinB->GetOwningNode());
	if ((NodeA && NodeA->NodeType == ELeoScenarioNodeType::Ending && PinA->Direction == EGPD_Output)
		|| (NodeB && NodeB->NodeType == ELeoScenarioNodeType::Ending && PinB->Direction == EGPD_Output))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("结局节点是终点，没有出边"));
	}

	// 方向检查（同向稍后在 TryCreateConnection 自动换向）
	bool bDirectionsOK = false;
	if ((PinA->Direction == EGPD_Input) && (PinB->Direction == EGPD_Output)) { bDirectionsOK = true; }
	else if ((PinB->Direction == EGPD_Input) && (PinA->Direction == EGPD_Output)) { bDirectionsOK = true; }

	if (!bDirectionsOK)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Directions are not compatible"));
	}

	// 节点 → 节点：中点生成边节点（状态机 transition 同款）
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE, TEXT("Create a transition"));
}

bool ULeoEdGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* B) const
{
	UEdGraphPin* PinB = B;
	// 同向 pin 自动换向（状态机同款）
	if (PinB->Direction == PinA->Direction)
	{
		if (ULeoEdGraphNode* Node = Cast<ULeoEdGraphNode>(PinB->GetOwningNode()))
		{
			if (PinA->Direction == EGPD_Input) { PinB = Node->GetOutputPin(); }
			else { PinB = Node->GetInputPin(); }
		}
	}

	const bool bModified = Super::TryCreateConnection(PinA, PinB);

	if (bModified)
	{
		// 入口接线 → 写资产 EntryNode（重建镜像恢复连线）
		if (FLeoGraphMirror* Mirror = GetMirror(PinA->GetOwningNode() ? PinA->GetOwningNode()->GetGraph() : nullptr))
		{
			ULeoEdGraphNode_Entry* Entry = Cast<ULeoEdGraphNode_Entry>(PinA->GetOwningNode());
			ULeoEdGraphNode* Target = Cast<ULeoEdGraphNode>(PinB->GetOwningNode());
			if (!Entry)
			{
				Entry = Cast<ULeoEdGraphNode_Entry>(PinB->GetOwningNode());
				Target = Cast<ULeoEdGraphNode>(PinA->GetOwningNode());
			}
			if (Entry && Target)
			{
				Mirror->SetEntry(Target);
			}
		}
	}

	return bModified;
}

bool ULeoEdGraphSchema::CreateAutomaticConversionNodeAndConnections(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	// 拖线建边：资产加无条件边 + 增量建边节点（中点占位，二段布局重算）
	ULeoEdGraphNode* NodeA = Cast<ULeoEdGraphNode>(PinA->GetOwningNode());
	ULeoEdGraphNode* NodeB = Cast<ULeoEdGraphNode>(PinB->GetOwningNode());
	if (!NodeA || !NodeB) { return false; }

	if (FLeoGraphMirror* Mirror = GetMirror(NodeA->GetGraph()))
	{
		if (PinA->Direction == EGPD_Output)
		{
			Mirror->AddEdge(NodeA, NodeB);
		}
		else
		{
			Mirror->AddEdge(NodeB, NodeA);
		}
		return true;
	}
	return false;
}

// ---- 断线 → 删资产边 ----

void ULeoEdGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	if (ULeoEdGraphNode_Edge* Edge = Cast<ULeoEdGraphNode_Edge>(&TargetNode))
	{
		if (FLeoGraphMirror* Mirror = GetMirror(Edge->GetGraph())) { Mirror->DeleteEdge(Edge); }
		return;
	}
	if (ULeoEdGraphNode* Node = Cast<ULeoEdGraphNode>(&TargetNode))
	{
		if (FLeoGraphMirror* Mirror = GetMirror(Node->GetGraph())) { Mirror->RemoveAllLinks(Node->NodeId, /*bOut*/true, /*bIn*/true); }
		return;
	}
	Super::BreakNodeLinks(TargetNode);
}

void ULeoEdGraphSchema::BreakPinLinks(UEdGraphPin& TargetPinRef, bool bSendsNodeNotif) const
{
	UEdGraphPin* TargetPin = &TargetPinRef;
	if (ULeoEdGraphNode_Edge* Edge = Cast<ULeoEdGraphNode_Edge>(TargetPin ? TargetPin->GetOwningNode() : nullptr))
	{
		if (FLeoGraphMirror* Mirror = GetMirror(Edge->GetGraph())) { Mirror->DeleteEdge(Edge); }
		return;
	}
	if (ULeoEdGraphNode* Node = Cast<ULeoEdGraphNode>(TargetPin ? TargetPin->GetOwningNode() : nullptr))
	{
		if (FLeoGraphMirror* Mirror = GetMirror(Node->GetGraph()))
		{
			const bool bIsInput = TargetPin->Direction == EGPD_Input;
			Mirror->RemoveAllLinks(Node->NodeId, /*bOut*/!bIsInput, /*bIn*/bIsInput);
		}
		return;
	}
	Super::BreakPinLinks(*TargetPin, bSendsNodeNotif);
}

void ULeoEdGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	// Alt 点线断开：涉及边节点 = 删资产边
	ULeoEdGraphNode_Edge* Edge = Cast<ULeoEdGraphNode_Edge>(SourcePin ? SourcePin->GetOwningNode() : nullptr);
	if (!Edge) { Edge = Cast<ULeoEdGraphNode_Edge>(TargetPin ? TargetPin->GetOwningNode() : nullptr); }
	if (Edge)
	{
		if (FLeoGraphMirror* Mirror = GetMirror(Edge->GetGraph())) { Mirror->DeleteEdge(Edge); }
		return;
	}
	Super::BreakSinglePinLink(SourcePin, TargetPin);
}

// ---- 拖线落到节点本体 ----

bool ULeoEdGraphSchema::SupportsDropPinOnNode(UEdGraphNode* InTargetNode, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection, FText& OutErrorMessage) const
{
	// 落在普通节点/入口隧道卡片上：允许（边节点中点图标不参与）
	return InTargetNode && InTargetNode->IsA<ULeoEdGraphNode>();
}

UEdGraphPin* ULeoEdGraphSchema::DropPinOnNode(UEdGraphNode* InTargetNode, const FName& InSourcePinName, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection) const
{
	// 源为输出 → 连目标输入；源为输入（反向拖）→ 连目标输出（TryCreateConnection 会做同向换向）
	ULeoEdGraphNode* Node = Cast<ULeoEdGraphNode>(InTargetNode);
	if (!Node) { return nullptr; }
	return InSourcePinDirection == EGPD_Output ? Node->GetInputPin() : Node->GetOutputPin();
}

// ---- 菜单 ----

void ULeoEdGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	const FText Category = LOCTEXT("AddNodeCategory", "添加节点");

	struct FNodeMenuEntry { ELeoScenarioNodeType Type; const TCHAR* Menu; const TCHAR* Tip; };
	static const FNodeMenuEntry Entries[] = {
		{ ELeoScenarioNodeType::Chapter,  TEXT("章节"),  TEXT("进入后运行指定章节剧本，章末按出边转移") },
		{ ELeoScenarioNodeType::Branch,   TEXT("分流"),  TEXT("不运行章节，立即按出边条件分流") },
		{ ELeoScenarioNodeType::Ending,   TEXT("结局"),  TEXT("终态：广播 EndingId（解锁/成就数据源）") },
		{ ELeoScenarioNodeType::Subgraph, TEXT("子图"),  TEXT("运行子图资产，收束后回到本节点出边") },
	};

	for (const FNodeMenuEntry& E : Entries)
	{
		TSharedPtr<FLeoSchemaAction_NewNode> Action = MakeShared<FLeoSchemaAction_NewNode>(
			E.Type, Category, FText::FromString(E.Menu), FText::FromString(E.Tip));
		ContextMenuBuilder.AddAction(Action);
	}
}

void ULeoEdGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	check(Context && Context->Graph);

	if (Context->Node)
	{
		FToolMenuSection& Section = Menu->AddSection("LeoGraphNodeActions", LOCTEXT("NodeActionsHeader", "节点操作"));
		if (!Context->bIsDebugging)
		{
			Section.AddMenuEntry(FGenericCommands::Get().Delete);
			if (!Context->Node->IsA<ULeoEdGraphNode_Entry>() && !Context->Node->IsA<ULeoEdGraphNode_Edge>())
			{
				Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
			}
		}
	}
	else if (Context->Pin)
	{
		FToolMenuSection& Section = Menu->AddSection("LeoGraphPinActions", LOCTEXT("PinActionsHeader", "引脚操作"));
		Section.AddMenuEntry(FGraphEditorCommands::Get().BreakPinLinks);
	}

	Super::GetContextMenuActions(Menu, Context);
}

// ---- 外观 ----

FLinearColor ULeoEdGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor::White;
}

FConnectionDrawingPolicy* ULeoEdGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID,
	float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FLeoGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

#undef LOCTEXT_NAMESPACE
