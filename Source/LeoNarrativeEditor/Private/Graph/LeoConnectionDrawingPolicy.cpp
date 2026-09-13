#include "Graph/LeoConnectionDrawingPolicy.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/LeoEdGraph.h"
#include "Graph/LeoEdGraphNodes.h"
#include "Graph/SGraphNodeLeoEdge.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Math/UnrealMathSSE.h"
#include "Math/UnrealMathUtility.h"
#include "Rendering/DrawElements.h"
#include "SGraphNode.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Templates/Casts.h"

/////////////////////////////////////////////////////
// FLeoGraphConnectionDrawingPolicy

FLeoGraphConnectionDrawingPolicy::FLeoGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
	const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	, GraphObj(InGraphObj)
{
	ArrowImage = FAppStyle::GetBrush(TEXT("Graph.AnimStateNode.ConnectionArrow"));
}

void FLeoGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireThickness = 1.5f;

	if (InputPin)
	{
		if (ULeoEdGraphNode_Edge* EdgeNode = Cast<ULeoEdGraphNode_Edge>(InputPin->GetOwningNode()))
		{
			const bool IsInputPinHovered = HoveredPins.Contains(InputPin);
			Params.WireColor = SGraphNodeLeoEdge::StaticGetTransitionColor(EdgeNode, IsInputPinHovered).GetSpecifiedColor();
		}
	}

	const bool bDeemphasizeUnhoveredPins = HoveredPins.Num() > 0;
	if (bDeemphasizeUnhoveredPins && !Params.bUserFlag2)
	{
		ApplyHoverDeemphasis(OutputPin, InputPin, /*inout*/ Params.WireThickness, /*inout*/ Params.WireColor);
	}

	// 拖线预览半透明
	if (Params.bUserFlag2)
	{
		Params.WireColor.A = 0.5f;
	}
}

void FLeoGraphConnectionDrawingPolicy::DetermineLinkGeometry(
	FArrangedChildren& ArrangedNodes,
	TSharedRef<SWidget>& OutputPinWidget,
	UEdGraphPin* OutputPin,
	UEdGraphPin* InputPin,
	/*out*/ FArrangedWidget*& StartWidgetGeometry,
	/*out*/ FArrangedWidget*& EndWidgetGeometry
)
{
	// 入口 → 目标节点：两端都取「节点卡片」几何
	if (ULeoEdGraphNode_Entry* EntryNode = Cast<ULeoEdGraphNode_Entry>(OutputPin->GetOwningNode()))
	{
		if (ULeoEdGraphNode* Target = Cast<ULeoEdGraphNode>(InputPin->GetOwningNode()))
		{
			if (int32* EntryIndex = NodeWidgetMap.Find(EntryNode))
			{
				if (int32* TargetIndex = NodeWidgetMap.Find(Target))
				{
					StartWidgetGeometry = &(ArrangedNodes[*EntryIndex]);
					EndWidgetGeometry = &(ArrangedNodes[*TargetIndex]);
				}
			}
		}
	}
	else if (ULeoEdGraphNode_Edge* EdgeNode = Cast<ULeoEdGraphNode_Edge>(InputPin->GetOwningNode()))
	{
		// 状态机语义：连线两端 = 转移节点的前驱/后继状态节点卡片
		ULeoEdGraphNode* PrevNode = EdgeNode->GetFromNode();
		ULeoEdGraphNode* NextNode = EdgeNode->GetToNode();
		if ((PrevNode != nullptr) && (NextNode != nullptr))
		{
			int32* PrevNodeIndex = NodeWidgetMap.Find(PrevNode);
			int32* NextNodeIndex = NodeWidgetMap.Find(NextNode);
			if ((PrevNodeIndex != nullptr) && (NextNodeIndex != nullptr))
			{
				StartWidgetGeometry = &(ArrangedNodes[*PrevNodeIndex]);
				EndWidgetGeometry = &(ArrangedNodes[*NextNodeIndex]);
			}
		}
	}
}

void FLeoGraphConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	// 建几何加速结构：节点 → Arranged 下标
	NodeWidgetMap.Empty();
	for (int32 NodeIndex = 0; NodeIndex < ArrangedNodes.Num(); ++NodeIndex)
	{
		FArrangedWidget& CurWidget = ArrangedNodes[NodeIndex];
		TSharedRef<SGraphNode> ChildNode = StaticCastSharedRef<SGraphNode>(CurWidget.Widget);
		NodeWidgetMap.Add(ChildNode->GetNodeObj(), NodeIndex);
	}

	FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

void FLeoGraphConnectionDrawingPolicy::DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin)
{
	FConnectionParams Params;
	Params.bUserFlag2 = true; // 预览线
	UEdGraphPin* InputPin = Pin->Direction == EGPD_Input ? Pin : nullptr;
	UEdGraphPin* OutputPin = Pin->Direction == EGPD_Output ? Pin : nullptr;
	DetermineWiringStyle(InputPin, OutputPin, /*inout*/ Params);

	const FVector2f SeedPoint = Pin->Direction == EGPD_Input ? StartPoint : EndPoint;
	const FVector2f AdjustedSeedPoint = FGeometryHelper::FindClosestPointOnGeom(PinGeometry, SeedPoint);
	const FVector2f AdjustedStartPoint = Pin->Direction == EGPD_Input ? StartPoint : AdjustedSeedPoint;
	const FVector2f AdjustedEndPoint = Pin->Direction == EGPD_Input ? AdjustedSeedPoint : EndPoint;

	DrawSplineWithArrow(AdjustedStartPoint, AdjustedEndPoint, Params);
}

void FLeoGraphConnectionDrawingPolicy::DrawSplineWithArrow(const FVector2f& StartAnchorPoint, const FVector2f& EndAnchorPoint, const FConnectionParams& Params)
{
	Internal_DrawLineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
}

void FLeoGraphConnectionDrawingPolicy::Internal_DrawLineWithArrow(const FVector2f& StartAnchorPoint, const FVector2f& EndAnchorPoint, const FConnectionParams& Params)
{
	const float LineSeparationAmount = 6.0f * ZoomFactor;

	const FVector2f DeltaPos = EndAnchorPoint - StartAnchorPoint;
	const FVector2f UnitDelta = DeltaPos.GetSafeNormal();
	const FVector2f Normal = FVector2f(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

	// 平行偏移双线 + 箭头让位
	const FVector2f DirectionBias = Normal * LineSeparationAmount;
	const FVector2f LengthBias = ArrowRadius.X * UnitDelta;
	const FVector2f StartPoint = StartAnchorPoint + DirectionBias + LengthBias;
	const FVector2f EndPoint = EndAnchorPoint + DirectionBias - LengthBias;
	FLinearColor ArrowHeadColor = Params.WireColor;

	// 线本体（长度收 0.8 避免与箭头叠色）
	DrawConnection(WireLayerID, StartPoint, EndPoint - (LengthBias * 0.8f), Params);

	const FVector2f ArrowDrawPos = EndPoint - ArrowRadius;
	const double AngleInRadians = FMath::Atan2(DeltaPos.Y, DeltaPos.X);

	// hover 到连线附近：上报 pin 重叠结果（面板据此做 Alt 断线/右键线菜单）+ relink 手柄底圈
	bool bStartHovered = false;
	bool bEndHovered = false;
	const FVector2f FVecMousePos = FVector2f(AbsoluteMousePosition.X, AbsoluteMousePosition.Y);
	const FVector2f ClosestPoint = FMath::ClosestPointOnSegment2D(FVecMousePos, StartPoint, EndPoint);
	if ((ClosestPoint - FVecMousePos).Length() < RelinkHandleHoverRadius * ZoomFactor)
	{
		bStartHovered = (StartPoint - AbsoluteMousePosition).Length() < RelinkHandleHoverRadius * ZoomFactor;
		bEndHovered = (EndPoint - AbsoluteMousePosition).Length() < RelinkHandleHoverRadius * ZoomFactor;

		const float SquaredDistToPin1 = (Params.AssociatedPin1 != nullptr) ? (StartPoint - AbsoluteMousePosition).SizeSquared() : FLT_MAX;
		const float SquaredDistToPin2 = (Params.AssociatedPin2 != nullptr) ? (EndPoint - AbsoluteMousePosition).SizeSquared() : FLT_MAX;
		UEdGraphPin* Pin1 = Params.AssociatedPin1;
		UEdGraphPin* Pin2 = Params.AssociatedPin2;

		if (bStartHovered)
		{
			SplineOverlapResult = FGraphSplineOverlapResult(Pin2, Pin1, FMath::Min(SquaredDistToPin1, SquaredDistToPin2), SquaredDistToPin2, SquaredDistToPin1, true);
		}
		else if (bEndHovered)
		{
			SplineOverlapResult = FGraphSplineOverlapResult(Pin1, Pin2, FMath::Min(SquaredDistToPin1, SquaredDistToPin2), SquaredDistToPin1, SquaredDistToPin2, true);
		}

		// 仅在无 relink 拖拽进行时画手柄底圈
		if (RelinkConnections.IsEmpty() && !Params.bUserFlag2)
		{
			if (bStartHovered)
			{
				static FSlateRoundedBoxBrush RoundedBoxBrush = FSlateRoundedBoxBrush(FStyleColors::Foreground, ArrowImage->ImageSize.X * 0.5f);

				FSlateDrawElement::MakeBox(DrawElementsList,
					ArrowLayerID - 1, // 画在箭头之下
					FPaintGeometry(StartPoint - ArrowRadius + (LengthBias * 0.2f), ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
					&RoundedBoxBrush,
					ESlateDrawEffect::None,
					FStyleColors::AccentOrange.GetSpecifiedColor());
			}
			else if (bEndHovered)
			{
				static FSlateRoundedBoxBrush RoundedBoxBrush = FSlateRoundedBoxBrush(FStyleColors::Foreground, ArrowImage->ImageSize.X * 0.5f);

				FSlateDrawElement::MakeBox(DrawElementsList,
					ArrowLayerID - 1,
					FPaintGeometry(EndPoint - ArrowRadius - (LengthBias * 0.2f), ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
					&RoundedBoxBrush,
					ESlateDrawEffect::None,
					FStyleColors::AccentOrange.GetSpecifiedColor());

				ArrowHeadColor = FLinearColor::Black;
			}
		}
	}

	// 终点三角箭头
	FSlateDrawElement::MakeRotatedBox(
		DrawElementsList,
		ArrowLayerID,
		FPaintGeometry(ArrowDrawPos, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
		ArrowImage,
		ESlateDrawEffect::None,
		static_cast<float>(AngleInRadians),
		TOptional<FVector2f>(),
		FSlateDrawElement::RelativeToElement,
		ArrowHeadColor
	);
}

void FLeoGraphConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	// 种子点 = 两卡片中心中点；锚点 = 各自边框最近点（父上子下 → 底出顶进）
	const FVector2f StartCenter = FGeometryHelper::CenterOf(StartGeom);
	const FVector2f EndCenter = FGeometryHelper::CenterOf(EndGeom);
	const FVector2f SeedPoint = (StartCenter + EndCenter) * 0.5f;

	const FVector2f StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2f EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	DrawSplineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
}

FVector2f FLeoGraphConnectionDrawingPolicy::ComputeSplineTangent(const FVector2f& Start, const FVector2f& End) const
{
	// 单位方向向量 → 近似直线（状态机连线形貌）
	const FVector2f Delta = End - Start;
	const FVector2f NormDelta = Delta.GetSafeNormal();

	return NormDelta;
}
