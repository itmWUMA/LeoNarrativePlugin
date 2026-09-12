// 编排图画布（引擎同源渲染层）：
// - 节点排布仿 SNodePanel：自定义 Panel 的 OnArrangeChildren 用 MakeChild(位置-视口偏移, 尺寸, 缩放)，
//   平移（中键拖）与缩放（滚轮）在布局层解决，滚动范围/命中测试天然正确；
// - 连线仿 FConnectionDrawingPolicy：锚点 = 源右中 → 目标左中，MakeDrawSpaceSpline 三次样条
//   （切线为引擎式钳制水平张力），箭头用 Graph.Arrow 笔刷贴终点染色；
// - 线层在节点层之下；右键空白处请求加节点（带图坐标）。
// 数据本体仍是 ULeoScenarioGraph 资产（无 EdGraph 镜像）。
#pragma once

#include "CoreMinimal.h"
#include "Data/LeoScenarioGraph.h"
#include "Delegates/Delegate.h"
#include "Layout/BasicLayoutWidgetSlot.h"
#include "Layout/Children.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SPanel.h"

class SLeoGraphNodeWidget;

// 选中项（节点或某节点的某条出边）
struct FLeoGraphSelection
{
	enum class EKind : uint8 { None, Node, Edge } Kind = EKind::None;
	int32 NodeIndex = INDEX_NONE;
	int32 EdgeIndex = INDEX_NONE;
	bool IsNode() const { return Kind == EKind::Node; }
	bool IsEdge() const { return Kind == EKind::Edge; }
};

// 画布事件（SLATE_EVENT 需要真委托类型）
DECLARE_DELEGATE_OneParam(FLeoGraphSelectionEvent, const FLeoGraphSelection&);
DECLARE_DELEGATE_TwoParams(FLeoGraphNodeMovedEvent, int32 /*NodeIndex*/, FVector2D /*NewPos*/);
DECLARE_DELEGATE_TwoParams(FLeoGraphContextMenuEvent, FVector2D /*图坐标*/, FVector2D /*屏幕坐标*/);
DECLARE_DELEGATE_TwoParams(FLeoGraphEdgeSelectionEvent, int32 /*源节点索引*/, int32 /*边索引*/);
DECLARE_DELEGATE_TwoParams(FLeoGraphConnectEvent, int32 /*源节点索引*/, int32 /*目标节点索引*/);

namespace LeoGraphCanvasConst
{
	FVector2D GetNodeSize();
	FLinearColor NodeColor(uint8 Type);
	const TCHAR* NodeTypeName(uint8 Type);
}

// 节点控件：按类型着色，可选中、可拖动（拖动改数据 + 通知面板重排）
class SLeoGraphNodeWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLeoGraphNodeWidget) {}
		SLATE_EVENT(FSimpleDelegate, OnSelected)
		SLATE_EVENT(FLeoGraphNodeMovedEvent, OnMoved)
		SLATE_EVENT(FSimpleDelegate, OnMovedVisual) // 拖动中：请求面板重排
	SLATE_END_ARGS()

	// 连线拖拽（由节点上报，画布画预览线并做落点命中）
	void NotifyWireDragStart(const FVector2f& ScreenPos);
	void NotifyWireDragMove(const FVector2f& ScreenPos);
	void NotifyWireDragEnd(const FVector2f& ScreenPos);
	void SetCanvas(const TSharedPtr<class SLeoGraphCanvas>& InCanvas);

	void Construct(const FArguments& InArgs, int32 InNodeIndex, const TWeakObjectPtr<ULeoScenarioGraph>& InGraph);
	void RefreshLook();
	void SetSelected(bool bIn) { bSelected = bIn; }
	void SetLiveHighlighted(FName LiveNodeId);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FVector2D ComputeDesiredSize(float) const override { return LeoGraphCanvasConst::GetNodeSize(); }

private:
	const struct FLeoScenarioNode* LookupNode() const;

	int32 NodeIndex = INDEX_NONE;
	TWeakObjectPtr<ULeoScenarioGraph> Graph;
	TWeakPtr<SLeoGraphCanvas> WeakCanvas;
	FSimpleDelegate OnSelected;
	FLeoGraphNodeMovedEvent OnMoved;
	FSimpleDelegate OnMovedVisual;
	bool bWireDragging = false;
	FVector2D DragStartScreen = FVector2D::ZeroVector;
	FVector2D DragStartPos = FVector2D::ZeroVector;
	bool bDragging = false;
	bool bSelected = false;
	bool bLiveHighlighted = false;
};

// 画布主体：自定义 Panel（排布 + 平移缩放 + 样条连线 + 右键加节点）
class SLeoGraphCanvas : public SPanel
{
	SLATE_DECLARE_WIDGET(SLeoGraphCanvas, SPanel)
public:
	SLATE_BEGIN_ARGS(SLeoGraphCanvas) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ULeoScenarioGraph>, Graph)
		SLATE_EVENT(FLeoGraphSelectionEvent, OnSelectionChanged)
		SLATE_EVENT(FLeoGraphNodeMovedEvent, OnNodeMoved) // 拖动结束（图 Modify 时机）
		SLATE_EVENT(FLeoGraphContextMenuEvent, OnContextMenuRequested) // 右键空白处（图坐标+屏幕坐标）
		SLATE_EVENT(FSimpleDelegate, OnBackgroundClick)   // 左键空白：清除选中
		SLATE_EVENT(FLeoGraphEdgeSelectionEvent, OnEdgeSelectionChanged) // 点击连线
		SLATE_EVENT(FLeoGraphConnectEvent, OnConnectRequested)           // 拖拽建边
		SLATE_EVENT(FSimpleDelegate, OnDeleteEdgeRequested)              // Delete：删选中边
		SLATE_EVENT(FSimpleDelegate, OnDeleteNodeRequested)              // Delete：删选中节点
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void RebuildNodes();                 // 结构变更（加删节点）后重建
	void SetSelection(const FLeoGraphSelection& InSel);
	void RefreshVisuals();               // 数据编辑后刷新节点外观
	void SetLiveNode(FName LiveNodeId);  // PIE 运行时高亮
	FVector2D LocalToGraph(const FVector2D& LocalPos) const;
	float GetZoom() const { return Zoom; }

	// ---- SPanel ----
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override { return &Children; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void SelectNode(int32 Index);
	void SelectEdge(int32 FromIndex, int32 EdgeIdx);
	// 连线拖拽：节点上报入口（内部状态 + 预览线 + 落点命中）
	void HandleWireDragStart(int32 FromIndex, const FVector2f& ScreenPos);
	void HandleWireDragMove(const FVector2f& ScreenPos);
	void HandleWireDragEnd(const FVector2f& ScreenPos);
	// 边命中：返回 (源节点索引, 边索引)；无命中 INDEX_NONE
	void HitTestEdge(const FVector2f& AbsPoint, int32& OutFrom, int32& OutEdge) const;
	friend class SLeoGraphNodeWidget;
	static FVector2f ComputeTangent(const FVector2f& Start, const FVector2f& End); // 引擎式钳制水平张力

	TWeakObjectPtr<ULeoScenarioGraph> Graph;
	TArray<TSharedPtr<SLeoGraphNodeWidget>> NodeWidgets;

	// 槽位（照 SOverlay：TPanelChildren + 基础槽）
	class FLeoCanvasSlot : public TBasicLayoutWidgetSlot<FLeoCanvasSlot>
	{
	public:
		FLeoCanvasSlot() : TBasicLayoutWidgetSlot<FLeoCanvasSlot>(HAlign_Fill, VAlign_Fill) {}
	};
	using FLeoCanvasSlots = TPanelChildren<FLeoCanvasSlot>;
	using FScopedWidgetSlotArguments = FLeoCanvasSlots::FScopedWidgetSlotArguments;
	FLeoCanvasSlots Children;
	FScopedWidgetSlotArguments AddSlot(); // 析构时提交（含 widget 绑定）

public:
	// TPanelChildren 无默认构造（需 owner），显式提供
	SLeoGraphCanvas() : Children(this) {}

	FVector2D ViewOffset = FVector2D(-64.f, -48.f); // 本地 = (图坐标 - ViewOffset)
	float Zoom = 1.f;
	bool bPanning = false;
	FVector2D PanStartScreen = FVector2D::ZeroVector;
	FVector2D PanStartOffset = FVector2D::ZeroVector;

	FLeoGraphSelection Selection;
	FLeoGraphSelectionEvent OnSelectionChanged;
	FLeoGraphNodeMovedEvent OnNodeMoved;
	FLeoGraphContextMenuEvent OnContextMenuRequested;
	FSimpleDelegate OnBackgroundClick;
	FLeoGraphEdgeSelectionEvent OnEdgeSelectionChanged;
	FLeoGraphConnectEvent OnConnectRequested;
	FSimpleDelegate OnDeleteEdgeRequested;
	FSimpleDelegate OnDeleteNodeRequested;

	// 连线拖拽状态（绝对屏幕坐标，预览线用）
	bool bDraggingWire = false;
	int32 WireFromNode = INDEX_NONE;
	FVector2f WireCursorAbs = FVector2f::ZeroVector;
};
