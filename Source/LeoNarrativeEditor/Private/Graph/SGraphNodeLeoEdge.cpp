#include "Graph/SGraphNodeLeoEdge.h"

#include "ConnectionDrawingPolicy.h" // FGeometryHelper
#include "Graph/LeoEdGraph.h"
#include "Graph/LeoEdGraphNodes.h"
#include "Layout/Geometry.h"
#include "Misc/Attribute.h"
#include "SGraphPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Templates/Casts.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

/////////////////////////////////////////////////////
// SGraphNodeLeoEdge

void SGraphNodeLeoEdge::Construct(const FArguments& InArgs, ULeoEdGraphNode_Edge* InNode)
{
	this->GraphNode = InNode;
	this->UpdateGraphNode();
}

void SGraphNodeLeoEdge::MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	// 忽略：位置由两端卡片决定（状态机 transition 同款）
}

bool SGraphNodeLeoEdge::RequiresSecondPassLayout() const
{
	return true;
}

void SGraphNodeLeoEdge::PerformSecondPassLayout(const TMap< UObject*, TSharedRef<SNode> >& NodeToWidgetLookup) const
{
	ULeoEdGraphNode_Edge* EdgeNode = CastChecked<ULeoEdGraphNode_Edge>(GraphNode);

	// 找两端卡片几何
	FGeometry StartGeom;
	FGeometry EndGeom;

	int32 TransIndex = 0;
	int32 NumOfTrans = 1;

	ULeoEdGraphNode* PrevNode = EdgeNode->GetFromNode();
	ULeoEdGraphNode* NextNode = EdgeNode->GetToNode();
	if ((PrevNode != nullptr) && (NextNode != nullptr))
	{
		const TSharedRef<SNode>* pPrevNodeWidget = NodeToWidgetLookup.Find(PrevNode);
		const TSharedRef<SNode>* pNextNodeWidget = NodeToWidgetLookup.Find(NextNode);
		if ((pPrevNodeWidget != nullptr) && (pNextNodeWidget != nullptr))
		{
			const TSharedRef<SNode>& PrevNodeWidget = *pPrevNodeWidget;
			const TSharedRef<SNode>& NextNodeWidget = *pNextNodeWidget;

			StartGeom = FGeometry(FVector2D(PrevNode->NodePosX, PrevNode->NodePosY), FVector2D::ZeroVector, PrevNodeWidget->GetDesiredSize(), 1.0f);
			EndGeom = FGeometry(FVector2D(NextNode->NodePosX, NextNode->NodePosY), FVector2D::ZeroVector, NextNodeWidget->GetDesiredSize(), 1.0f);

			// 同一对节点间的多条边沿方向排开
			TArray<ULeoEdGraphNode_Edge*> SamePairEdges;
			if (const ULeoEdGraph* Owner = Cast<ULeoEdGraph>(EdgeNode->GetGraph()))
			{
				for (UEdGraphNode* N : Owner->Nodes)
				{
					if (ULeoEdGraphNode_Edge* E = Cast<ULeoEdGraphNode_Edge>(N))
					{
						if (E->FromNodeId == EdgeNode->FromNodeId && E->GetToNode() == NextNode)
						{
							SamePairEdges.Add(E);
						}
					}
				}
			}
			TransIndex = SamePairEdges.IndexOfByKey(EdgeNode);
			NumOfTrans = FMath::Max(NumOfTrans, SamePairEdges.Num());

			PrevStateNodeWidgetPtr = PrevNodeWidget;
		}
	}

	PositionBetweenTwoNodesWithOffset(StartGeom, EndGeom, TransIndex, NumOfTrans);
}

void SGraphNodeLeoEdge::PositionBetweenTwoNodesWithOffset(const FGeometry& StartGeom, const FGeometry& EndGeom, int32 NodeIndex, int32 MaxNodes) const
{
	// 种子点 = 两卡片中心中点；锚点 = 各自边框最近点
	const FVector2D StartCenter = FGeometryHelper::CenterOf(StartGeom);
	const FVector2D EndCenter = FGeometryHelper::CenterOf(EndGeom);
	const FVector2D SeedPoint = (StartCenter + EndCenter) * 0.5f;

	const FVector2D StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2D EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	// 定位在连线中点、法向抬高
	const float Height = 24.0f;

	const FVector2D DesiredNodeSize = GetDesiredSize();

	FVector2D DeltaPos(EndAnchorPoint - StartAnchorPoint);

	if (DeltaPos.IsNearlyZero())
	{
		DeltaPos = FVector2D(10.0f, 0.0f);
	}

	const FVector2D Normal = FVector2D(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

	const FVector2D NewCenter = StartAnchorPoint + (0.5 * DeltaPos) + (Height * Normal);

	FVector2D DeltaNormal = DeltaPos.GetSafeNormal();

	// 多条边时沿方向排开
	const float MutliNodeSpace = 0.0f;
	const float MultiNodeStep = (1.f + MutliNodeSpace);

	const float MultiNodeStart = -((MaxNodes - 1) * MultiNodeStep) / 2.f;
	const float MultiNodeOffset = MultiNodeStart + (NodeIndex * MultiNodeStep);

	const FVector2D NewCorner = NewCenter - (0.5 * DesiredNodeSize) + (DeltaNormal * MultiNodeOffset * DesiredNodeSize.X);

	GraphNode->NodePosX = static_cast<int32>(NewCorner.X);
	GraphNode->NodePosY = static_cast<int32>(NewCorner.Y);

	ULeoEdGraphNode_Edge* EdgeNode = CastChecked<ULeoEdGraphNode_Edge>(GraphNode);
	EdgeNode->CachedRotation = DeltaNormal;
}

void SGraphNodeLeoEdge::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	ULeoEdGraphNode_Edge* EdgeNode = CastChecked<ULeoEdGraphNode_Edge>(GraphNode);

	// 方向箭头随连线朝向旋转
	TSharedRef<SImage> DirectionImage = SNew(SImage)
		.Image(this, &SGraphNodeLeoEdge::GetTransitionIconImage)
		.ColorAndOpacity(FStyleColors::Background);

	DirectionImage->SetRenderTransform(MakeAttributeLambda([this]() -> TOptional<FSlateRenderTransform>
	{
		ULeoEdGraphNode_Edge* EdgeNodeInner = CastChecked<ULeoEdGraphNode_Edge>(GraphNode);
		return FSlateRenderTransform(FQuat2D(EdgeNodeInner->CachedRotation));
	}));
	DirectionImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			.Padding(2.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Graph.AnimTransitionNode.ColorSpill"))
				.ColorAndOpacity(this, &SGraphNodeLeoEdge::GetTransitionColor)
			]

			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Padding(4.0f)
				[
					DirectionImage
				]
			]

			// 选中描边（橙）
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Graph.AnimTransitionNode.Selection"))
				.Padding(0)
				.Visibility_Lambda([this]()
				{
					TSharedPtr<SGraphPanel> OwnerPanel = OwnerGraphPanelPtr.Pin();
					if (!OwnerPanel.IsValid())
					{
						return EVisibility::Hidden;
					}

					return OwnerPanel->SelectionManager.IsNodeSelected(GraphNode) ? EVisibility::HitTestInvisible : EVisibility::Hidden;
				})
			]
		];
}

FSlateColor SGraphNodeLeoEdge::StaticGetTransitionColor(const ULeoEdGraphNode_Edge* EdgeNode, bool bIsHovered)
{
	FLinearColor ActiveColor = FStyleColors::AccentOrange.GetSpecifiedColor().Desaturate(0.25f);
	ActiveColor.A = 1.0f;
	const FSlateColor HoverColor = FStyleColors::AccentOrange;
	const FSlateColor BaseColor = FStyleColors::Foreground;

	if (EdgeNode && EdgeNode->bLeoActive)
	{
		return ActiveColor;
	}
	return bIsHovered ? HoverColor : BaseColor;
}

FSlateColor SGraphNodeLeoEdge::GetTransitionColor() const
{
	// 本体或前驱卡片 hover 时高亮
	ULeoEdGraphNode_Edge* EdgeNode = CastChecked<ULeoEdGraphNode_Edge>(GraphNode);
	return StaticGetTransitionColor(EdgeNode, (IsHovered() || (PrevStateNodeWidgetPtr.IsValid() && PrevStateNodeWidgetPtr.Pin()->IsHovered())));
}

const FSlateBrush* SGraphNodeLeoEdge::GetTransitionIconImage() const
{
	return FAppStyle::GetBrush("Graph.AnimTransitionNode.Icon");
}

int32 SGraphNodeLeoEdge::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 抬高层级：画在卡片之上
	return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 100, InWidgetStyle, bParentEnabled);
}
