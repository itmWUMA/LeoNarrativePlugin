// FLeoGraphMirror：资产 ↔ EdGraph 镜像的唯一变更入口。
// 职责：
// - Init/RebuildFromAsset：按 ULeoScenarioGraph 全量重建瞬态镜像（含入口隧道、边节点与 pin 连接）；
// - 变更入口（AddNode/AddEdge/SetEntry/DeleteNodes/DeleteEdge/RenameCaption/ApplyAutoLayout）：
//   统一 FScopedTransaction + 资产 Modify + 重建 + 回调刷新——撤销重做只还原资产，
//   PostUndo 后由 toolkit 触发 RebuildFromAsset 即恢复视图；
// - 位置提交：拖拽中只改 EdNode.NodePosX/Y（面板自带网格吸附），MoveTo 记录活动时间戳，
//   TickSyncPositions 在拖拽停止后一次性写回 EditorPos（一次拖拽恰好一个事务）；
// - SetActiveNode：PIE/干跑当前节点 → 卡片黄边框 + 出边变橙（属性直读，无需刷新）。
#pragma once

#include "CoreMinimal.h"
#include "Data/LeoScenarioGraph.h"

class UEdGraphPin;
class ULeoEdGraph;
class ULeoEdGraphNode;
class ULeoEdGraphNode_Entry;
class ULeoEdGraphNode_Edge;
class ULeoScenarioGraph;
struct FLeoScenarioNode;

class FLeoGraphMirror
{
public:
	~FLeoGraphMirror();

	void Init(ULeoScenarioGraph* InAsset);

	ULeoEdGraph* GetEdGraph() const { return EdGraph.Get(); }
	ULeoScenarioGraph* GetAsset() const { return Asset.Get(); }

	// 结构重建后回调（toolkit：刷新列表/详情/模拟器/选区）
	TFunction<void()> OnRebuilt;
	// 新增节点后回调（自动选中并居中）
	TFunction<void(ULeoEdGraphNode*)> OnNodeAdded;

	// ---- 变更入口（全部带事务）----
	ULeoEdGraphNode* AddNode(ELeoScenarioNodeType Type, const FVector2D& Pos, UEdGraphPin* FromPin = nullptr, bool bSelectNewNode = true);
	void AddEdge(ULeoEdGraphNode* From, ULeoEdGraphNode* To);
	void SetEntry(ULeoEdGraphNode* Target);
	void DeleteNodes(const TArray<ULeoEdGraphNode*>& Nodes);
	void DeleteEdge(ULeoEdGraphNode_Edge* EdgeNode);
	void DeleteEdges(const TArray<ULeoEdGraphNode_Edge*>& EdgeNodes);
	void RemoveAllLinks(FName NodeId, bool bIncludeOut, bool bIncludeIn); // BreakNodeLinks/BreakPinLinks 落点
	void RenameCaption(ULeoEdGraphNode* Node, const FText& NewCaption);
	void ApplyAutoLayout();

	// ---- 视图 ----
	void RebuildFromAsset();
	ULeoEdGraphNode* FindNode(FName NodeId) const;
	ULeoEdGraphNode_Edge* FindEdge(FName FromNodeId, int32 EdgeIndex) const;
	void RefreshNodeVisual(FName NodeId);
	void SetActiveNode(FName NodeId);

	// ---- 位置提交（toolkit 高频 ticker 调用）----
	void NoteMoveActivity() { LastMoveActivity = FPlatformTime::Seconds(); }
	void TickSyncPositions();

private:
	FName GenerateNodeId() const;
	FLeoScenarioNode* FindAssetNodeMutable(FName NodeId) const;
	ULeoEdGraphNode_Entry* FindEntryNode() const;
	// 增量移除边节点并平移同源节点的边下标（供 pin 调用栈内的断线路径使用，禁止整树重建）
	void RemoveEdgeEdNodesInternal(const TArray<ULeoEdGraphNode_Edge*>& EdgeNodes);

	TWeakObjectPtr<ULeoScenarioGraph> Asset;
	TStrongObjectPtr<ULeoEdGraph> EdGraph;

	FName ActiveNodeId;
	double LastMoveActivity = 0.0;
};
