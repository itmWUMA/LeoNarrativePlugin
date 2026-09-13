// Leo 编排图连线绘制策略：逐函数复刻 FStateMachineConnectionDrawingPolicy
// （AnimationBlueprintEditor/Private/StateMachineConnectionDrawingPolicy.cpp）。
// 关键语义：
// - DetermineLinkGeometry 不用 pin 几何，直接换成两端「节点卡片」几何 → 锚点取
//   FindClosestPointOnGeom 最近边框点，父上子下时天然「底出顶进」（无需任何肘部代码）；
// - 近似直线（切线 = 单位方向向量）；双线偏移 6×Zoom；终点三角箭头；
// - hover 橙 / 默认白 / 激活（PIE·干跑）去饱和橙；拖线预览半透明；
// - hover 线端附近画 relink 手柄底圈并上报 SplineOverlapResult（Alt 断线等面板行为）。
#pragma once

#include "ConnectionDrawingPolicy.h"

class UEdGraph;

class FLeoGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
protected:
	UEdGraph* GraphObj;

	// 节点 → ArrangedChildren 下标（几何加速结构）
	TMap<UEdGraphNode*, int32> NodeWidgetMap;

public:
	// @Note FConnectionParams.bUserFlag2 = 拖线预览（半透明）

	FLeoGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor,
		const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

	// ---- FConnectionDrawingPolicy ----
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override;
	virtual void DetermineLinkGeometry(
		FArrangedChildren& ArrangedNodes,
		TSharedRef<SWidget>& OutputPinWidget,
		UEdGraphPin* OutputPin,
		UEdGraphPin* InputPin,
		/*out*/ FArrangedWidget*& StartWidgetGeometry,
		/*out*/ FArrangedWidget*& EndWidgetGeometry
	) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FVector2f& StartPoint, const FVector2f& EndPoint, const FConnectionParams& Params) override;
	virtual void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin) override;
	virtual FVector2f ComputeSplineTangent(const FVector2f& Start, const FVector2f& End) const override;
	// ---- end ----

protected:
	void Internal_DrawLineWithArrow(const FVector2f& StartAnchorPoint, const FVector2f& EndAnchorPoint, const FConnectionParams& Params);

	static constexpr float RelinkHandleHoverRadius = 10.0f;
};
