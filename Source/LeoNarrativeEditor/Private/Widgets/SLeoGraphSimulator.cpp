#include "Widgets/SLeoGraphSimulator.h"

#include "Data/LeoScenarioGraph.h"
#include "ScriptRuntime/LeoScriptBridge.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "LeoGraphSimulator"

SLeoGraphSimulator::~SLeoGraphSimulator() = default;

void SLeoGraphSimulator::Construct(const FArguments& InArgs)
{
	Graph = InArgs._Graph;
	GetSelectedNodeId = InArgs._GetSelectedNodeId;
	SimCtx = TStrongObjectPtr<ULeoGraphSimContext>(NewObject<ULeoGraphSimContext>(GetTransientPackage()));
	SimCtx->Global = NewObject<UNarrativeBlackboard>(SimCtx.Get());
	SimCtx->Local = NewObject<UNarrativeBlackboard>(SimCtx.Get());

	// 预置常用变量行（可改）
	VarRows.Add(MakeShared<FVarRow>(FVarRow{ TEXT("affection"), TEXT("0") }));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton).Text(LOCTEXT("Reset", "重置"))
				.OnClicked_Lambda([this] { Reset(); RefreshViews(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton).Text(LOCTEXT("Step", "单步"))
				.OnClicked_Lambda([this] { Step(); RefreshViews(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton).Text(LOCTEXT("RunToEnd", "跑到底"))
				.OnClicked_Lambda([this] { RunToEnd(); RefreshViews(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this] { return BuildStatusText(); }).AutoWrapText(true)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.4f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("VarsHeader", "沙箱变量（全局黑板）"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right)
						[
							SNew(SButton).Text(LOCTEXT("AddVar", "+ 变量"))
							.OnClicked_Lambda([this]
							{
								VarRows.Add(MakeShared<FVarRow>(FVarRow{ TEXT("var"), TEXT("0") }));
								BuildVarRows();
								return FReply::Handled();
							})
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(VarBox, SVerticalBox)
						]
					]
				]
			]
			+ SSplitter::Slot().Value(0.6f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4)
					[
						SNew(STextBlock).Text(LOCTEXT("EdgesHeader", "最近一次转移的出边条件"))
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
					[
						SAssignNew(ResultList, SListView<TSharedPtr<FString>>)
						.ListItemsSource(&ResultRows)
						.OnGenerateRow_Lambda([](TSharedPtr<FString> Row, const TSharedRef<STableViewBase>& Table)
						{
							return SNew(STableRow<TSharedPtr<FString>>, Table)
								.Content()
								[
									SNew(STextBlock).Text(FText::FromString(*Row))
								];
						})
					]
				]
			]
		]
	];
	BuildVarRows();
	ApplyVarsToBoard();
	Reset();
}

void SLeoGraphSimulator::NotifyGraphChanged()
{
	Reset();
}

void SLeoGraphSimulator::Reset()
{
	ApplyVarsToBoard();
	Stack.Reset();
	bInsideChapter = false;
	bDone = false;
	LastEnding = NAME_None;
	StatusExtra.Reset();
	LastEdgeResults.Reset();
	ExprCache.Reset();

	ULeoScenarioGraph* G = Graph.Get();
	if (!G) { return; }
	FName Start = G->EntryNode;
	if (GetSelectedNodeId.IsBound()) { Start = GetSelectedNodeId.Execute(); }
	if (Start.IsNone()) { Start = G->EntryNode; }
	FSimFrame Frame;
	Frame.Graph = G;
	Frame.NodeId = Start;
	Stack.Add(MoveTemp(Frame));
	RunNode(Start);
}

void SLeoGraphSimulator::ApplyVarsToBoard()
{
	if (!SimCtx || !SimCtx->Global) { return; }
	TMap<FName, leo::FLeoValue> Vars;
	for (const TSharedRef<FVarRow>& Row : VarRows)
	{
		leo::FLeoDiag D;
		leo::FLeoExprPtr Expr = LeoBridge::CompileExpr(Row->Expr, D);
		if (!Expr) { continue; }
		leo::FLeoValue V;
		leo::ELeoDiag Code; std::string Msg;
		if (LeoBridge::EvalExpr(Expr, leo::FLeoVarResolver(), V, Code, Msg))
		{
			Vars.Add(FName(*Row->Key), V);
		}
	}
	SimCtx->Global->RestoreFromMap(Vars);
}

const FLeoScenarioNode* SLeoGraphSimulator::TopNode() const
{
	if (Stack.IsEmpty()) { return nullptr; }
	const ULeoScenarioGraph* G = Stack.Last().Graph.Get();
	return G ? G->FindNode(Stack.Last().NodeId) : nullptr;
}

FString SLeoGraphSimulator::DescribeNode(const FLeoScenarioNode& N) const
{
	const TCHAR* T = TEXT("?");
	switch (N.Type)
	{
	case ELeoScenarioNodeType::Chapter:  T = TEXT("Chapter"); break;
	case ELeoScenarioNodeType::Branch:   T = TEXT("Branch"); break;
	case ELeoScenarioNodeType::Ending:   T = TEXT("Ending"); break;
	case ELeoScenarioNodeType::Subgraph: T = TEXT("Subgraph"); break;
	default: break;
	}
	return FString::Printf(TEXT("%s[%s]"), T, *N.Id.ToString());
}

void SLeoGraphSimulator::RunNode(FName NodeId)
{
	if (Stack.IsEmpty()) { return; }
	const ULeoScenarioGraph* G = Stack.Last().Graph.Get();
	if (!G) { return; }
	const FLeoScenarioNode* N = G->FindNode(NodeId);
	if (!N)
	{
		StatusExtra = FString::Printf(TEXT("节点不存在: %s"), *NodeId.ToString());
		Finish(NAME_None);
		return;
	}
	Stack.Last().NodeId = NodeId;

	switch (N->Type)
	{
	case ELeoScenarioNodeType::Chapter:
		bInsideChapter = true; // 下一步 = 模拟"章末"做转移
		StatusExtra = FString::Printf(TEXT("将进入章节 %s%s"),
			*N->Chapter.ToString(), N->Label.IsNone() ? TEXT("") : *FString::Printf(TEXT("@%s"), *N->Label.ToString()));
		break;

	case ELeoScenarioNodeType::Branch:
		TransitionFromCurrent(); // 立即分流
		break;

	case ELeoScenarioNodeType::Subgraph:
	{
		ULeoScenarioGraph* Sub = N->SubGraph;
		if (!Sub || Stack.Num() >= 16)
		{
			StatusExtra = Sub ? TEXT("子图嵌套过深") : TEXT("Subgraph 未配置子图");
			Finish(NAME_None);
			return;
		}
		FSimFrame Frame;
		Frame.Graph = Sub;
		Frame.NodeId = Sub->EntryNode;
		Stack.Add(MoveTemp(Frame));
		RunNode(Sub->EntryNode);
		break;
	}

	case ELeoScenarioNodeType::Ending:
		if (Stack.Num() > 1)
		{
			Stack.Pop();          // 子图收束
			TransitionFromCurrent();
		}
		else
		{
			Finish(N->EndingId);
		}
		break;
	}
}

void SLeoGraphSimulator::TransitionFromCurrent()
{
	while (!Stack.IsEmpty())
	{
		const ULeoScenarioGraph* G = Stack.Last().Graph.Get();
		const FLeoScenarioNode* N = G ? G->FindNode(Stack.Last().NodeId) : nullptr;
		if (!N) { Finish(NAME_None); return; }

		// 展示每条出边的真假（编辑器模拟核心价值）
		LastEdgeResults.Reset();
		TArray<bool> Results;
		LeoGraphEval::EvaluateAllEdges(*N, SimCtx->Local, SimCtx->Global, ExprCache, Results);
		for (int32 i = 0; i < N->Edges.Num(); ++i)
		{
			const FString Cond = N->Edges[i].Condition.IsEmpty() ? TEXT("(恒真)") : N->Edges[i].Condition;
			LastEdgeResults.Add(TPair<FString, bool>(Cond + TEXT(" -> ") + N->Edges[i].To.ToString(), Results[i]));
		}

		bInsideChapter = false;
		const FName Next = LeoGraphEval::SelectEdge(*N, SimCtx->Local, SimCtx->Global, ExprCache);
		if (!Next.IsNone())
		{
			RunNode(Next);
			return;
		}
		if (Stack.Num() > 1)
		{
			Stack.Pop(); // 子图收束（无 Ending 也算）
			continue;
		}
		StatusExtra = TEXT("无满足条件的出边");
		Finish(NAME_None);
		return;
	}
}

void SLeoGraphSimulator::Step()
{
	if (bDone) { return; }
	if (Stack.IsEmpty()) { Reset(); return; }
	if (bInsideChapter)
	{
		TransitionFromCurrent(); // Chapter 的"章末"
	}
	else
	{
		const FLeoScenarioNode* N = TopNode();
		if (!N) { Finish(NAME_None); return; }
		// 已在节点上：Branch/Ending 在 Reset 已处理；这里主要覆盖 Chapter 之外的重入
		TransitionFromCurrent();
	}
}

void SLeoGraphSimulator::RunToEnd()
{
	int32 Guard = 0;
	while (!bDone && ++Guard < 1000)
	{
		Step();
	}
}

void SLeoGraphSimulator::Finish(FName EndingId)
{
	bDone = true;
	LastEnding = EndingId;
}

FText SLeoGraphSimulator::BuildStatusText() const
{
	if (bDone)
	{
		return FText::FromString(FString::Printf(TEXT("◆ 完结：结局 = %s"),
			LastEnding.IsNone() ? TEXT("(无)") : *LastEnding.ToString()));
	}
	const FLeoScenarioNode* N = TopNode();
	if (!N) { return FText::FromString(TEXT("（未开始，点重置）")); }
	return FText::FromString(FString::Printf(TEXT("当前 %s ｜ 子图深度 %d ｜ %s"),
		*DescribeNode(*N), Stack.Num(), *StatusExtra));
}

void SLeoGraphSimulator::RefreshViews()
{
	ResultRows.Reset();
	for (const TPair<FString, bool>& R : LastEdgeResults)
	{
		ResultRows.Add(MakeShared<FString>(FString::Printf(TEXT("%s  %s"),
			R.Value ? TEXT("[真]") : TEXT("[假]"), *R.Key)));
	}
	if (ResultRows.Num() == 0 && !bDone)
	{
		ResultRows.Add(MakeShared<FString>(TEXT("（转移后显示）")));
	}
	if (ResultList.IsValid()) { ResultList->RequestListRefresh(); }
}

void SLeoGraphSimulator::BuildVarRows()
{
	if (!VarBox.IsValid()) { return; }
	VarBox->ClearChildren();
	for (int32 i = 0; i < VarRows.Num(); ++i)
	{
		const TSharedRef<FVarRow> Row = VarRows[i];
		const int32 Idx = i;
		VarBox->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.38f)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(Row->Key))
				.OnTextChanged_Lambda([Row](const FText& T) { Row->Key = T.ToString(); })
			]
			+ SHorizontalBox::Slot().FillWidth(0.46f).Padding(2, 0)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromString(Row->Expr))
				.HintText(LOCTEXT("ExprHint", ".leo 表达式"))
				.OnTextChanged_Lambda([Row](const FText& T) { Row->Expr = T.ToString(); })
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type)
				{
					ApplyVarsToBoard(); // 提交后重建沙箱黑板（下次转移生效）
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).Text(FText::FromString(TEXT("×")))
				.OnClicked_Lambda([this, Idx]
				{
					if (VarRows.IsValidIndex(Idx)) { VarRows.RemoveAt(Idx); }
					BuildVarRows();
					ApplyVarsToBoard();
					return FReply::Handled();
				})
			]
		];
	}
}
