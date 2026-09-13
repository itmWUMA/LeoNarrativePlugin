#include "Widgets/SLeoGraphSimulator.h"

#include "Data/LeoScenarioGraph.h"
#include "LeoConditionCodec.h"
#include "LeoVariableHarvest.h"
#include "ScriptRuntime/LeoScriptBridge.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
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
	OnCurrentNodeEvent = InArgs._OnCurrentNodeChanged;
	SimCtx = TStrongObjectPtr<ULeoGraphSimContext>(NewObject<ULeoGraphSimContext>(GetTransientPackage()));
	SimCtx->Global = NewObject<UNarrativeBlackboard>(SimCtx.Get());
	SimCtx->Local = NewObject<UNarrativeBlackboard>(SimCtx.Get());

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
							SNew(STextBlock).Text(LOCTEXT("VarsHeader", "黑板键（脚本与图收割 · 值可编辑）"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right)
						[
							SNew(SButton).Text(LOCTEXT("AddVar", "+ 临时键"))
							.ToolTipText(LOCTEXT("AddVarTip", "收割集之外的变量（如游戏代码写入的），供模拟引用"))
							.OnClicked_Lambda([this]
							{
								const TSharedRef<FVarRow> Row = MakeShared<FVarRow>();
								Row->bUserAdded = true;
								Row->bGlobal = true;
								VarRows.Add(Row);
								RebuildVarWidgets();
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
	SyncRowsWithHarvest();
	RebuildVarWidgets();
	Reset();
}

void SLeoGraphSimulator::NotifyGraphChanged()
{
	SyncRowsWithHarvest();
	RebuildVarWidgets();
	Reset();
	RefreshViews();
}

void SLeoGraphSimulator::Reset()
{
	// 黑板值是会话状态：重置只回到入口，不清值（重开编辑器/图变更重建才重置默认值）
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

	// 干跑高亮上报：仅顶层图的节点可上画布（子图内节点 → NAME_None 清除）
	NotifyCurrentNode(Stack.Last().Graph.Get() == Graph.Get() ? NodeId : NAME_None);

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
	NotifyCurrentNode(NAME_None);
}

void SLeoGraphSimulator::NotifyCurrentNode(FName NodeId)
{
	if (OnCurrentNodeEvent.IsBound())
	{
		OnCurrentNodeEvent.Execute(NodeId);
	}
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
	RefreshVarCells(); // 边副作用写过黑板 → 值单元格实时回显
}

// ---- 黑板键预览（收割驱动 · 值直连沙箱黑板） ----

namespace
{
	leo::FLeoValue DefaultForKind(leo::FLeoValue::EKind Kind)
	{
		using EK = leo::FLeoValue::EKind;
		switch (Kind)
		{
		case EK::Bool:   return leo::FLeoValue::MakeBool(false);
		case EK::Int:    return leo::FLeoValue::MakeInt(0);
		case EK::Float:  return leo::FLeoValue::MakeFloat(0.0);
		case EK::String: return leo::FLeoValue::MakeString("");
		default:         return leo::FLeoValue::MakeInt(0);
		}
	}
}

void SLeoGraphSimulator::SyncRowsWithHarvest()
{
	// 收割键 → 行（新键按类型提示落默认值；既有键保留当前值；临时行原样保留）
	const TMap<FName, FLeoKnownVar>& Vars = FLeoVariableHarvest::Get().GetVars();
	for (const TPair<FName, FLeoKnownVar>& KV : Vars)
	{
		const TSharedRef<FVarRow>* Found = VarRows.FindByPredicate(
			[&KV](const TSharedRef<FVarRow>& R) { return R->Key == KV.Key; });
		if (Found) { continue; } // 已有行（含用户早前手动加的同名临时键）：原样接管

		const TSharedRef<FVarRow> Row = MakeShared<FVarRow>();
		Row->Key = KV.Key;
		Row->bGlobal = KV.Value.bGlobal;
		Row->Info = FString::Printf(TEXT("%s%s"), KV.Value.bGlobal ? TEXT("全局") : TEXT("局部"),
			KV.Value.Kind.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" · %s"), *KV.Value.Kind));
		TArray<FString> Tips;
		Tips.Add(KV.Value.bWritten
			? TEXT("脚本 set/setg 或图边副作用写入")
			: TEXT("仅被引用、未见写入（可能由游戏代码定义）"));
		if (!KV.Value.Source.IsNone()) { Tips.Add(FString::Printf(TEXT("来源: %s"), *KV.Value.Source.ToString())); }
		Row->Tooltip = FString::Join(Tips, TEXT("\n"));

		// 归属层上若无值则落默认（局部键→局部板，全局键→全局板）
		UNarrativeBlackboard* Board = Row->bGlobal ? SimCtx->Global.Get() : SimCtx->Local.Get();
		leo::FLeoValue Existing;
		if (Board && !Board->GetValue(KV.Key, Existing))
		{
			leo::FLeoValue::EKind Kind = leo::FLeoValue::EKind::Null;
			if (KV.Value.Kind == TEXT("Bool")) { Kind = leo::FLeoValue::EKind::Bool; }
			else if (KV.Value.Kind == TEXT("Int")) { Kind = leo::FLeoValue::EKind::Int; }
			else if (KV.Value.Kind == TEXT("Float")) { Kind = leo::FLeoValue::EKind::Float; }
			else if (KV.Value.Kind == TEXT("String")) { Kind = leo::FLeoValue::EKind::String; }
			Board->SetValue(KV.Key, DefaultForKind(Kind));
		}
		VarRows.Add(Row);
	}
	// 按名排序（收割键与临时键混排，稳定观感）
	VarRows.Sort([](const TSharedRef<FVarRow>& A, const TSharedRef<FVarRow>& B)
	{
		return A->Key.LexicalLess(B->Key);
	});
}

void SLeoGraphSimulator::RebuildVarWidgets()
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

			// 键名：收割键只读展示（悬停看类型/来源）；临时键可编辑
			+ SHorizontalBox::Slot().FillWidth(0.34f).VAlign(VAlign_Center)
			[
				Row->bUserAdded
				? static_cast<TSharedRef<SWidget>>(
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("KeyNameHint", "键名"))
					.Text_Lambda([Row]() { return FText::FromName(Row->Key); })
					.OnTextCommitted_Lambda([Row](const FText& T, ETextCommit::Type)
					{
						const FString S = T.ToString().TrimStartAndEnd();
						Row->Key = S.IsEmpty() ? NAME_None : FName(*S);
					}))
				: static_cast<TSharedRef<SWidget>>(
					SNew(STextBlock)
					.Text(FText::FromName(Row->Key))
					.ToolTipText(FText::FromString(Row->Tooltip)))
			]

			// 层/类型灰字（临时键显示"临时·全局"）
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 2, 0).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Row]()
				{
					return FText::FromString(Row->bUserAdded ? TEXT("临时·全局") : Row->Info);
				})
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			// 值：直连沙箱黑板（提交即写板；非法输入回显回原值）
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("ValueHint", "值（.leo 字面量：3 / 2.5 / true / \"文本\"）"))
				.Text_Lambda([this, Row]()
				{
					leo::FLeoValue V;
					FString Src;
					return ReadSandboxValue(Row->Key, V) && LeoConditionCodec::ValueToSource(V, Src)
						? FText::FromString(Src) : FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([this, Row](const FText& T, ETextCommit::Type)
				{
					WriteSandboxValue(Row, T.ToString());
					RefreshVarCells();
				})
			]

			// 删行：仅临时键（标准 Button 样式，理由同 SLeoConditionEditor 删行钮）
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveTempKey", "删除"))
				.ButtonStyle(FAppStyle::Get(), "Button")
				.ContentPadding(FMargin(4, 1))
				.Visibility_Lambda([Row]() { return Row->bUserAdded ? EVisibility::Visible : EVisibility::Hidden; })
				.OnClicked_Lambda([this, Row]
				{
					VarRows.RemoveAll([Row](const TSharedRef<FVarRow>& R) { return R == Row; });
					RebuildVarWidgets();
					return FReply::Handled();
				})
			]
		];
	}
}

bool SLeoGraphSimulator::ReadSandboxValue(FName Key, leo::FLeoValue& Out) const
{
	if (Key.IsNone() || !SimCtx) { return false; }
	if (SimCtx->Local && SimCtx->Local->GetValue(Key, Out)) { return true; } // 读取链同运行时：局部→全局
	return SimCtx->Global && SimCtx->Global->GetValue(Key, Out);
}

void SLeoGraphSimulator::WriteSandboxValue(const TSharedRef<FVarRow>& Row, const FString& SourceText)
{
	if (Row->Key.IsNone() || !SimCtx) { return; }
	leo::FLeoDiag D;
	const leo::FLeoExprPtr Expr = LeoBridge::CompileExpr(SourceText, D);
	leo::FLeoValue V;
	leo::ELeoDiag Code; std::string Msg;
	if (!Expr || !LeoBridge::EvalExpr(Expr, leo::FLeoVarResolver(), V, Code, Msg)) { return; } // 非法：不写板，回显还原
	UNarrativeBlackboard* Board = Row->bGlobal ? SimCtx->Global.Get() : SimCtx->Local.Get();
	if (Board) { Board->SetValue(Row->Key, V); }
}

void SLeoGraphSimulator::RefreshVarCells()
{
	// 值单元格不逐帧刷：RefreshViews（转移/重置后）与提交时回显
	if (!VarBox.IsValid()) { return; }
	RebuildVarWidgets(); // 行数少，整体重建最简单且顺带刷新灰字
}

