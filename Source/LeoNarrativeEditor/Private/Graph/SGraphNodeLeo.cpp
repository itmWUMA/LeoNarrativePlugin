#include "Graph/SGraphNodeLeo.h"

#include "EdGraph/EdGraphPin.h"
#include "Graph/LeoEdGraph.h"
#include "Graph/LeoEdGraphNodes.h"
#include "Graph/LeoGraphMirror.h"
#include "SGraphPanel.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphNodeLeo"

/////////////////////////////////////////////////////
// SGraphPinLeo（细横条，SGraphPinAI 同款）

void SGraphPinLeo::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	this->SetCursor(EMouseCursor::Default);

	bShowLabel = true;

	GraphPinObj = InPin;
	check(GraphPinObj != NULL);

	const UEdGraphSchema* Schema = GraphPinObj->GetSchema();
	check(Schema);

	SBorder::Construct(SBorder::FArguments()
		.BorderImage(this, &SGraphPinLeo::GetPinBorder)
		.BorderBackgroundColor(this, &SGraphPinLeo::GetPinColor)
		.OnMouseButtonDown(this, &SGraphPinLeo::OnPinMouseDown) // 从 pin 拖出连线（FDragConnection 入口）
		.Cursor(this, &SGraphPinLeo::GetPinCursor)
		.Padding(FMargin(10.0f))
	);
}

TSharedRef<SWidget> SGraphPinLeo::GetDefaultValueWidget()
{
	return SNew(STextBlock);
}

const FSlateBrush* SGraphPinLeo::GetPinBorder() const
{
	return FAppStyle::GetBrush(TEXT("Graph.StateNode.Body"));
}

FSlateColor SGraphPinLeo::GetPinColor() const
{
	return FSlateColor(IsHovered() ? FLinearColor(1.0f, 0.7f, 0.0f) : FLinearColor(0.02f, 0.02f, 0.02f));
}

/////////////////////////////////////////////////////
// SGraphNodeLeo（行为树卡片）

void SGraphNodeLeo::Construct(const FArguments& InArgs, ULeoEdGraphNode* InNode)
{
	SetCursor(EMouseCursor::CardinalCross);

	GraphNode = InNode;
	UpdateGraphNode();
}

void SGraphNodeLeo::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	// 刷新已构建的节点前先重置将要暴露的变量
	RightNodeBox.Reset();
	LeftNodeBox.Reset();
	OutputPinBox.Reset();

	TSharedPtr<SErrorText> ErrorText;

	// BT 卡片内边距（非装饰器/服务节点用 8）
	const FMargin NodePadding(8.0f);

	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.StateNode.Body"))
			.Padding(0.0f)
			.BorderBackgroundColor(this, &SGraphNodeLeo::GetBorderBackgroundColor)
			[
				SNew(SOverlay)

				// pin 区 + 卡片主体
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SVerticalBox)

					// 输入 pin 区（顶部）
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.MinDesiredHeight(NodePadding.Top)
						[
							SAssignNew(LeftNodeBox, SVerticalBox)
						]
					]

					// 标题区
					+ SVerticalBox::Slot()
					.Padding(FMargin(NodePadding.Left, 0.0f, NodePadding.Right, 0.0f))
					[
						SAssignNew(NodeBody, SBorder)
						.BorderImage(FAppStyle::GetBrush("BTEditor.Graph.BTNode.Body"))
						.BorderBackgroundColor(this, &SGraphNodeLeo::GetBackgroundColor)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.Visibility(EVisibility::SelfHitTestInvisible)
						[
							SNew(SVerticalBox)

							// 标题行：错误角标 + 图标 + 内联可编辑标题
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									// 弹出错误消息
									SAssignNew(ErrorText, SErrorText)
									.BackgroundColor(this, &SGraphNodeLeo::GetErrorColor)
									.ToolTipText(this, &SGraphNodeLeo::GetErrorMsgToolTip)
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(SImage)
									.Image(this, &SGraphNodeLeo::GetNameIcon)
								]

								+ SHorizontalBox::Slot()
								.Padding(FMargin(4.0f, 0.0f, 4.0f, 0.0f))
								.VAlign(VAlign_Center)
								[
									SAssignNew(InlineEditableText, SInlineEditableTextBlock)
									.Style(FAppStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText")
									.Text(this, &SGraphNodeLeo::GetEditableTitleText)
									.OnVerifyTextChanged(this, &SGraphNodeLeo::OnVerifyNameTextChanged)
									.OnTextCommitted(this, &SGraphNodeLeo::OnNameTextCommited)
									.IsReadOnly(this, &SGraphNodeLeo::IsNameReadOnly)
									.IsSelected(this, &SGraphNodeLeo::IsSelectedForRenaming)
								]
							]

							// 副标题：Id + 类型参数（低 LOD 折叠）
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(this, &SGraphNodeLeo::GetDescriptionText)
								.Visibility(this, &SGraphNodeLeo::GetDescriptionVisibility)
								.TextStyle(FAppStyle::Get(), "SmallText")
							]
						]
					]

					// 输出 pin 区（底部）
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.MinDesiredHeight(NodePadding.Bottom)
						[
							SAssignNew(RightNodeBox, SVerticalBox)
							+ SVerticalBox::Slot()
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							.Padding(20.0f, 0.0f)
							.FillHeight(1.0f)
							[
								SAssignNew(OutputPinBox, SHorizontalBox)
							]
						]
					]
				]
			]
		];

	ErrorReporting = ErrorText;
	ErrorReporting->SetError(ErrorMsg);
	CreatePinWidgets();
}

void SGraphNodeLeo::CreatePinWidgets()
{
	ULeoEdGraphNode* LeoNode = CastChecked<ULeoEdGraphNode>(GraphNode);
	for (int32 PinIdx = 0; PinIdx < LeoNode->Pins.Num(); ++PinIdx)
	{
		UEdGraphPin* MyPin = LeoNode->Pins[PinIdx];
		if (!MyPin->bHidden)
		{
			TSharedPtr<SGraphPin> NewPin = SNew(SGraphPinLeo, MyPin);
			AddPin(NewPin);
		}
	}
}

void SGraphNodeLeo::AddPin(const TSharedPtr<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));

	if (PinToAdd->GetDirection() == EEdGraphPinDirection::EGPD_Input)
	{
		// 输入 = 顶部通宽细条
		LeftNodeBox->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillHeight(1.0f)
			[
				PinToAdd.ToSharedRef()
			];
		InputPins.Add(PinToAdd.ToSharedRef());
	}
	else
	{
		// 输出 = 底部满宽细条（整条都是拖线热区）
		OutputPinBox->AddSlot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			[
				PinToAdd.ToSharedRef()
			];
		OutputPins.Add(PinToAdd.ToSharedRef());
	}
}

void SGraphNodeLeo::MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
	// 记录拖拽活动时间：Mirror 轮询据此在拖拽结束后一次性提交位置（恰好一个事务）
	if (const ULeoEdGraph* Owner = Cast<ULeoEdGraph>(GraphNode ? GraphNode->GetGraph() : nullptr))
	{
		if (FLeoGraphMirror* Mirror = Owner->Mirror)
		{
			Mirror->NoteMoveActivity();
		}
	}
}

FSlateColor SGraphNodeLeo::GetBorderBackgroundColor() const
{
	const ULeoEdGraphNode* LeoNode = Cast<ULeoEdGraphNode>(GraphNode);
	if (LeoNode && LeoNode->bLeoActive)
	{
		return FLinearColor(1.0f, 1.0f, 0.0f); // NodeBorder::ActiveDebugging
	}
	TSharedPtr<SGraphPanel> Panel = GetOwnerPanel();
	const bool bSelected = Panel.IsValid() && Panel->SelectionManager.IsNodeSelected(GraphNode);
	return bSelected ? FLinearColor(1.0f, 0.08f, 0.08f)   // NodeBorder::Selected
	                 : FLinearColor(0.08f, 0.08f, 0.08f);  // NodeBorder::Inactive
}

FSlateColor SGraphNodeLeo::GetBackgroundColor() const
{
	const ULeoEdGraphNode* LeoNode = Cast<ULeoEdGraphNode>(GraphNode);
	return LeoNode ? LeoNode->GetBodyColor() : FLinearColor(0.15f, 0.15f, 0.15f);
}

const FSlateBrush* SGraphNodeLeo::GetNameIcon() const
{
	// BT 默认节点图标（行为树卡片左上角小图标同款）
	return FAppStyle::GetBrush("BTEditor.Graph.BTNode.Icon");
}

EVisibility SGraphNodeLeo::GetDescriptionVisibility() const
{
	// 缩放过小时折叠副标题（BT 同款 LOD 行为）
	TSharedPtr<SGraphPanel> MyOwnerPanel = GetOwnerPanel();
	return (!MyOwnerPanel.IsValid() || MyOwnerPanel->GetCurrentLOD() > EGraphRenderingLOD::LowDetail) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SGraphNodeLeo::GetEditableTitleText() const
{
	const ULeoEdGraphNode* LeoNode = Cast<ULeoEdGraphNode>(GraphNode);
	if (!LeoNode) { return FText::GetEmpty(); }
	if (LeoNode->IsA<ULeoEdGraphNode_Entry>())
	{
		return LOCTEXT("EntryTitle", "入口");
	}
	if (const FLeoScenarioNode* N = LeoNode->FindAssetNode())
	{
		return !N->Caption.IsEmpty() ? N->Caption : FText::FromName(N->Id);
	}
	return FText::FromName(LeoNode->NodeId);
}

FText SGraphNodeLeo::GetDescriptionText() const
{
	const ULeoEdGraphNode* LeoNode = Cast<ULeoEdGraphNode>(GraphNode);
	if (!LeoNode) { return FText::GetEmpty(); }
	if (LeoNode->IsA<ULeoEdGraphNode_Entry>())
	{
		const ULeoScenarioGraph* G = LeoNode->GetGraph() ? Cast<ULeoEdGraph>(LeoNode->GetGraph())->GetLeoGraph() : nullptr;
		if (G && !G->EntryNode.IsNone())
		{
			return FText::Format(LOCTEXT("EntryTo", "→ {0}"), FText::FromName(G->EntryNode));
		}
		return LOCTEXT("EntryUnlinked", "→（拖线到节点设为入口）");
	}
	if (const FLeoScenarioNode* N = LeoNode->FindAssetNode())
	{
		FString Sub = N->Id.ToString();
		switch (N->Type)
		{
		case ELeoScenarioNodeType::Chapter:
			Sub += FString::Printf(TEXT(" · %s"), *N->Chapter.ToString());
			if (!N->Label.IsNone()) { Sub += FString::Printf(TEXT(" @%s"), *N->Label.ToString()); }
			break;
		case ELeoScenarioNodeType::Ending:
			if (!N->EndingId.IsNone()) { Sub += FString::Printf(TEXT(" · %s"), *N->EndingId.ToString()); }
			break;
		case ELeoScenarioNodeType::Subgraph:
			Sub += FString::Printf(TEXT(" · %s"), *GetNameSafe(N->SubGraph));
			break;
		default: break;
		}
		return FText::FromString(Sub);
	}
	return FText::GetEmpty();
}

bool SGraphNodeLeo::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	// Caption 为自由文本，无需校验
	return true;
}

void SGraphNodeLeo::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{
	ULeoEdGraphNode* LeoNode = Cast<ULeoEdGraphNode>(GraphNode);
	if (LeoNode && !LeoNode->IsA<ULeoEdGraphNode_Entry>())
	{
		if (ULeoEdGraph* Owner = Cast<ULeoEdGraph>(LeoNode->GetGraph()))
		{
			if (FLeoGraphMirror* Mirror = Owner->Mirror)
			{
				Mirror->RenameCaption(LeoNode, InText);
			}
		}
	}
}

bool SGraphNodeLeo::IsNameReadOnly() const
{
	const ULeoEdGraphNode* LeoNode = Cast<ULeoEdGraphNode>(GraphNode);
	return !LeoNode || LeoNode->IsA<ULeoEdGraphNode_Entry>();
}

bool SGraphNodeLeo::IsSelectedForRenaming() const
{
	return IsSelectedExclusively();
}

#undef LOCTEXT_NAMESPACE
