#include "LeoGraphEditorToolkit.h"

#include "LeoVariableHarvest.h"
#include "Widgets/SLeoGraphSimulator.h"

#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Graph/LeoEdGraph.h"
#include "Graph/LeoEdGraphNodes.h"
#include "Graph/LeoGraphMirror.h"
#include "GraphEditor.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SGraphPanel.h"
#include "Subsystem/LeoNarrativeSubsystem.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
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

	// 镜像控制器：SGraphEditor 编辑的瞬态 EdGraph + 资产同步
	Mirror = MakeUnique<FLeoGraphMirror>();
	Mirror->Init(InGraph);
	Mirror->OnRebuilt = [this]() { OnMirrorRebuilt(); };
	Mirror->OnNodeAdded = [this](ULeoEdGraphNode* Node)
	{
		if (Node && GraphEditorPtr.IsValid())
		{
			GraphEditorPtr->JumpToNode(Node, /*bRequestRename*/false, /*bSelectNode*/true);
		}
	};

	BindGraphCommands();

	// 单 Tab 宿纳全部编辑器内容（SetHideTabWell 去掉标签头）
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
		FTickerDelegate::CreateRaw(this, &FLeoGraphEditorToolkit::TickEditor), 0.1f);
}

FText FLeoGraphEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "叙事编排图编辑器");
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

// ---- 撤销/重做：资产已被事务系统还原，重建镜像即恢复视图 ----

void FLeoGraphEditorToolkit::PostUndo(bool bSuccess)
{
	if (bSuccess && Mirror.IsValid())
	{
		Mirror->RebuildFromAsset();
		SelectedNodeId = NAME_None;
		SelectedEdgeFromId = NAME_None;
		SelectedEdgeIndex = INDEX_NONE;
		RefreshNodeRows();
		ShowDetailsForSelection();
		if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
	}
}

void FLeoGraphEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

// ---- 单 Tab 全量内容 ----

void FLeoGraphEditorToolkit::BindGraphCommands()
{
	GraphCommands = MakeShared<FUICommandList>();
	GraphCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateRaw(this, &FLeoGraphEditorToolkit::DeleteSelected),
		FCanExecuteAction::CreateLambda([this]()
		{
			return GraphEditorPtr.IsValid() && GraphEditorPtr->GetSelectedNodes().Num() > 0;
		}));
}

TSharedRef<SWidget> FLeoGraphEditorToolkit::MakeGraphTab()
{
	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateRaw(this, &FLeoGraphEditorToolkit::OnGraphSelectionChanged);
	Events.OnNodeDoubleClicked = FSingleNodeEvent::CreateRaw(this, &FLeoGraphEditorToolkit::OnNodeDoubleClicked);

	FGraphAppearanceInfo Appearance;
	Appearance.InstructionText = LOCTEXT("GraphInstruction",
		"右键空白添加节点\n从卡片底部引脚拖出连线\n从入口拖线设为起点");
	Appearance.CornerText = LOCTEXT("CornerText", "叙事编排图");

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
				SNew(SButton).Text(LOCTEXT("DeleteSelected", "删除选中"))
				.OnClicked_Lambda([this] { DeleteSelected(); return FReply::Handled(); })
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
				SNew(STextBlock).Text(FText::FromString(TEXT("中键拖=平移 · 滚轮=缩放 · 右键=添加节点 · 点连线中点图标=选边 · Ctrl+Z=撤销")))
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
						SAssignNew(NodeList, SListView<TSharedPtr<FLeoNodeRow>>)
						.ListItemsSource(&NodeRows)
						.OnGenerateRow_Lambda([](TSharedPtr<FLeoNodeRow> Row, const TSharedRef<STableViewBase>& Table)
						{
							return SNew(STableRow<TSharedPtr<FLeoNodeRow>>, Table)
								.Content()
								[
									SNew(STextBlock).Text(FText::FromString(Row->Label))
								];
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FLeoNodeRow> Row, ESelectInfo::Type)
						{
							if (!Row.IsValid() || bSyncingListSelection) { return; }
							SelectNodeById(Row->Id);
						})
					]
				]
			]
			+ SSplitter::Slot().Value(0.56f)
			[
				SAssignNew(GraphEditorPtr, SGraphEditor)
				.AdditionalCommands(GraphCommands)
				.IsEditable(true)
				.GraphToEdit(Mirror.IsValid() ? Mirror->GetEdGraph() : nullptr)
				.GraphEvents(Events)
				.AutoExpandActionMenu(true)
				.Appearance(Appearance)
				.ShowGraphStateOverlay(false)
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
								return !SelectedEdgeFromId.IsNone()
									? FText::FromString(TEXT("连线（条件/优先级/副作用）"))
									: FText::FromString(TEXT("详情（编辑后自动写回）"));
							})
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right)
						[
							SNew(SButton).Text(LOCTEXT("DeleteEdge", "删除此连线"))
							.IsEnabled_Lambda([this] { return !SelectedEdgeFromId.IsNone(); })
							.OnClicked_Lambda([this]
							{
								if (Mirror.IsValid())
								{
									if (ULeoEdGraphNode_Edge* Edge = Mirror->FindEdge(SelectedEdgeFromId, SelectedEdgeIndex))
									{
										Mirror->DeleteEdge(Edge);
									}
								}
								return FReply::Handled();
							})
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
					.OnCurrentNodeChanged_Lambda([this](FName NodeId) { OnSimCurrentNode(NodeId); })
				]
			]
		];
}

// ---- 交互 ----

void FLeoGraphEditorToolkit::OnGraphSelectionChanged(const TSet<class UObject*>& NewSelection)
{
	ULeoEdGraphNode* SelNode = nullptr;
	ULeoEdGraphNode_Edge* SelEdge = nullptr;
	for (UObject* Obj : NewSelection)
	{
		if (ULeoEdGraphNode* N = Cast<ULeoEdGraphNode>(Obj))
		{
			if (!SelNode) { SelNode = N; }
		}
		else if (ULeoEdGraphNode_Edge* E = Cast<ULeoEdGraphNode_Edge>(Obj))
		{
			if (!SelEdge) { SelEdge = E; }
		}
	}

	SelectedNodeId = (SelNode && !SelNode->IsA<ULeoEdGraphNode_Entry>()) ? SelNode->NodeId : NAME_None;
	if (SelEdge)
	{
		SelectedEdgeFromId = SelEdge->FromNodeId;
		SelectedEdgeIndex = SelEdge->EdgeIndex;
	}
	else
	{
		SelectedEdgeFromId = NAME_None;
		SelectedEdgeIndex = INDEX_NONE;
	}

	ShowDetailsForSelection();

	// 列表联动（防回调环）
	if (NodeList.IsValid())
	{
		TSharedPtr<FLeoNodeRow> RowToSelect;
		for (const TSharedPtr<FLeoNodeRow>& Row : NodeRows)
		{
			if (Row->Id == SelectedNodeId) { RowToSelect = Row; break; }
		}
		TGuardValue<bool> Guard(bSyncingListSelection, true);
		NodeList->ClearSelection();
		if (RowToSelect) { NodeList->SetSelection(RowToSelect); }
	}
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

void FLeoGraphEditorToolkit::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	// 双击连线中点图标 / 卡片 → 详情面板刷新（条件/优先级在右侧编辑）
	ShowDetailsForSelection();
}

void FLeoGraphEditorToolkit::DeleteSelected()
{
	FLeoGraphMirror* M = GetMirror();
	if (!M || !GraphEditorPtr.IsValid()) { return; }
	const FGraphPanelSelectionSet& Sel = GraphEditorPtr->GetSelectedNodes();
	TArray<ULeoEdGraphNode*> Nodes;
	TArray<ULeoEdGraphNode_Edge*> Edges;
	for (UObject* Obj : Sel)
	{
		if (ULeoEdGraphNode* N = Cast<ULeoEdGraphNode>(Obj))
		{
			if (!N->IsA<ULeoEdGraphNode_Entry>()) { Nodes.Add(N); }
		}
		else if (ULeoEdGraphNode_Edge* E = Cast<ULeoEdGraphNode_Edge>(Obj))
		{
			Edges.Add(E);
		}
	}
	if (Edges.Num() > 0) { M->DeleteEdges(Edges); }
	if (Nodes.Num() > 0) { M->DeleteNodes(Nodes); }
}

void FLeoGraphEditorToolkit::SelectNodeById(FName NodeId)
{
	if (NodeId.IsNone()) { return; }
	if (Mirror.IsValid())
	{
		if (ULeoEdGraphNode* Ed = Mirror->FindNode(NodeId))
		{
			if (GraphEditorPtr.IsValid())
			{
				GraphEditorPtr->JumpToNode(Ed, /*bRequestRename*/false, /*bSelectNode*/true);
			}
		}
	}
}

void FLeoGraphEditorToolkit::ShowDetailsForSelection()
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G || !DetailsView.IsValid()) { return; }

	const FLeoScenarioNode* Node = !SelectedNodeId.IsNone() ? G->FindNode(SelectedNodeId) : nullptr;
	if (Node)
	{
		if (!NodeWrapper.IsValid())
		{
			NodeWrapper = TStrongObjectPtr<ULeoNodeEditWrapper>(NewObject<ULeoNodeEditWrapper>(GetTransientPackage()));
		}
		NodeWrapper->Owner = G;
		NodeWrapper->NodeId = SelectedNodeId;
		NodeWrapper->Node = *Node;
		DetailsView->SetObject(NodeWrapper.Get());
		return;
	}

	if (!SelectedEdgeFromId.IsNone())
	{
		const FLeoScenarioNode* FromNode = G->FindNode(SelectedEdgeFromId);
		if (FromNode && FromNode->Edges.IsValidIndex(SelectedEdgeIndex))
		{
			if (!EdgeWrapper.IsValid())
			{
				EdgeWrapper = TStrongObjectPtr<ULeoEdgeEditWrapper>(NewObject<ULeoEdgeEditWrapper>(GetTransientPackage()));
			}
			EdgeWrapper->Owner = G;
			EdgeWrapper->FromNodeId = SelectedEdgeFromId;
			EdgeWrapper->EdgeIndex = SelectedEdgeIndex;
			EdgeWrapper->Edge = FromNode->Edges[SelectedEdgeIndex];
			DetailsView->SetObject(EdgeWrapper.Get());
			return;
		}
	}

	DetailsView->SetObject(G); // 无选中：编辑图本体（EntryNode 等）
}

void FLeoGraphEditorToolkit::NotifyPostChange(const FPropertyChangedEvent& Event, FProperty*)
{
	ULeoScenarioGraph* G = Graph.Get();
	if (!G) { return; }

	bool bHandled = false;

	// 节点包装编辑 → 写回（连线与坐标由画布管理，写回时保留现值防覆盖）
	if (NodeWrapper.IsValid() && NodeWrapper->Owner == G && NodeWrapper->NodeId == SelectedNodeId)
	{
		for (FLeoScenarioNode& AssetNode : G->Nodes)
		{
			if (AssetNode.Id == NodeWrapper->NodeId)
			{
				FScopedTransaction Transaction(LOCTEXT("EditNodeTx", "编辑节点属性"));
				G->Modify();
				FLeoScenarioNode NewNode = NodeWrapper->Node;
				NewNode.Edges = AssetNode.Edges;
				NewNode.EditorPos = AssetNode.EditorPos;
				AssetNode = NewNode;
				G->PostEditChange();
				bHandled = true;
				if (Mirror.IsValid()) { Mirror->RefreshNodeVisual(AssetNode.Id); }
				break;
			}
		}
	}

	// 连线包装编辑 → 写回
	if (EdgeWrapper.IsValid() && EdgeWrapper->Owner == G
		&& EdgeWrapper->FromNodeId == SelectedEdgeFromId && EdgeWrapper->EdgeIndex == SelectedEdgeIndex
		&& !SelectedEdgeFromId.IsNone())
	{
		const FLeoScenarioNode* FromNode = G->FindNode(EdgeWrapper->FromNodeId);
		if (FromNode && FromNode->Edges.IsValidIndex(EdgeWrapper->EdgeIndex))
		{
			FScopedTransaction Transaction(LOCTEXT("EditEdgeTx", "编辑连线属性"));
			G->Modify();
			for (FLeoScenarioNode& AssetNode : G->Nodes)
			{
				if (AssetNode.Id == EdgeWrapper->FromNodeId)
				{
					AssetNode.Edges[EdgeWrapper->EdgeIndex] = EdgeWrapper->Edge;
					break;
				}
			}
			G->PostEditChange();
			bHandled = true;
		}
	}

	if (bHandled)
	{
		RefreshNodeRows();
		if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
		// 图数据变了 → 变量收割注册表过期（边条件/副作用可能引用了新变量）
		FLeoVariableHarvest::Get().MarkStale();
	}
	else if (Mirror.IsValid())
	{
		// 图本体编辑（EntryNode 等）→ 重建镜像（入口连线跟随）
		Mirror->RebuildFromAsset();
	}
}

FVector2D FLeoGraphEditorToolkit::FindFreePosition() const
{
	// 网格级联：行优先扫描第一个不与任何节点（含边距）重叠的 32px 网格位
	const ULeoScenarioGraph* G = Graph.Get();
	const FVector2D Size(190.f, 70.f); // 卡片估计尺寸（实际尺寸自适应内容）
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
	if (Mirror.IsValid())
	{
		Mirror->AddNode(Type, FindFreePosition());
	}
}

void FLeoGraphEditorToolkit::AutoLayout()
{
	if (!Mirror.IsValid()) { return; }
	Mirror->ApplyAutoLayout();
	if (GraphEditorPtr.IsValid())
	{
		if (SGraphPanel* Panel = GraphEditorPtr->GetGraphPanel())
		{
			Panel->ZoomToFit(/*bOnlySelection*/false);
		}
	}
}

void FLeoGraphEditorToolkit::RefreshNodeRows()
{
	ULeoScenarioGraph* G = Graph.Get();
	NodeRows.Reset();
	if (G)
	{
		for (const FLeoScenarioNode& N : G->Nodes)
		{
			const FString Label = FString::Printf(TEXT("%s%s"),
				*N.Id.ToString(),
				N.Id == G->EntryNode ? TEXT("  ◀入口") : TEXT(""));
			NodeRows.Add(MakeShared<FLeoNodeRow>(FLeoNodeRow{ N.Id, Label }));
		}
	}
	if (NodeList.IsValid()) { NodeList->RequestListRefresh(); }
}

void FLeoGraphEditorToolkit::RefreshAll()
{
	RefreshNodeRows();
	ShowDetailsForSelection();
	if (Mirror.IsValid()) { Mirror->RebuildFromAsset(); }
}

void FLeoGraphEditorToolkit::OnMirrorRebuilt()
{
	RefreshNodeRows();
	// 选中失效则清（结构删除后）
	if (!SelectedNodeId.IsNone() && (!Mirror.IsValid() || !Mirror->FindNode(SelectedNodeId)))
	{
		SelectedNodeId = NAME_None;
	}
	if (!SelectedEdgeFromId.IsNone() && (!Mirror.IsValid() || !Mirror->FindEdge(SelectedEdgeFromId, SelectedEdgeIndex)))
	{
		SelectedEdgeFromId = NAME_None;
		SelectedEdgeIndex = INDEX_NONE;
	}
	ShowDetailsForSelection();
	if (Simulator.IsValid()) { Simulator->NotifyGraphChanged(); }
}

// ---- PIE/干跑联动 ----

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

bool FLeoGraphEditorToolkit::TickEditor(float)
{
	// 拖拽结束后的位置提交（Mirror 内部去抖，一次拖拽一个事务）
	if (Mirror.IsValid())
	{
		Mirror->TickSyncPositions();
	}

	// PIE 运行高亮（激活时优先于干跑高亮）
	FName LiveNode;
	bool bPieGraphActive = false;
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.OwningGameInstance && Ctx.WorldType == EWorldType::PIE)
			{
				if (ULeoNarrativeSubsystem* Leo = Ctx.OwningGameInstance->GetSubsystem<ULeoNarrativeSubsystem>())
				{
					if (Leo->IsGraphActive())
					{
						LiveNode = Leo->GetCurrentGraphNode();
						bPieGraphActive = true;
					}
				}
				break;
			}
		}
	}
	if (bPieGraphActive && Mirror.IsValid())
	{
		Mirror->SetActiveNode(LiveNode);
	}
	return true;
}

void FLeoGraphEditorToolkit::OnSimCurrentNode(FName NodeId)
{
	if (Mirror.IsValid())
	{
		Mirror->SetActiveNode(NodeId);
	}
}

#undef LOCTEXT_NAMESPACE
