#include "Widgets/SLeoGraphCanvas.h"

#include "Data/LeoScenarioGraph.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

SLATE_IMPLEMENT_WIDGET(SLeoGraphCanvas)
void SLeoGraphCanvas::PrivateRegisterAttributes(FSlateAttributeInitializer&)
{
	// 本控件无 Slate 属性（引擎惯例：空注册，见 SOverlay）
}

namespace LeoGraphCanvasConst
{
	constexpr float NodeW = 170.f, NodeH = 58.f;
	constexpr float CanvasPadding = 120.f;

	FVector2D GetNodeSize() { return FVector2D(NodeW, NodeH); }

	FLinearColor NodeColor(uint8 T)
	{
		switch (static_cast<ELeoScenarioNodeType>(T))
		{
		case ELeoScenarioNodeType::Chapter:  return FLinearColor(0.10f, 0.30f, 0.55f); // 蓝
		case ELeoScenarioNodeType::Branch:   return FLinearColor(0.55f, 0.40f, 0.10f); // 橙
		case ELeoScenarioNodeType::Ending:   return FLinearColor(0.45f, 0.12f, 0.45f); // 紫
		case ELeoScenarioNodeType::Subgraph: return FLinearColor(0.12f, 0.42f, 0.30f); // 绿
		default: return FLinearColor::Gray;
		}
	}

	const TCHAR* NodeTypeName(uint8 T)
	{
		switch (static_cast<ELeoScenarioNodeType>(T))
		{
		case ELeoScenarioNodeType::Chapter:  return TEXT("Chapter");
		case ELeoScenarioNodeType::Branch:   return TEXT("Branch");
		case ELeoScenarioNodeType::Ending:   return TEXT("Ending");
		case ELeoScenarioNodeType::Subgraph: return TEXT("Subgraph");
		default: return TEXT("?");
		}
	}
}

using namespace LeoGraphCanvasConst;

// ---- SLeoGraphNodeWidget ----

const FLeoScenarioNode* SLeoGraphNodeWidget::LookupNode() const
{
	const ULeoScenarioGraph* G = Graph.Get();
	return (G && G->Nodes.IsValidIndex(NodeIndex)) ? &G->Nodes[NodeIndex] : nullptr;
}

void SLeoGraphNodeWidget::Construct(const FArguments& InArgs, int32 InNodeIndex,
	const TWeakObjectPtr<ULeoScenarioGraph>& InGraph)
{
	NodeIndex = InNodeIndex;
	Graph = InGraph;
	OnSelected = InArgs._OnSelected;
	OnMoved = InArgs._OnMoved;
	OnMovedVisual = InArgs._OnMovedVisual;
	RefreshLook();
}

void SLeoGraphNodeWidget::RefreshLook()
{
	const FLeoScenarioNode* N = LookupNode();
	if (!N) { return; }
	FString Label = N->Id.ToString() + TEXT("\n") + NodeTypeName(static_cast<uint8>(N->Type));
	if (N->Type == ELeoScenarioNodeType::Chapter && !N->Chapter.IsNone())
	{
		Label += TEXT(" · ") + N->Chapter.ToString();
	}
	else if (N->Type == ELeoScenarioNodeType::Ending && !N->EndingId.IsNone())
	{
		Label += TEXT(" · ") + N->EndingId.ToString();
	}
	const ULeoScenarioGraph* G = Graph.Get();
	if (G && G->EntryNode == N->Id) { Label += TEXT("  ◀入口"); }
	SetToolTipText(FText::FromString(N->Caption.ToString()));

	const FLinearColor Base = NodeColor(static_cast<uint8>(N->Type));
	const FLinearColor Border = bSelected ? FLinearColor(1.f, 0.85f, 0.2f)
		: (bLiveHighlighted ? FLinearColor(0.2f, 1.f, 0.3f) : Base * 1.7f);
	const bool bHasOutput = N->Type != ELeoScenarioNodeType::Ending; // Ending 无出边 → 无输出 pin
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.Node.Body")))
			.BorderBackgroundColor(Border)
			.Padding(6)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ColorAndOpacity(FLinearColor::White)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0, 0, -8, 0)
		[
			SNew(SBox).WidthOverride(12).HeightOverride(12).Visibility(bHasOutput ? EVisibility::Visible : EVisibility::Collapsed)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Graph.Pin.NodeBody")))
				.BorderBackgroundColor(FLinearColor(1.f, 0.75f, 0.2f))
			]
		]
	];
}

void SLeoGraphNodeWidget::SetCanvas(const TSharedPtr<SLeoGraphCanvas>& InCanvas)
{
	WeakCanvas = InCanvas;
}

void SLeoGraphNodeWidget::NotifyWireDragStart(const FVector2f& ScreenPos)
{
	if (const TSharedPtr<SLeoGraphCanvas> C = WeakCanvas.Pin()) { C->HandleWireDragStart(NodeIndex, ScreenPos); }
}

void SLeoGraphNodeWidget::NotifyWireDragMove(const FVector2f& ScreenPos)
{
	if (const TSharedPtr<SLeoGraphCanvas> C = WeakCanvas.Pin()) { C->HandleWireDragMove(ScreenPos); }
}

void SLeoGraphNodeWidget::NotifyWireDragEnd(const FVector2f& ScreenPos)
{
	if (const TSharedPtr<SLeoGraphCanvas> C = WeakCanvas.Pin()) { C->HandleWireDragEnd(ScreenPos); }
}

void SLeoGraphNodeWidget::SetLiveHighlighted(FName LiveNodeId)
{
	const FLeoScenarioNode* N = LookupNode();
	const bool bNew = N && N->Id == LiveNodeId;
	if (bNew != bLiveHighlighted)
	{
		bLiveHighlighted = bNew;
		RefreshLook();
	}
}

FReply SLeoGraphNodeWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FLeoScenarioNode* N = LookupNode();
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D Size = MyGeometry.GetLocalSize();
		// 输出 pin 热区：右缘中点附近（Ending 无输出）
		const bool bPinHit = N && N->Type != ELeoScenarioNodeType::Ending
			&& Local.X >= Size.X - 18.f && FMath::Abs(Local.Y - Size.Y * 0.5f) <= 12.f;
		if (bPinHit)
		{
			NotifyWireDragStart(FVector2f(MouseEvent.GetScreenSpacePosition()));
			return FReply::Handled().CaptureMouse(AsShared());
		}
		if (N)
		{
			DragStartScreen = MouseEvent.GetScreenSpacePosition();
			DragStartPos = N->EditorPos;
		}
		bDragging = false;
		OnSelected.ExecuteIfBound();
		return FReply::Handled().CaptureMouse(AsShared());
	}
	return FReply::Unhandled();
}

FReply SLeoGraphNodeWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (HasMouseCapture())
	{
		if (bWireDragging)
		{
			bWireDragging = false;
			NotifyWireDragEnd(FVector2f(MouseEvent.GetScreenSpacePosition()));
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (bDragging)
		{
			if (const FLeoScenarioNode* N = LookupNode())
			{
				OnMoved.ExecuteIfBound(NodeIndex, N->EditorPos); // 拖动结束才写回（Modify 时机）
			}
		}
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SLeoGraphNodeWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (HasMouseCapture() && MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		if (bWireDragging)
		{
			NotifyWireDragMove(FVector2f(MouseEvent.GetScreenSpacePosition()));
			return FReply::Handled();
		}
		FLeoScenarioNode* N = const_cast<FLeoScenarioNode*>(LookupNode());
		if (N)
		{
			bDragging = true;
			// 屏幕差值换算（几何含面板缩放，免疫节点自身移动）
			const FVector2D ScreenDelta = MouseEvent.GetScreenSpacePosition() - DragStartScreen;
			const FVector2D NewPos = DragStartPos + ScreenDelta / FMath::Max(MyGeometry.Scale, 0.01f);
			const FVector2D Clamped(FMath::Max(0.f, NewPos.X), FMath::Max(0.f, NewPos.Y));
			if (!Clamped.Equals(N->EditorPos, 0.1f))
			{
				N->EditorPos = FVector2D(FMath::GridSnap(Clamped.X, 2.f), FMath::GridSnap(Clamped.Y, 2.f));
				OnMovedVisual.ExecuteIfBound(); // 请求面板重排（连线跟手）
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ---- SLeoGraphCanvas ----

void SLeoGraphCanvas::Construct(const FArguments& InArgs)
{
	Graph = InArgs._Graph;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	OnNodeMoved = InArgs._OnNodeMoved;
	OnContextMenuRequested = InArgs._OnContextMenuRequested;
	OnBackgroundClick = InArgs._OnBackgroundClick;
	OnEdgeSelectionChanged = InArgs._OnEdgeSelectionChanged;
	OnConnectRequested = InArgs._OnConnectRequested;
	OnDeleteEdgeRequested = InArgs._OnDeleteEdgeRequested;
	OnDeleteNodeRequested = InArgs._OnDeleteNodeRequested;
	SetClipping(EWidgetClipping::ClipToBounds);
	RebuildNodes();
}

SLeoGraphCanvas::FScopedWidgetSlotArguments SLeoGraphCanvas::AddSlot()
{
	return FScopedWidgetSlotArguments{MakeUnique<FLeoCanvasSlot>(), Children, INDEX_NONE};
}

void SLeoGraphCanvas::RebuildNodes()
{
	Children.Empty();
	NodeWidgets.Reset();

	ULeoScenarioGraph* G = Graph.Get();
	if (!G) { return; }
	for (int32 i = 0; i < G->Nodes.Num(); ++i)
	{
		const TSharedRef<SLeoGraphNodeWidget> Node = SNew(SLeoGraphNodeWidget, i, Graph)
			.OnSelected_Lambda([this, i] { SelectNode(i); })
			.OnMoved(OnNodeMoved)
			.OnMovedVisual_Lambda([this]
			{
				Invalidate(EInvalidateWidgetReason::Layout); // EditorPos 已变，重排让连线跟手
			});
		Node->SetCanvas(StaticCastSharedRef<SLeoGraphCanvas>(AsShared())); // SWidget::AsShared 返回基类引用
		AddSlot()
			[
				Node
			];
		NodeWidgets.Add(Node);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SLeoGraphCanvas::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	// 仿 SNodePanel::ArrangeChildNodes：位置与缩放在布局层解决（滚动/命中/缩放天然正确）
	const ULeoScenarioGraph* G = Graph.Get();
	if (!G) { return; }
	for (int32 i = 0; i < NodeWidgets.Num() && i < G->Nodes.Num(); ++i)
	{
		const TSharedRef<SWidget> Widget = NodeWidgets[i].ToSharedRef();
		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(
			Widget,
			G->Nodes[i].EditorPos - ViewOffset,
			Widget->GetDesiredSize(),
			Zoom));
	}
}

FVector2D SLeoGraphCanvas::ComputeDesiredSize(float) const
{
	FVector2D Max(-FLT_MAX, -FLT_MAX);
	if (const ULeoScenarioGraph* G = Graph.Get())
	{
		for (const FLeoScenarioNode& N : G->Nodes)
		{
			Max.X = FMath::Max(Max.X, N.EditorPos.X + LeoGraphCanvasConst::NodeW);
			Max.Y = FMath::Max(Max.Y, N.EditorPos.Y + LeoGraphCanvasConst::NodeH);
		}
	}
	if (Max.X < 0.f) { return FVector2D(400.f, 300.f); }
	return (Max + FVector2D(LeoGraphCanvasConst::CanvasPadding, LeoGraphCanvasConst::CanvasPadding)) * Zoom;
}

int32 SLeoGraphCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 连线画在节点之下：先画线（本层与箭头层），再让基类画子节点（上层）
	const ULeoScenarioGraph* G = Graph.Get();
	if (G && NodeWidgets.Num() == G->Nodes.Num())
	{
		const FSlateBrush* ArrowBrush = FAppStyle::GetBrush(TEXT("Graph.Arrow"));
		const FVector2f ArrowSize = ArrowBrush ? FVector2f(ArrowBrush->ImageSize.X * 0.5f, ArrowBrush->ImageSize.Y * 0.5f) : FVector2f(8.f, 8.f);

		// 节点几何 → 绝对锚点（引擎 FConnectionDrawingPolicy 同法）
		TArray<FVector2f> RightMid, LeftMid;
		RightMid.SetNumUninitialized(G->Nodes.Num());
		LeftMid.SetNumUninitialized(G->Nodes.Num());
		for (int32 i = 0; i < NodeWidgets.Num(); ++i)
		{
			const FGeometry& Geom = NodeWidgets[i]->GetTickSpaceGeometry();
			const FVector2f Abs = Geom.GetAbsolutePosition();
			const FVector2f Size = Geom.GetLocalSize() * Geom.Scale;
			RightMid[i] = Abs + FVector2f(Size.X, Size.Y * 0.5f);
			LeftMid[i] = Abs + FVector2f(0.f, Size.Y * 0.5f);
		}

		for (int32 From = 0; From < G->Nodes.Num(); ++From)
		{
			for (int32 Ei = 0; Ei < G->Nodes[From].Edges.Num(); ++Ei)
			{
				const FLeoScenarioEdge& E = G->Nodes[From].Edges[Ei];
				int32 To = INDEX_NONE;
				for (int32 j = 0; j < G->Nodes.Num(); ++j)
				{
					if (G->Nodes[j].Id == E.To) { To = j; break; }
				}
				const FVector2f A = RightMid[From];
				const FVector2f B = (To != INDEX_NONE ? LeftMid[To] : A + FVector2f(90.f, 60.f)) - FVector2f(ArrowSize.X * 0.5f, 0.f);

				const bool bSel = (Selection.IsEdge() && Selection.NodeIndex == From && Selection.EdgeIndex == Ei)
					|| (Selection.IsNode() && Selection.NodeIndex == From);
				const FLinearColor Color = bSel ? FLinearColor(1.f, 0.8f, 0.1f) : FLinearColor(0.55f, 0.55f, 0.62f);

				// 三次样条（引擎绘图原语 + 钳制水平切线）
				const FVector2f Tangent = ComputeTangent(A, B);
				FSlateDrawElement::MakeDrawSpaceSpline(OutDrawElements, LayerId,
					A, Tangent, B, Tangent, bSel ? 3.f : 1.5f, ESlateDrawEffect::None, Color);

				// 箭头：贴终点、按线色染色（引擎同法）
				if (ArrowBrush)
				{
					FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
						FPaintGeometry(B - ArrowSize * 0.5f, ArrowSize * 2.f, 1.f),
						ArrowBrush, ESlateDrawEffect::None, Color);
				}
			}
		}
	}

	// 预览线：从源节点右中拖到光标
	if (bDraggingWire && NodeWidgets.IsValidIndex(WireFromNode) && NodeWidgets[WireFromNode].IsValid())
	{
		const FGeometry& G1 = NodeWidgets[WireFromNode]->GetTickSpaceGeometry();
		const FVector2f A = G1.GetAbsolutePosition() + FVector2f(G1.GetLocalSize().X * G1.Scale, G1.GetLocalSize().Y * G1.Scale * 0.5f);
		const FVector2f B = WireCursorAbs;
		const FVector2f T = ComputeTangent(A, B);
		FSlateDrawElement::MakeDrawSpaceSpline(OutDrawElements, LayerId,
			A, T, B, T, 2.f, ESlateDrawEffect::None, FLinearColor(1.f, 0.75f, 0.2f));
	}

	return SPanel::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId + 2, InWidgetStyle, bParentEnabled && IsEnabled());
}

FVector2f SLeoGraphCanvas::ComputeTangent(const FVector2f& Start, const FVector2f& End)
{
	// 引擎式：水平差钳制张力（蓝图导线观感，正向/回折皆不穿节点）
	const FVector2f Delta = End - Start;
	const bool bForward = Delta.X >= 0.f;
	const float Tension = FMath::Min(FMath::Abs(Delta.X), 160.f) * 0.5f;
	return FVector2f(bForward ? Tension : -Tension, 0.f);
}

void SLeoGraphCanvas::SelectEdge(int32 FromIndex, int32 EdgeIdx)
{
	FLeoGraphSelection Sel;
	Sel.Kind = FLeoGraphSelection::EKind::Edge;
	Sel.NodeIndex = FromIndex;
	Sel.EdgeIndex = EdgeIdx;
	SetSelection(Sel);
	OnEdgeSelectionChanged.ExecuteIfBound(FromIndex, EdgeIdx);
}

void SLeoGraphCanvas::HandleWireDragStart(int32 FromIndex, const FVector2f& ScreenPos)
{
	bDraggingWire = true;
	WireFromNode = FromIndex;
	WireCursorAbs = ScreenPos;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SLeoGraphCanvas::HandleWireDragMove(const FVector2f& ScreenPos)
{
	WireCursorAbs = ScreenPos;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SLeoGraphCanvas::HandleWireDragEnd(const FVector2f& ScreenPos)
{
	if (!bDraggingWire) { return; }
	bDraggingWire = false;
	WireCursorAbs = ScreenPos;
	// 落点命中：遍历节点几何，含光标者为目标（不含源自身）
	const ULeoScenarioGraph* G = Graph.Get();
	int32 Target = INDEX_NONE;
	if (G)
	{
		for (int32 i = 0; i < NodeWidgets.Num() && i < G->Nodes.Num(); ++i)
		{
			if (i == WireFromNode || !NodeWidgets[i].IsValid()) { continue; }
			const FGeometry& Geom = NodeWidgets[i]->GetTickSpaceGeometry();
			const FSlateRect AbsRect(Geom.GetAbsolutePosition(),
				Geom.GetAbsolutePosition() + Geom.GetLocalSize() * Geom.Scale);
			if (AbsRect.ContainsPoint(FVector2f(ScreenPos))) { Target = i; break; }
		}
	}
	if (Target != INDEX_NONE && OnConnectRequested.IsBound())
	{
		OnConnectRequested.Execute(WireFromNode, Target);
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SLeoGraphCanvas::HitTestEdge(const FVector2f& AbsPoint, int32& OutFrom, int32& OutEdge) const
{
	OutFrom = INDEX_NONE;
	OutEdge = INDEX_NONE;
	const ULeoScenarioGraph* G = Graph.Get();
	if (!G || NodeWidgets.Num() != G->Nodes.Num()) { return; }
	for (int32 From = 0; From < G->Nodes.Num(); ++From)
	{
		for (int32 Ei = 0; Ei < G->Nodes[From].Edges.Num(); ++Ei)
		{
			int32 To = INDEX_NONE;
			for (int32 j = 0; j < G->Nodes.Num(); ++j)
			{
				if (G->Nodes[j].Id == G->Nodes[From].Edges[Ei].To) { To = j; break; }
			}
			if (To == INDEX_NONE || !NodeWidgets[From].IsValid() || !NodeWidgets[To].IsValid()) { continue; }
			const FGeometry& G1 = NodeWidgets[From]->GetTickSpaceGeometry();
			const FGeometry& G2 = NodeWidgets[To]->GetTickSpaceGeometry();
			const FVector2f A = G1.GetAbsolutePosition() + FVector2f(G1.GetLocalSize().X * G1.Scale, G1.GetLocalSize().Y * G1.Scale * 0.5f);
			const FVector2f B = G2.GetAbsolutePosition() + FVector2f(0.f, G2.GetLocalSize().Y * G2.Scale * 0.5f);
			const FVector2f T = ComputeTangent(A, B);
			// 三次贝塞尔采样命中（16 段，阈值 8px）
			for (int32 K = 0; K <= 16; ++K)
			{
				const float U = static_cast<float>(K) / 16.f, V = 1.f - U;
				const FVector2f P = A * (V * V * V) + (A + T) * (3.f * V * V * U) + (B - T) * (3.f * V * U * U) + B * (U * U * U);
				if (FVector2f::Distance(P, AbsPoint) < 8.f)
				{
					OutFrom = From;
					OutEdge = Ei;
					return;
				}
			}
		}
	}
}

void SLeoGraphCanvas::SelectNode(int32 Index)
{
	FLeoGraphSelection Sel;
	Sel.Kind = FLeoGraphSelection::EKind::Node;
	Sel.NodeIndex = Index;
	SetSelection(Sel);
	OnSelectionChanged.ExecuteIfBound(Sel);
}

void SLeoGraphCanvas::SetSelection(const FLeoGraphSelection& InSel)
{
	Selection = InSel;
	for (int32 i = 0; i < NodeWidgets.Num(); ++i)
	{
		if (NodeWidgets[i].IsValid())
		{
			NodeWidgets[i]->SetSelected(Selection.IsNode() && Selection.NodeIndex == i);
			NodeWidgets[i]->RefreshLook();
		}
	}
}

void SLeoGraphCanvas::RefreshVisuals()
{
	for (const TSharedPtr<SLeoGraphNodeWidget>& W : NodeWidgets)
	{
		if (W.IsValid()) { W->RefreshLook(); }
	}
}

void SLeoGraphCanvas::SetLiveNode(FName LiveNodeId)
{
	for (const TSharedPtr<SLeoGraphNodeWidget>& W : NodeWidgets)
	{
		if (W.IsValid()) { W->SetLiveHighlighted(LiveNodeId); }
	}
}

FVector2D SLeoGraphCanvas::LocalToGraph(const FVector2D& LocalPos) const
{
	return LocalPos / FMath::Max(Zoom, 0.01f) + ViewOffset;
}

FReply SLeoGraphCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bPanning = true;
		PanStartScreen = MouseEvent.GetScreenSpacePosition();
		PanStartOffset = ViewOffset;
		return FReply::Handled().CaptureMouse(AsShared());
	}
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// 右键空白处：报告图坐标+屏幕坐标，由 Toolkit 弹加节点菜单
		if (OnContextMenuRequested.IsBound())
		{
			const FVector2D GraphPos = LocalToGraph(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
			OnContextMenuRequested.Execute(GraphPos, MouseEvent.GetScreenSpacePosition());
		}
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		int32 HitFrom = INDEX_NONE, HitEdge = INDEX_NONE;
		HitTestEdge(FVector2f(MouseEvent.GetScreenSpacePosition()), HitFrom, HitEdge);
		if (HitFrom != INDEX_NONE)
		{
			SelectEdge(HitFrom, HitEdge); // 点中连线
		}
		else
		{
			OnBackgroundClick.ExecuteIfBound(); // 空白：清除选中
		}
		FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::Mouse);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SLeoGraphCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		if (Selection.IsEdge())
		{
			OnDeleteEdgeRequested.ExecuteIfBound();
			return FReply::Handled();
		}
		if (Selection.IsNode())
		{
			OnDeleteNodeRequested.ExecuteIfBound();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SLeoGraphCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (HasMouseCapture() && bPanning)
	{
		bPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SLeoGraphCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (HasMouseCapture() && bPanning)
	{
		const FVector2D ScreenDelta = MouseEvent.GetScreenSpacePosition() - PanStartScreen;
		ViewOffset = PanStartOffset - ScreenDelta / FMath::Max(Zoom, 0.01f);
		Invalidate(EInvalidateWidgetReason::Layout);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SLeoGraphCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// 以光标为锚缩放：光标下的图坐标缩放前后保持不动
	const float OldZoom = Zoom;
	const float WheelDelta = MouseEvent.GetWheelDelta();
	Zoom = FMath::Clamp(Zoom * (1.f + WheelDelta * 0.1f), 0.25f, 2.5f);
	if (!FMath::IsNearlyEqual(OldZoom, Zoom))
	{
		const FVector2D LocalCursor = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D AnchorGraph = LocalToGraph(LocalCursor);
		// 令 (AnchorGraph - ViewOffset') * Zoom' = LocalCursor
		ViewOffset = AnchorGraph - LocalCursor / Zoom;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	return FReply::Handled();
}
