// Leo 节点卡片：结构逐段复刻 SGraphNode_BehaviorTree::UpdateGraphNode
// （BehaviorTreeEditor/Private/SGraphNode_BehaviorTree.cpp:261-582）。
// 外框 Graph.StateNode.Body + 输入pin区/标题区/输出pin区三段 + 内体 BTEditor.Graph.BTNode.Body，
// 内联可编辑标题 = Caption；副标题 = Id + 类型参数（LOD 折叠）；类型色 = BT 色板。
// pin 为细横条（复刻 SGraphPinAI）；Ending 无输出 pin（叶子，同 BT Task）。
#pragma once

#include "SGraphNode.h"
#include "SGraphPin.h"

class ULeoEdGraphNode;

// 细横条 pin（SGraphPinAI 同款：SBorder + Graph.StateNode.Body + Padding(10)，hover 黄）
class SGraphPinLeo : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinLeo) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

	const FSlateBrush* GetPinBorder() const;
	FSlateColor GetPinColor() const;
};

// 行为树卡片（普通节点 + 入口隧道共用；入口经 ULeoEdGraphNode_Entry 覆盖标题/体色）
class SGraphNodeLeo : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeLeo) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ULeoEdGraphNode* InNode);

	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;

protected:
	// 外框色：选中红（BT NodeBorder::Selected）/ 激活黄（ActiveDebugging）/ 默认暗（Inactive）
	FSlateColor GetBorderBackgroundColor() const;
	// 内体色：BT 色板按类型（ULeoEdGraphNode::GetBodyColor）
	FSlateColor GetBackgroundColor() const;
	const FSlateBrush* GetNameIcon() const;
	EVisibility GetDescriptionVisibility() const;
	FText GetEditableTitleText() const;
	FText GetDescriptionText() const;

	bool OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage);
	void OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo);
	bool IsNameReadOnly() const;
	bool IsSelectedForRenaming() const; // SGraphNode::IsSelectedExclusively 为 protected，经包装绑定

private:
	void AddPin(const TSharedPtr<SGraphPin>& PinToAdd);

	TSharedPtr<SBorder> NodeBody;
	TSharedPtr<SHorizontalBox> OutputPinBox;
};
