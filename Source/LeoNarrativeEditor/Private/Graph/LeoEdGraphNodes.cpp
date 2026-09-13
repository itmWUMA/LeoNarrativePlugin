#include "Graph/LeoEdGraphNodes.h"

#include "Graph/LeoEdGraph.h"
#include "Graph/SGraphNodeLeo.h"
#include "Graph/SGraphNodeLeoEdge.h"

#define LOCTEXT_NAMESPACE "LeoEdGraphNodes"

namespace
{
	FText LeoTypeName(ELeoScenarioNodeType Type)
	{
		switch (Type)
		{
		case ELeoScenarioNodeType::Chapter:  return LOCTEXT("Chapter", "章节");
		case ELeoScenarioNodeType::Branch:   return LOCTEXT("Branch", "分流");
		case ELeoScenarioNodeType::Ending:   return LOCTEXT("Ending", "结局");
		case ELeoScenarioNodeType::Subgraph: return LOCTEXT("Subgraph", "子图");
		default:                             return LOCTEXT("Unknown", "未知");
		}
	}
}

// ---- ULeoEdGraphNode ----

void ULeoEdGraphNode::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, TEXT("LeoNode"), TEXT("In"));
	if (NodeType != ELeoScenarioNodeType::Ending) // Ending = 叶子（同 BT Task 无输出 pin）
	{
		CreatePin(EGPD_Output, TEXT("LeoNode"), TEXT("Out"));
	}
}

FText ULeoEdGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (const FLeoScenarioNode* N = FindAssetNode())
	{
		if (!N->Caption.IsEmpty())
		{
			return N->Caption;
		}
		return FText::FromName(N->Id);
	}
	return FText::FromName(NodeId);
}

FText ULeoEdGraphNode::GetTooltipText() const
{
	const FLeoScenarioNode* N = FindAssetNode();
	FString Tip = FString::Printf(TEXT("%s [%s]"), *LeoTypeName(NodeType).ToString(), *NodeId.ToString());
	if (N)
	{
		switch (N->Type)
		{
		case ELeoScenarioNodeType::Chapter:
			Tip += FString::Printf(TEXT("\n章节: %s"), *N->Chapter.ToString());
			if (!N->Label.IsNone()) { Tip += FString::Printf(TEXT(" @ %s"), *N->Label.ToString()); }
			break;
		case ELeoScenarioNodeType::Ending:
			Tip += FString::Printf(TEXT("\n结局 Id: %s"), *N->EndingId.ToString());
			break;
		case ELeoScenarioNodeType::Subgraph:
			Tip += FString::Printf(TEXT("\n子图: %s"), *GetNameSafe(N->SubGraph));
			break;
		default: break;
		}
	}
	return FText::FromString(Tip);
}

TSharedPtr<SGraphNode> ULeoEdGraphNode::CreateVisualWidget()
{
	TSharedPtr<SGraphNodeLeo> Widget = SNew(SGraphNodeLeo, this);
	NodeWidget = Widget;
	return Widget;
}const FLeoScenarioNode* ULeoEdGraphNode::FindAssetNode() const
{
	const ULeoEdGraph* Owner = Cast<ULeoEdGraph>(GetGraph());
	return (Owner && Owner->GetLeoGraph()) ? Owner->GetLeoGraph()->FindNode(NodeId) : nullptr;
}

FLeoScenarioNode* ULeoEdGraphNode::FindAssetNodeMutable() const
{
	ULeoEdGraph* Owner = Cast<ULeoEdGraph>(GetGraph());
	ULeoScenarioGraph* G = Owner ? Owner->GetLeoGraph() : nullptr;
	if (!G) { return nullptr; }
	for (FLeoScenarioNode& N : G->Nodes)
	{
		if (N.Id == NodeId) { return &N; }
	}
	return nullptr;
}

UEdGraphPin* ULeoEdGraphNode::GetInputPin() const
{
	return Pins.IsValidIndex(0) && Pins[0]->Direction == EGPD_Input ? Pins[0] : nullptr;
}

UEdGraphPin* ULeoEdGraphNode::GetOutputPin() const
{
	for (UEdGraphPin* P : Pins)
	{
		if (P->Direction == EGPD_Output) { return P; }
	}
	return nullptr;
}

FLinearColor ULeoEdGraphNode::GetBodyColor() const
{
	// BT 色板映射（BehaviorTreeColors.h 原值）：卡片观感与行为树编辑器一致
	switch (NodeType)
	{
	case ELeoScenarioNodeType::Chapter:  return FLinearColor(0.24f, 0.055f, 0.715f);  // NodeBody::Task
	case ELeoScenarioNodeType::Branch:   return FLinearColor(0.0f, 0.07f, 0.4f);      // NodeBody::Decorator
	case ELeoScenarioNodeType::Ending:   return FLinearColor(0.1f, 0.1f, 0.1f);       // NodeBody::Composite
	case ELeoScenarioNodeType::Subgraph: return FLinearColor(0.0f, 0.4f, 0.22f);      // NodeBody::Service
	default:                             return FLinearColor(0.15f, 0.15f, 0.15f);   // NodeBody::Default
	}
}

void ULeoEdGraphNode::RefreshVisual()
{
	if (TSharedPtr<SGraphNodeLeo> Widget = NodeWidget.Pin())
	{
		Widget->UpdateGraphNode();
	}
}

// ---- ULeoEdGraphNode_Entry ----

void ULeoEdGraphNode_Entry::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, TEXT("LeoNode"), TEXT("Out"));
}

FText ULeoEdGraphNode_Entry::GetNodeTitle(ENodeTitleType::Type) const
{
	return LOCTEXT("EntryTitle", "入口");
}

FLinearColor ULeoEdGraphNode_Entry::GetBodyColor() const
{
	return FLinearColor(0.5f, 0.5f, 0.5f, 0.1f); // NodeBody::Root
}

// ---- ULeoEdGraphNode_Edge ----

void ULeoEdGraphNode_Edge::AllocateDefaultPins()
{
	// 状态机 transition 同款：一对隐藏 pin，线由 FLeoGraphConnectionDrawingPolicy 画
	UEdGraphPin* In = CreatePin(EGPD_Input, TEXT("LeoEdge"), TEXT("In"));
	In->bHidden = true;
	UEdGraphPin* Out = CreatePin(EGPD_Output, TEXT("LeoEdge"), TEXT("Out"));
	Out->bHidden = true;
}

FText ULeoEdGraphNode_Edge::GetNodeTitle(ENodeTitleType::Type) const
{
	const ULeoEdGraphNode* From = GetFromNode();
	const ULeoEdGraphNode* To = GetToNode();
	return FText::FromString(FString::Printf(TEXT("%s → %s"),
		From ? *From->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("?"),
		To ? *To->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("?")));
}

FText ULeoEdGraphNode_Edge::GetTooltipText() const
{
	const FLeoScenarioEdge* E = FindAssetEdge();
	FString Tip = GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	if (E)
	{
		Tip += FString::Printf(TEXT("\n条件: %s\n优先级: %d"), E->Condition.IsEmpty() ? TEXT("（恒真）") : *E->Condition, E->Priority);
	}
	return FText::FromString(Tip);
}

TSharedPtr<SGraphNode> ULeoEdGraphNode_Edge::CreateVisualWidget()
{
	return SNew(SGraphNodeLeoEdge, this);
}

UEdGraphPin* ULeoEdGraphNode_Edge::GetInputPin() const
{
	return Pins.IsValidIndex(0) && Pins[0]->Direction == EGPD_Input ? Pins[0] : nullptr;
}

UEdGraphPin* ULeoEdGraphNode_Edge::GetOutputPin() const
{
	return Pins.IsValidIndex(1) && Pins[1]->Direction == EGPD_Output ? Pins[1] : nullptr;
}

ULeoEdGraphNode* ULeoEdGraphNode_Edge::GetFromNode() const
{
	const ULeoEdGraph* Owner = Cast<ULeoEdGraph>(GetGraph());
	return Owner ? Owner->FindLeoNode(FromNodeId) : nullptr;
}

ULeoEdGraphNode* ULeoEdGraphNode_Edge::GetToNode() const
{
	const FLeoScenarioEdge* E = FindAssetEdge();
	const ULeoEdGraph* Owner = Cast<ULeoEdGraph>(GetGraph());
	return (E && Owner) ? Owner->FindLeoNode(E->To) : nullptr;
}

const FLeoScenarioEdge* ULeoEdGraphNode_Edge::FindAssetEdge() const
{
	const ULeoEdGraph* Owner = Cast<ULeoEdGraph>(GetGraph());
	const ULeoScenarioGraph* G = Owner ? Owner->GetLeoGraph() : nullptr;
	if (!G) { return nullptr; }
	const FLeoScenarioNode* N = G->FindNode(FromNodeId);
	return (N && N->Edges.IsValidIndex(EdgeIndex)) ? &N->Edges[EdgeIndex] : nullptr;
}

#undef LOCTEXT_NAMESPACE
