#include "Graph/LeoGraphMirror.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/LeoEdGraph.h"
#include "Graph/LeoEdGraphNodes.h"
#include "Graph/LeoEdGraphSchema.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "LeoGraphMirror"

FLeoGraphMirror::~FLeoGraphMirror() = default;

void FLeoGraphMirror::Init(ULeoScenarioGraph* InAsset)
{
	Asset = InAsset;
	if (!InAsset) { return; }

	// 瞬态镜像图（不序列化；Schema = 类默认对象）
	EdGraph = TStrongObjectPtr<ULeoEdGraph>(NewObject<ULeoEdGraph>(GetTransientPackage(), NAME_None, RF_Transient));
	EdGraph->LeoGraph = InAsset;
	EdGraph->Mirror = this;
	EdGraph->Schema = ULeoEdGraphSchema::StaticClass();
	RebuildFromAsset();
}

FName FLeoGraphMirror::GenerateNodeId() const
{
	const ULeoScenarioGraph* G = Asset.Get();
	int32 MaxNum = 0;
	if (G)
	{
		for (const FLeoScenarioNode& N : G->Nodes)
		{
			FString Left, Right;
			if (N.Id.ToString().StartsWith(TEXT("node_")) && N.Id.ToString().Split(TEXT("_"), &Left, &Right) && Right.IsNumeric())
			{
				MaxNum = FMath::Max(MaxNum, FCString::Atoi(*Right));
			}
		}
	}
	return *FString::Printf(TEXT("node_%d"), MaxNum + 1);
}

FLeoScenarioNode* FLeoGraphMirror::FindAssetNodeMutable(FName NodeId) const
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G) { return nullptr; }
	for (FLeoScenarioNode& N : G->Nodes)
	{
		if (N.Id == NodeId) { return &N; }
	}
	return nullptr;
}

// ---- 重建 ----

void FLeoGraphMirror::RebuildFromAsset()
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || !EdGraph.IsValid()) { return; }

	// 清空旧镜像（瞬态节点，不进事务）
	TArray<UEdGraphNode*> OldNodes = EdGraph->Nodes;
	EdGraph->Nodes.Reset();
	for (UEdGraphNode* Old : OldNodes)
	{
		if (Old)
		{
			Old->BreakAllNodeLinks();
			Old->MarkAsGarbage();
		}
	}

	// 普通节点
	for (const FLeoScenarioNode& N : G->Nodes)
	{
		ULeoEdGraphNode* NodeEd = NewObject<ULeoEdGraphNode>(EdGraph.Get());
		NodeEd->CreateNewGuid(); // ★ 必须显式赋 GUID：NodeGuidMap / pin 句柄解析（预览线、拖线落点）全依赖它
		EdGraph->AddNode(NodeEd, /*bUserNeedsAction*/false, /*bSelectNewNode*/false);
		NodeEd->NodeId = N.Id;
		NodeEd->NodeType = N.Type;
		NodeEd->NodePosX = static_cast<int32>(N.EditorPos.X);
		NodeEd->NodePosY = static_cast<int32>(N.EditorPos.Y);
		NodeEd->AllocateDefaultPins();
		NodeEd->bLeoActive = (N.Id == ActiveNodeId);
	}

	// 边节点（状态机 transition 式：From 输出 → 边输入，边输出 → To 输入）
	for (const FLeoScenarioNode& N : G->Nodes)
	{
		ULeoEdGraphNode* FromEd = FindNode(N.Id);
		if (!FromEd) { continue; }
		for (int32 EdgeIdx = 0; EdgeIdx < N.Edges.Num(); ++EdgeIdx)
		{
			ULeoEdGraphNode* ToEd = FindNode(N.Edges[EdgeIdx].To);
			if (!ToEd) { continue; } // 悬空边：不建镜像（校验器负责报错）

			ULeoEdGraphNode_Edge* EdgeEd = NewObject<ULeoEdGraphNode_Edge>(EdGraph.Get());
			EdgeEd->CreateNewGuid();
			EdGraph->AddNode(EdgeEd, false, false);
			EdgeEd->FromNodeId = N.Id;
			EdgeEd->EdgeIndex = EdgeIdx;
			EdgeEd->NodePosX = (FromEd->NodePosX + ToEd->NodePosX) / 2; // 占位；二段布局会重算
			EdgeEd->NodePosY = (FromEd->NodePosY + ToEd->NodePosY) / 2;
			EdgeEd->AllocateDefaultPins();
			EdgeEd->bLeoActive = (N.Id == ActiveNodeId);

			if (UEdGraphPin* FromOut = FromEd->GetOutputPin())
			{
				if (UEdGraphPin* EdgeIn = EdgeEd->GetInputPin()) { FromOut->MakeLinkTo(EdgeIn); }
			}
			if (UEdGraphPin* EdgeOut = EdgeEd->GetOutputPin())
			{
				if (UEdGraphPin* ToIn = ToEd->GetInputPin()) { EdgeOut->MakeLinkTo(ToIn); }
			}
		}
	}

	// 入口隧道：始终存在；摆在入口目标正上方（BT 纵向流：入口在上、子层向下）
	ULeoEdGraphNode_Entry* EntryEd = NewObject<ULeoEdGraphNode_Entry>(EdGraph.Get());
	EntryEd->CreateNewGuid();
	EdGraph->AddNode(EntryEd, false, false);
	{
		ULeoEdGraphNode* TargetEd = FindNode(G->EntryNode);
		if (TargetEd)
		{
			EntryEd->NodePosX = TargetEd->NodePosX;
			EntryEd->NodePosY = TargetEd->NodePosY - 200;
		}
		else
		{
			EntryEd->NodePosX = 64;
			EntryEd->NodePosY = 64;
		}
		EntryEd->AllocateDefaultPins();
		if (TargetEd)
		{
			if (UEdGraphPin* EntryOut = EntryEd->GetOutputPin())
			{
				if (UEdGraphPin* TargetIn = TargetEd->GetInputPin()) { EntryOut->MakeLinkTo(TargetIn); }
			}
		}
	}

	EdGraph->NotifyGraphChanged();
	if (OnRebuilt) { OnRebuilt(); }
}

ULeoEdGraphNode* FLeoGraphMirror::FindNode(FName NodeId) const
{
	return EdGraph.IsValid() ? EdGraph->FindLeoNode(NodeId) : nullptr;
}

ULeoEdGraphNode_Edge* FLeoGraphMirror::FindEdge(FName FromNodeId, int32 EdgeIndex) const
{
	if (!EdGraph.IsValid()) { return nullptr; }
	for (UEdGraphNode* N : EdGraph->Nodes)
	{
		if (ULeoEdGraphNode_Edge* E = Cast<ULeoEdGraphNode_Edge>(N))
		{
			if (E->FromNodeId == FromNodeId && E->EdgeIndex == EdgeIndex) { return E; }
		}
	}
	return nullptr;
}

void FLeoGraphMirror::RefreshNodeVisual(FName NodeId)
{
	if (ULeoEdGraphNode* Node = FindNode(NodeId))
	{
		Node->RefreshVisual();
	}
}

void FLeoGraphMirror::SetActiveNode(FName NodeId)
{
	if (ActiveNodeId == NodeId) { return; }
	ActiveNodeId = NodeId;
	if (!EdGraph.IsValid()) { return; }
	for (UEdGraphNode* N : EdGraph->Nodes)
	{
		if (ULeoEdGraphNode* LN = Cast<ULeoEdGraphNode>(N))
		{
			LN->bLeoActive = !NodeId.IsNone() && LN->NodeId == NodeId;
		}
		else if (ULeoEdGraphNode_Edge* EN = Cast<ULeoEdGraphNode_Edge>(N))
		{
			EN->bLeoActive = !NodeId.IsNone() && EN->FromNodeId == NodeId;
		}
	}
}

// ---- 变更入口 ----

ULeoEdGraphNode* FLeoGraphMirror::AddNode(ELeoScenarioNodeType Type, const FVector2D& Pos, UEdGraphPin* FromPin, bool bSelectNewNode)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G) { return nullptr; }

	const FName NewId = GenerateNodeId();
	const bool bEntryDefault = G->EntryNode.IsNone();

	FScopedTransaction Transaction(LOCTEXT("AddNodeTx", "添加节点"));
	G->Modify();
	FLeoScenarioNode& N = G->Nodes.AddDefaulted_GetRef();
	N.Type = Type;
	N.Id = NewId;
	N.EditorPos = FVector2D(FMath::GridSnap<float>(Pos.X, 16.f), FMath::GridSnap<float>(Pos.Y, 16.f));

	// 从 pin 拖到空白弹菜单后自动接线（BT AutowireNewNode 的 Leo 版）
	if (FromPin)
	{
		if (ULeoEdGraphNode_Entry* FromEntry = Cast<ULeoEdGraphNode_Entry>(FromPin->GetOwningNode()))
		{
			if (FromPin->Direction == EGPD_Output) { G->EntryNode = NewId; }
		}
		else if (ULeoEdGraphNode* FromNode = Cast<ULeoEdGraphNode>(FromPin->GetOwningNode()))
		{
			if (FromPin->Direction == EGPD_Output)
			{
				// 新节点作为源的目标
				if (FLeoScenarioNode* FromAsset = FindAssetNodeMutable(FromNode->NodeId))
				{
					FLeoScenarioEdge E;
					E.To = NewId;
					FromAsset->Edges.Add(E);
				}
			}
			else
			{
				// 从输入 pin 拖出：新节点作为源
				FLeoScenarioEdge E;
				E.To = FromNode->NodeId;
				N.Edges.Add(E);
			}
		}
	}

	if (bEntryDefault) { G->EntryNode = NewId; } // 首个节点自动成为入口
	G->PostEditChange();

	RebuildFromAsset();
	ULeoEdGraphNode* NewEd = FindNode(NewId);
	if (bSelectNewNode && OnNodeAdded) { OnNodeAdded(NewEd); }
	return NewEd;
}

void FLeoGraphMirror::AddEdge(ULeoEdGraphNode* From, ULeoEdGraphNode* To)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || !From || !To || From == To) { return; }
	FLeoScenarioNode* FromAsset = FindAssetNodeMutable(From->NodeId);
	if (!FromAsset) { return; }

	FScopedTransaction Transaction(LOCTEXT("AddEdgeTx", "添加连线"));
	G->Modify();
	FLeoScenarioEdge E;
	E.To = To->NodeId;
	FromAsset->Edges.Add(E); // 无条件边；条件/优先级在选中后 Details 编辑
	G->PostEditChange();

	// 增量：只新增边节点（禁止整树重建——本函数在 Schema pin 调用栈内执行，
	// 重建会把调用方正使用的 pin 所属节点 MarkAsGarbage）
	if (EdGraph.IsValid())
	{
		ULeoEdGraphNode_Edge* EdgeEd = NewObject<ULeoEdGraphNode_Edge>(EdGraph.Get());
		EdgeEd->CreateNewGuid();
		EdGraph->AddNode(EdgeEd, false, false);
		EdgeEd->FromNodeId = From->NodeId;
		EdgeEd->EdgeIndex = FromAsset->Edges.Num() - 1;
		EdgeEd->NodePosX = (From->NodePosX + To->NodePosX) / 2; // 占位；二段布局会重算
		EdgeEd->NodePosY = (From->NodePosY + To->NodePosY) / 2;
		EdgeEd->AllocateDefaultPins();
		if (UEdGraphPin* FromOut = From->GetOutputPin())
		{
			if (UEdGraphPin* EdgeIn = EdgeEd->GetInputPin()) { FromOut->MakeLinkTo(EdgeIn); }
		}
		if (UEdGraphPin* EdgeOut = EdgeEd->GetOutputPin())
		{
			if (UEdGraphPin* ToIn = To->GetInputPin()) { EdgeOut->MakeLinkTo(ToIn); }
		}
		EdGraph->NotifyNodeAdded(EdgeEd);
	}
	if (OnRebuilt) { OnRebuilt(); }
}

void FLeoGraphMirror::SetEntry(ULeoEdGraphNode* Target)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || !Target || G->EntryNode == Target->NodeId) { return; }

	FScopedTransaction Transaction(LOCTEXT("SetEntryTx", "设置入口"));
	G->Modify();
	G->EntryNode = Target->NodeId;
	G->PostEditChange();

	// 增量：只重连入口 pin 并挪位置（不整树重建，理由同 AddEdge）
	if (EdGraph.IsValid())
	{
		if (ULeoEdGraphNode_Entry* EntryEd = FindEntryNode())
		{
			if (UEdGraphPin* EntryOut = EntryEd->GetOutputPin())
			{
				EntryOut->BreakAllPinLinks(false);
				if (UEdGraphPin* TargetIn = Target->GetInputPin()) { EntryOut->MakeLinkTo(TargetIn); }
			}
			EntryEd->NodePosX = Target->NodePosX;
			EntryEd->NodePosY = Target->NodePosY - 200;
		}
	}
	if (OnRebuilt) { OnRebuilt(); }
}

ULeoEdGraphNode_Entry* FLeoGraphMirror::FindEntryNode() const
{
	if (!EdGraph.IsValid()) { return nullptr; }
	for (UEdGraphNode* N : EdGraph->Nodes)
	{
		if (ULeoEdGraphNode_Entry* Entry = Cast<ULeoEdGraphNode_Entry>(N)) { return Entry; }
	}
	return nullptr;
}

void FLeoGraphMirror::RemoveEdgeEdNodesInternal(const TArray<ULeoEdGraphNode_Edge*>& EdgeNodes)
{
	if (!EdGraph.IsValid()) { return; }

	// 按源节点记录被删下标，随后平移同源后续边下标（保持 (FromId, EdgeIndex) 定位有效）
	TMap<FName, TArray<int32>> RemovedByNode;
	for (ULeoEdGraphNode_Edge* E : EdgeNodes)
	{
		if (E) { RemovedByNode.FindOrAdd(E->FromNodeId).Add(E->EdgeIndex); }
	}

	for (ULeoEdGraphNode_Edge* E : EdgeNodes)
	{
		if (!E) { continue; }
		E->BreakAllNodeLinks();
		EdGraph->Nodes.Remove(E);
		EdGraph->NotifyNodeRemoved(E); // 面板只移除该节点的 widget
		E->MarkAsGarbage();
	}

	for (const TPair<FName, TArray<int32>>& Pair : RemovedByNode)
	{
		TArray<int32> Sorted = Pair.Value;
		Sorted.Sort();
		for (UEdGraphNode* N : EdGraph->Nodes)
		{
			if (ULeoEdGraphNode_Edge* E = Cast<ULeoEdGraphNode_Edge>(N))
			{
				if (E->FromNodeId == Pair.Key)
				{
					int32 Shift = 0;
					for (int32 Removed : Sorted) { if (E->EdgeIndex > Removed) { ++Shift; } }
					E->EdgeIndex -= Shift;
				}
			}
		}
	}
}

void FLeoGraphMirror::DeleteNodes(const TArray<ULeoEdGraphNode*>& Nodes)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || Nodes.IsEmpty()) { return; }

	TArray<FName> RemoveIds;
	for (ULeoEdGraphNode* N : Nodes)
	{
		if (N && !N->IsA<ULeoEdGraphNode_Entry>() && !N->NodeId.IsNone())
		{
			RemoveIds.AddUnique(N->NodeId);
		}
	}
	if (RemoveIds.IsEmpty()) { return; }

	FScopedTransaction Transaction(LOCTEXT("DeleteNodesTx", "删除节点"));
	G->Modify();
	G->Nodes.RemoveAll([&RemoveIds](const FLeoScenarioNode& N) { return RemoveIds.Contains(N.Id); });
	for (FLeoScenarioNode& N : G->Nodes) // 清悬空入边
	{
		N.Edges.RemoveAll([&RemoveIds](const FLeoScenarioEdge& E) { return RemoveIds.Contains(E.To); });
	}
	if (RemoveIds.Contains(G->EntryNode))
	{
		G->EntryNode = G->Nodes.Num() > 0 ? G->Nodes[0].Id : NAME_None;
	}
	G->PostEditChange();

	ActiveNodeId = NAME_None;
	RebuildFromAsset();
}

void FLeoGraphMirror::DeleteEdge(ULeoEdGraphNode_Edge* EdgeNode)
{
	DeleteEdges(TArray<ULeoEdGraphNode_Edge*>({ EdgeNode }));
}

void FLeoGraphMirror::DeleteEdges(const TArray<ULeoEdGraphNode_Edge*>& EdgeNodes)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G) { return; }

	FScopedTransaction Transaction(LOCTEXT("DeleteEdgeTx", "删除连线"));
	G->Modify();
	bool bChanged = false;
	for (ULeoEdGraphNode_Edge* EdgeNode : EdgeNodes)
	{
		if (!EdgeNode) { continue; }
		if (FLeoScenarioNode* FromAsset = FindAssetNodeMutable(EdgeNode->FromNodeId))
		{
			if (FromAsset->Edges.IsValidIndex(EdgeNode->EdgeIndex))
			{
				FromAsset->Edges.RemoveAt(EdgeNode->EdgeIndex);
				bChanged = true;
			}
		}
	}
	if (bChanged)
	{
		G->PostEditChange();
		// 增量：只移除对应边节点（断线可能发生在 pin 调用栈内，如 Alt 点线）
		RemoveEdgeEdNodesInternal(EdgeNodes);
		if (OnRebuilt) { OnRebuilt(); }
	}
}

void FLeoGraphMirror::RemoveAllLinks(FName NodeId, bool bIncludeOut, bool bIncludeIn)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || NodeId.IsNone()) { return; }

	bool bChanged = false;
	{
		FScopedTransaction Transaction(LOCTEXT("BreakLinksTx", "断开连线"));
		G->Modify();
		if (bIncludeOut)
		{
			if (FLeoScenarioNode* N = FindAssetNodeMutable(NodeId))
			{
				bChanged |= N->Edges.Num() > 0;
				N->Edges.Reset();
			}
		}
		if (bIncludeIn)
		{
			for (FLeoScenarioNode& N : G->Nodes)
			{
				const int32 Before = N.Edges.Num();
				N.Edges.RemoveAll([NodeId](const FLeoScenarioEdge& E) { return E.To == NodeId; });
				bChanged |= N.Edges.Num() != Before;
			}
		}
		if (bChanged)
		{
			G->PostEditChange();
		}
	}
	if (bChanged && EdGraph.IsValid())
	{
		// 增量：收集涉及的边节点后移除（BreakNodeLinks/BreakPinLinks 可能在 pin 调用栈内触发）
		TArray<ULeoEdGraphNode_Edge*> ToRemove;
		for (UEdGraphNode* N : EdGraph->Nodes)
		{
			ULeoEdGraphNode_Edge* E = Cast<ULeoEdGraphNode_Edge>(N);
			if (!E) { continue; }
			const bool bIsOut = (E->FromNodeId == NodeId);
			const bool bIsIn = !bIsOut && E->GetToNode() && E->GetToNode()->NodeId == NodeId;
			if ((bIncludeOut && bIsOut) || (bIncludeIn && bIsIn)) { ToRemove.Add(E); }
		}
		RemoveEdgeEdNodesInternal(ToRemove);
		if (OnRebuilt) { OnRebuilt(); }
	}
}

void FLeoGraphMirror::RenameCaption(ULeoEdGraphNode* Node, const FText& NewCaption)
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || !Node || Node->IsA<ULeoEdGraphNode_Entry>()) { return; }
	FLeoScenarioNode* N = FindAssetNodeMutable(Node->NodeId);
	if (!N || N->Caption.EqualTo(NewCaption)) { return; }

	FScopedTransaction Transaction(LOCTEXT("RenameTx", "重命名节点"));
	G->Modify();
	N->Caption = NewCaption;
	G->PostEditChange();

	RefreshNodeVisual(Node->NodeId);
	if (OnRebuilt) { OnRebuilt(); }
}

void FLeoGraphMirror::ApplyAutoLayout()
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || G->Nodes.Num() == 0) { return; }

	FScopedTransaction Transaction(LOCTEXT("AutoLayoutTx", "自动布局"));
	G->Modify();

	// 入口 BFS 分层；纵向流（BT 形态）：层深向下（Y），同层横向排开（X）
	TMap<FName, int32> Level;
	if (!G->EntryNode.IsNone())
	{
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
	}
	TMap<int32, int32> Column;
	for (FLeoScenarioNode& N : G->Nodes)
	{
		const int32 L = Level.Contains(N.Id) ? Level[N.Id] : 0;
		const int32 C = Column.Contains(L) ? Column[L] + 1 : 0;
		Column.Add(L, C);
		N.EditorPos = FVector2D(64.f + C * 230.f, 64.f + L * 170.f);
	}
	G->PostEditChange();

	RebuildFromAsset();
}

// ---- 位置提交 ----

void FLeoGraphMirror::TickSyncPositions()
{
	ULeoScenarioGraph* G = Asset.Get();
	if (!G || !EdGraph.IsValid()) { return; }

	// 拖拽中（0.25s 内有 MoveTo 活动）不提交
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMoveActivity < 0.25) { return; }

	TArray<TPair<FLeoScenarioNode*, FVector2D>> Moved;
	for (UEdGraphNode* N : EdGraph->Nodes)
	{
		ULeoEdGraphNode* LN = Cast<ULeoEdGraphNode>(N);
		if (!LN || LN->IsA<ULeoEdGraphNode_Entry>()) { continue; } // 入口位置不入库
		FLeoScenarioNode* AssetNode = FindAssetNodeMutable(LN->NodeId);
		if (!AssetNode) { continue; }
		const FVector2D PanelPos(LN->NodePosX, LN->NodePosY);
		if (!PanelPos.Equals(AssetNode->EditorPos, 0.5f))
		{
			Moved.Add(TPair<FLeoScenarioNode*, FVector2D>(AssetNode, PanelPos));
		}
	}
	if (Moved.IsEmpty()) { return; }

	FScopedTransaction Transaction(LOCTEXT("MoveNodesTx", "移动节点"));
	G->Modify();
	for (auto& P : Moved)
	{
		P.Key->EditorPos = P.Value;
	}
}

#undef LOCTEXT_NAMESPACE
