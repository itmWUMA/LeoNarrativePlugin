#include "Graph/LeoEdGraph.h"

#include "EdGraph/EdGraphSchema.h"
#include "Graph/LeoEdGraphNodes.h"
#include "GraphEditAction.h"

ULeoEdGraphNode* ULeoEdGraph::FindLeoNode(FName NodeId) const
{
	for (UEdGraphNode* N : Nodes)
	{
		if (ULeoEdGraphNode* Leo = Cast<ULeoEdGraphNode>(N))
		{
			if (Leo->NodeId == NodeId) { return Leo; }
		}
	}
	return nullptr;
}

void ULeoEdGraph::NotifyNodeAdded(UEdGraphNode* Node)
{
	FEdGraphEditAction Action;
	Action.Graph = this;
	Action.Action = GRAPHACTION_AddNode;
	Action.Nodes.Add(Node);
	Action.bUserInvoked = true;
	NotifyGraphChanged(Action);
}

void ULeoEdGraph::NotifyNodeRemoved(UEdGraphNode* Node)
{
	FEdGraphEditAction Action;
	Action.Graph = this;
	Action.Action = GRAPHACTION_RemoveNode;
	Action.Nodes.Add(Node);
	Action.bUserInvoked = true;
	NotifyGraphChanged(Action);
}
