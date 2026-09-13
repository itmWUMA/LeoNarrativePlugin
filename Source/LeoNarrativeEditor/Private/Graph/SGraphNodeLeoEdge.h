// 连线中点小图标：复刻 SGraphNodeAnimTransition（AnimationBlueprintEditor）。
// 二段布局：PositionBetweenTwoNodesWithOffset 把图标放在两端卡片锚点连线中点、法向抬高 24px，
// 同一对节点间的多条边沿方向排开；朝向随连线方向旋转。
// 点选图标 = 选中边（Delete 删）；线本体 hover 变橙由绘图策略负责。
#pragma once

#include "SGraphNode.h"

class ULeoEdGraphNode_Edge;

class SGraphNodeLeoEdge : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeLeoEdge) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ULeoEdGraphNode_Edge* InNode);

	// ---- SNodePanel::SNode ----
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	virtual bool RequiresSecondPassLayout() const override;
	virtual void PerformSecondPassLayout(const TMap< UObject*, TSharedRef<SNode> >& NodeToWidgetLookup) const override;
	// ---- end ----

	// ---- SGraphNode ----
	virtual void UpdateGraphNode() override;
	// ---- end ----

	// ---- SWidget ----
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	// ---- end ----

	// 连线颜色：hover 橙 / 激活去饱和橙（PIE·干跑）/ 默认白（状态机同款）
	static FSlateColor StaticGetTransitionColor(const ULeoEdGraphNode_Edge* EdgeNode, bool bIsHovered);

private:
	void PositionBetweenTwoNodesWithOffset(const FGeometry& StartGeom, const FGeometry& EndGeom, int32 NodeIndex, int32 MaxNodes) const;

	FSlateColor GetTransitionColor() const;
	const FSlateBrush* GetTransitionIconImage() const;

	// 中点下方的条件摘要标签（"无条件" / 条件缩略）——让边在画布上"开口说话"
	FString FetchRawCondition() const;
	FText GetConditionSummary() const;
	FSlateColor GetConditionColor() const;

	/** 前驱卡片 widget 缓存（hover 联动） */
	mutable TWeakPtr<SNode> PrevStateNodeWidgetPtr;
	/** 条件摘要缓存（脏检查：原文变更才重新编解码，避免每帧编译表达式） */
	mutable FString CachedRawCond;
	mutable FString CachedSummary;
};
