#include "LeoGraphEditorToolkit.h"

#include "Widgets/SLeoGraphSimulator.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Subsystem/LeoNarrativeSubsystem.h"
#include "Toolkits/IToolkitHost.h"
#include "Framework/Application/MenuStack.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "LeoGraphEditorToolkit"

namespace
{
	const FName GGraphTabId(TEXT("LeoGraphEditorTab"));
}

FLeoGraphEditorToolkit::~FLeoGraphEditorToolkit()
{
	if (GEditor && BeginPIEHandle.IsValid())
	{
		FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(LiveTickHandle);
}

void FLeoGraphEditorToolkit::InitLeoGraphEditor(const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& Host, ULeoScenarioGraph* InGraph)
{
	Graph = InGraph;

	FPropertyEditorModule& PEM = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.bAllowSearch = false;
	Args.NotifyHook = this; // 属性变更 → NotifyPostChange 写回资产
	DetailsView = PEM.CreateDetailView(Args);
	DetailsView->SetObject(InGraph);

	// v2：单 Tab 宿纳全部编辑器内容（SetHideTabWell 去掉标签头）
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("LeoGraphEditorLayout_v2"))
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Horizontal)
		->Split(FTabManager::NewStack()->SetHideTabWell(true)
			->AddTab(GGraphTabId, ETabState::OpenedTab))
	);

	InitAssetEditor(Mode, Host, TEXT("LeoGraphEditorApp"), Layout,
		/*bCreateDefaultStandaloneMenu*/true, /*bCreateDefaultToolbar*/true, InGraph);

	BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FLeoGraphEditorToolkit::OnBeginPIE);
	LiveTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FLeoGraphEditorToolkit::TickLiveHighlight), 0.5f);
}

FText FLeoGraphEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Leo 编排图编辑器");
}

void FLeoGraphEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(GGraphTabId,
		FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
		{
			return SNew(SDockTab).TabRole(ETabRole::PanelTab)
				.Label(LOCTEXT("GraphTabLabel", "编排图"))
				[
					MakeGraphTab()
				];
		}));
}

void FLeoGraphEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(GGraphTabId);
}

FName FLeoGraphEditorToolkit::GetSelectedNodeId() const
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !Selection.IsNode() || !G->Nodes.IsValidIndex(Selection.NodeIndex)) { return NAME_None; }
	return G->Nodes[Selection.NodeIndex].Id;
}

// ---- 单 Tab 全量内容 ----

TSharedRef<SWidget> FLeoGraphEditorToolkit::MakeGraphTab()
{
	return SNew(SVerticalBox)
		// 顶部工具条
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 4, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("＋章节")))
				.OnClicked_Lambda([this] { AddNode(ELeoScenarioNodeType::Chapter); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("＋分流")))
				.OnClicked_Lambda([this] { AddNode(ELeoScenarioNodeType::Branch); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("＋结局")))
				.OnClicked_Lambda([this] { AddNode(ELeoScenarioNodeType::Ending); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("＋子图")))
				.OnClicked_Lambda([this] { AddNode(ELeoScenarioNodeType::Subgraph); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("删除选中")))
				.OnClicked_Lambda([this] { DeleteSelectedNode(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("设为入口")))
				.OnClicked_Lambda([this]
				{
					if (ULeoScenarioGraph* G = Graph.Get())
					{
						const FName Id = GetSelectedNodeId();
						if (!Id.IsNone()) { G->Modify(); G->EntryNode = Id; RefreshNodeRows(); }
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			[
				SNew(SButton).Text(FText::FromString(TEXT("自动布局")))
				.OnClicked_Lambda([this] { AutoLayout(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this] { return bRunFromSelectionInPIE ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bRunFromSelectionInPIE = S == ECheckBoxState::Checked; })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("PIE 从选中节点运行")))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("中键拖=平移 · 滚轮=缩放 · 右键空白=加节点")))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]
		// 主区：列表 | 画布 | 详情
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.16f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(4)
					[
						SNew(STextBlock).Text(LOCTEXT("OutlineHeader", "流程大纲"))
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
					[
						SAssignNew(NodeList, SListView<TSharedPtr<FString>>)
						.ListItemsSource(&NodeRows)
						.OnGenerateRow_Lambda([](TSharedPtr<FString> Row, const TSharedRef<STableViewBase>& Table)
						{
							return SNew(STableRow<TSharedPtr<FString>>, Table)
								.Content()
								[
									SNew(STextBlock).Text(FText::FromString(*Row))
								];
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Row, ESelectInfo::Type)
						{
							if (!Row.IsValid()) { return; }
							const int32 Idx = NodeRows.IndexOfByKey(Row);
							if (Idx != INDEX_NONE) { SelectNode(Idx); }
						})
					]
				]
			]
			+ SSplitter::Slot().Value(0.56f)
			[
				SAssignNew(Canvas, SLeoGraphCanvas)
				.Graph(Graph)
				.OnSelectionChanged_Lambda([this](const FLeoGraphSelection& Sel) { OnCanvasSelection(Sel); })
				.OnNodeMoved_Lambda([this](int32, FVector2D)
				{
					if (ULeoScenarioGraph* G = Graph.Get()) { G->Modify(); } // 拖动结束标脏
				})
				.OnContextMenuRequested_Lambda([this](FVector2D GraphPos, FVector2D ScreenPos)
				{
					ShowCanvasContextMenu(GraphPos, ScreenPos);
				})
				.OnBackgroundClick_Lambda([this]
				{
					Selection = FLeoGraphSelection();
					if (NodeList.IsValid()) { NodeList->ClearSelection(); }
					ShowDetailsForSelection();
				})
				.OnEdgeSelectionChanged_Lambda([this](int32 FromIdx, int32 EdgeIdx)
				{
					OnEdgeSelected(FromIdx, EdgeIdx);
				})
				.OnConnectRequested_Lambda([this](int32 FromIdx, int32 ToIdx)
				{
					OnConnectRequested(FromIdx, ToIdx);
				})
				.OnDeleteEdgeRequested_Lambda([this] { DeleteSelectedEdge(); })
				.OnDeleteNodeRequested_Lambda([this] { DeleteSelectedNode(); })
			]
			+ SSplitter::Slot().Value(0.28f)
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
							SNew(STextBlock).Text_Lambda([this]
							{
								return Selection.IsEdge()
									? FText::FromString(TEXT("连线（条件/优先级/副作用）"))
									: FText::FromString(TEXT("详情（编辑后自动写回）"));
							})
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right)
						[
							SNew(SButton).Text(LOCTEXT("DeleteEdge", "删除此连线"))
							.IsEnabled_Lambda([this] { return Selection.IsEdge(); })
							.OnClicked_Lambda([this] { DeleteSelectedEdge(); return FReply::Handled(); })
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(2)
					[
						DetailsView.ToSharedRef()
					]
				]
			]
		]
		// 底部：整宽干跑模拟抽屉
		+ SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 4)
		[
			SNew(SBox).HeightOverride(280.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SAssignNew(Simulator, SLeoGraphSimulator)
					.Graph(Graph)
					.GetSelectedNodeId_Lambda([this]() { return GetSelectedNodeId(); })
				]
			]
		];
}

// ---- 交互 ----

void FLeoGraphEditorToolkit::OnCanvasSelection(const FLeoGraphSelection& Sel)
{
	Selection = Sel;
	ShowDetailsForSelection();
	if (NodeList.IsValid() && Sel.IsNode() && NodeRows.IsValidIndex(Sel.NodeIndex))
	{
		NodeList->SetSelection(NodeRows[Sel.NodeIndex]);
	}
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

void FLeoGraphEditorToolkit::SelectNode(int32 Index)
{
	FLeoGraphSelection Sel;
	Sel.Kind = FLeoGraphSelection::EKind::Node;
	Sel.NodeIndex = Index;
	Selection = Sel;
	if (Canvas.IsValid()) { Canvas->SetSelection(Sel); }
	ShowDetailsForSelection();
}

void FLeoGraphEditorToolkit::ShowDetailsForSelection()
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !DetailsView.IsValid()) { return; }
	if (Selection.IsNode() && G->Nodes.IsValidIndex(Selection.NodeIndex))
	{
		if (!NodeWrapper.IsValid())
		{
			NodeWrapper = TStrongObjectPtr<ULeoNodeEditWrapper>(NewObject<ULeoNodeEditWrapper>(GetTransientPackage()));
		}
		NodeWrapper->Owner = G;
		NodeWrapper->SourceIndex = Selection.NodeIndex;
		NodeWrapper->Node = G->Nodes[Selection.NodeIndex];
		DetailsView->SetObject(NodeWrapper.Get());
	}
	else
	{
		DetailsView->SetObject(G); // 无选中：编辑图本体（EntryNode 等）
	}
}

void FLeoGraphEditorToolkit::NotifyPostChange(const FPropertyChangedEvent& Event, FProperty*)
{
	// 节点包装编辑 → 写回资产（连线与坐标由画布交互管理，写回时保留现值防覆盖）
	if (NodeWrapper.IsValid())
	{
		if (ULeoScenarioGraph* G = NodeWrapper->Owner)
		{
			if (G->Nodes.IsValidIndex(NodeWrapper->SourceIndex))
			{
				G->Modify();
				FLeoScenarioNode NewNode = NodeWrapper->Node;
				NewNode.Edges = G->Nodes[NodeWrapper->SourceIndex].Edges;
				NewNode.EditorPos = G->Nodes[NodeWrapper->SourceIndex].EditorPos;
				G->Nodes[NodeWrapper->SourceIndex] = NewNode;
				G->PostEditChange();
			}
		}
	}
	// 连线包装编辑 → 写回
	if (EdgeWrapper.IsValid())
	{
		if (ULeoScenarioGraph* G = EdgeWrapper->Owner)
		{
			if (G->Nodes.IsValidIndex(EdgeWrapper->NodeIndex)
				&& G->Nodes[EdgeWrapper->NodeIndex].Edges.IsValidIndex(EdgeWrapper->EdgeIndex))
			{
				G->Modify();
				G->Nodes[EdgeWrapper->NodeIndex].Edges[EdgeWrapper->EdgeIndex] = EdgeWrapper->Edge;
				G->PostEditChange();
			}
		}
	}
	RefreshNodeRows();
	if (Canvas.IsValid()) { Canvas->RefreshVisuals(); }
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

FVector2D FLeoGraphEditorToolkit::FindFreePosition() const
{
	// 网格级联：行优先扫描第一个不与任何节点（含边距）重叠的 32px 网格位
	const ULeoScenarioGraph* G = Graph.Get();
	const FVector2D Size = LeoGraphCanvasConst::GetNodeSize();
	const float Margin = 24.f;
	for (float Y = 64.f; Y < 4096.f; Y += 32.f)
	{
		for (float X = 64.f; X < 4096.f; X += 32.f)
		{
			bool bFree = true;
			if (G)
			{
				for (const FLeoScenarioNode& N : G->Nodes)
				{
					const FVector2D Min = N.EditorPos - Margin;
					const FVector2D Max = N.EditorPos + Size + Margin;
					if (X >= Min.X && X <= Max.X && Y >= Min.Y && Y <= Max.Y) { bFree = false; break; }
				}
			}
			if (bFree) { return FVector2D(X, Y); }
		}
	}
	return FVector2D(64.f, 64.f);
}

void FLeoGraphEditorToolkit::AddNode(ELeoScenarioNodeType Type)
{
	AddNodeAt(Type, FindFreePosition());
}

void FLeoGraphEditorToolkit::AddNodeAt(ELeoScenarioNodeType Type, FVector2D Pos)
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G) { return; }
	G->Modify();
	FLeoScenarioNode N;
	N.Type = Type;
	static int32 Counter = 0;
	N.Id = *FString::Printf(TEXT("node_%d"), ++Counter);
	N.EditorPos = FVector2D(FMath::GridSnap(Pos.X, 16.f), FMath::GridSnap(Pos.Y, 16.f));
	N.EditorPos = FVector2D(FMath::Max(0.f, N.EditorPos.X), FMath::Max(0.f, N.EditorPos.Y));
	const int32 NewIndex = G->Nodes.Add(N);
	if (G->EntryNode.IsNone()) { G->EntryNode = N.Id; }
	RefreshAll();
	SelectNode(NewIndex);
}

void FLeoGraphEditorToolkit::OnConnectRequested(int32 FromIdx, int32 ToIdx)
{
	// 画布拖拽建边：无条件/优先级 0，条件在选中连线后的详情里编
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !G->Nodes.IsValidIndex(FromIdx) || !G->Nodes.IsValidIndex(ToIdx) || FromIdx == ToIdx) { return; }
	G->Modify();
	FLeoScenarioEdge E;
	E.To = G->Nodes[ToIdx].Id;
	G->Nodes[FromIdx].Edges.Add(E);
	if (Canvas.IsValid()) { Canvas->RefreshVisuals(); }
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

void FLeoGraphEditorToolkit::OnEdgeSelected(int32 FromIdx, int32 EdgeIdx)
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !G->Nodes.IsValidIndex(FromIdx) || !G->Nodes[FromIdx].Edges.IsValidIndex(EdgeIdx)) { return; }
	FLeoGraphSelection Sel;
	Sel.Kind = FLeoGraphSelection::EKind::Edge;
	Sel.NodeIndex = FromIdx;
	Sel.EdgeIndex = EdgeIdx;
	Selection = Sel;
	if (Canvas.IsValid()) { Canvas->SetSelection(Sel); }
	if (NodeList.IsValid()) { NodeList->ClearSelection(); }

	if (!EdgeWrapper.IsValid())
	{
		EdgeWrapper = TStrongObjectPtr<ULeoEdgeEditWrapper>(NewObject<ULeoEdgeEditWrapper>(GetTransientPackage()));
	}
	EdgeWrapper->Owner = G;
	EdgeWrapper->NodeIndex = FromIdx;
	EdgeWrapper->EdgeIndex = EdgeIdx;
	EdgeWrapper->Edge = G->Nodes[FromIdx].Edges[EdgeIdx];
	if (DetailsView.IsValid()) { DetailsView->SetObject(EdgeWrapper.Get()); }
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

void FLeoGraphEditorToolkit::DeleteSelectedEdge()
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !Selection.IsEdge() || !G->Nodes.IsValidIndex(Selection.NodeIndex)) { return; }
	G->Modify();
	G->Nodes[Selection.NodeIndex].Edges.RemoveAt(Selection.EdgeIndex);
	Selection = FLeoGraphSelection();
	if (Canvas.IsValid()) { Canvas->RefreshVisuals(); Canvas->SetSelection(Selection); }
	ShowDetailsForSelection();
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

void FLeoGraphEditorToolkit::ShowCanvasContextMenu(FVector2D GraphPos, FVector2D ScreenPos)
{
	if (!Canvas.IsValid()) { return; }
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	const auto AddItem = [&Menu, this, GraphPos](const TCHAR* Label, ELeoScenarioNodeType Type)
	{
		Menu->AddSlot().AutoHeight().Padding(2)
		[
			SNew(SButton)
			.Text(FText::FromString(Label))
			.OnClicked_Lambda([this, Type, GraphPos]
			{
				AddNodeAt(Type, GraphPos);
				return FReply::Handled();
			})
		];
	};
	AddItem(TEXT("加 章节节点"), ELeoScenarioNodeType::Chapter);
	AddItem(TEXT("加 分流节点"), ELeoScenarioNodeType::Branch);
	AddItem(TEXT("加 结局节点"), ELeoScenarioNodeType::Ending);
	AddItem(TEXT("加 子图节点"), ELeoScenarioNodeType::Subgraph);

	FSlateApplication::Get().PushMenu(
		Canvas.ToSharedRef(),
		FWidgetPath(),
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(4)
		[
			Menu
		],
		ScreenPos,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)); // 5.8：枚举内嵌于 FPopupTransitionEffect
}

void FLeoGraphEditorToolkit::DeleteSelectedNode()
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !Selection.IsNode() || !G->Nodes.IsValidIndex(Selection.NodeIndex)) { return; }
	G->Modify();
	const FName RemovedId = G->Nodes[Selection.NodeIndex].Id;
	G->Nodes.RemoveAt(Selection.NodeIndex);
	for (FLeoScenarioNode& N : G->Nodes) // 清理指向被删节点的悬空边
	{
		N.Edges.RemoveAll([RemovedId](const FLeoScenarioEdge& E) { return E.To == RemovedId; });
	}
	if (G->EntryNode == RemovedId)
	{
		G->EntryNode = G->Nodes.Num() > 0 ? G->Nodes[0].Id : NAME_None;
	}
	Selection = FLeoGraphSelection();
	RefreshAll();
}

void FLeoGraphEditorToolkit::AutoLayout()
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || G->Nodes.Num() == 0) { return; }
	G->Modify();
	// 入口 BFS 分层；层距=节点宽+96，同层纵向堆叠行距=节点高+48
	TMap<FName, int32> Level;
	Level.Add(G->EntryNode, 0);
	TArray<FName> Queue = { G->EntryNode };
	while (!Queue.IsEmpty())
	{
		const FName Cur = Queue.Pop();
		const FLeoScenarioNode* N = G->FindNode(Cur);
		if (!N) { continue; }
		for (const FLeoScenarioEdge& E : N->Edges)
		{
			if (!Level.Contains(E.To))
			{
				Level.Add(E.To, Level[Cur] + 1);
				Queue.Add(E.To);
			}
		}
	}
	TMap<int32, int32> Column;
	for (FLeoScenarioNode& N : G->Nodes)
	{
		const int32 L = Level.Contains(N.Id) ? Level[N.Id] : 0;
		const int32 C = Column.Contains(L) ? Column[L] + 1 : 0;
		Column.Add(L, C);
		const FVector2D Size = LeoGraphCanvasConst::GetNodeSize();
		N.EditorPos = FVector2D(64.f + L * (Size.X + 96.f), 64.f + C * (Size.Y + 48.f));
	}
	if (Canvas.IsValid()) { Canvas->RefreshVisuals(); }
	RefreshNodeRows();
}

void FLeoGraphEditorToolkit::RefreshNodeRows()
{
	ULeoScenarioGraph* G = Graph.Get();
	NodeRows.Reset();
	if (G)
	{
		for (int32 i = 0; i < G->Nodes.Num(); ++i)
		{
			NodeRows.Add(MakeShared<FString>(FString::Printf(TEXT("%d. %s%s"),
				i, *G->Nodes[i].Id.ToString(),
				G->Nodes[i].Id == G->EntryNode ? TEXT("  ◀入口") : TEXT(""))));
		}
	}
	if (NodeList.IsValid()) { NodeList->RequestListRefresh(); }
}

void FLeoGraphEditorToolkit::RefreshAll()
{
	RefreshNodeRows();
	if (Canvas.IsValid()) { Canvas->RebuildNodes(); }
	ShowDetailsForSelection();
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

// ---- PIE 联动 ----

void FLeoGraphEditorToolkit::OnBeginPIE(bool bIsSimulating)
{
	if (!bRunFromSelectionInPIE) { return; }
	ULeoScenarioGraph* G = Graph.Get();
	const FName StartId = GetSelectedNodeId();
	if (!G || StartId.IsNone() || !GEngine) { return; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.OwningGameInstance && Ctx.WorldType == EWorldType::PIE)
		{
			if (ULeoNarrativeSubsystem* Leo = Ctx.OwningGameInstance->GetSubsystem<ULeoNarrativeSubsystem>())
			{
				Leo->StartGraph(G, StartId);
			}
			break;
		}
	}
}

bool FLeoGraphEditorToolkit::TickLiveHighlight(float)
{
	if (!Canvas.IsValid() || !GEngine) { return true; }
	FName LiveNode;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.OwningGameInstance && Ctx.WorldType == EWorldType::PIE)
		{
			if (ULeoNarrativeSubsystem* Leo = Ctx.OwningGameInstance->GetSubsystem<ULeoNarrativeSubsystem>())
			{
				if (Leo->IsGraphActive()) { LiveNode = Leo->GetCurrentGraphNode(); }
			}
			break;
		}
	}
	Canvas->SetLiveNode(LiveNode);
	return true;
}

#undef LOCTEXT_NAMESPACE
