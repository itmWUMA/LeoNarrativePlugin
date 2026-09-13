// 编排图编辑器的瞬态 EdGraph 镜像：
// ULeoScenarioGraph 资产仍是唯一事实源（序列化/运行时/校验零变化）；
// 本图只是会话内编辑视图——节点卡片与连线由 SGraphPanel 按引擎标准管线渲染
// （卡片复刻 SGraphNode_BehaviorTree，连线复刻 FStateMachineConnectionDrawingPolicy）。
// 一切资产变更必须经 FLeoGraphMirror 控制器（事务 + 镜像重建），不许绕过。
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "LeoEdGraph.generated.h"

class FLeoGraphMirror;
class ULeoEdGraphNode;
class ULeoScenarioGraph;

UCLASS()
class ULeoEdGraph : public UEdGraph
{
	GENERATED_BODY()
public:
	// 资产弱引用（toolkit 持有资产；镜像不拥有、不序列化）
	TWeakObjectPtr<ULeoScenarioGraph> LeoGraph;

	// 控制器回指（Mirror 以 TStrongObjectPtr 拥有本图，裸指针安全）
	FLeoGraphMirror* Mirror = nullptr;

	ULeoScenarioGraph* GetLeoGraph() const { return LeoGraph.Get(); }
	FLeoGraphMirror* GetMirror() const { return Mirror; }

	// 按 NodeId 找镜像节点（规模小，线性扫描）
	ULeoEdGraphNode* FindLeoNode(FName NodeId) const;

	// 广播「新增单个节点」的精确变更（面板只为该节点补建 widget，不动其他节点；
	// 用于 schema pin 调用栈内的增量更新——整树重建会把调用方正使用的 pin 标记为垃圾）
	void NotifyNodeAdded(UEdGraphNode* Node);
	void NotifyNodeRemoved(UEdGraphNode* Node);
};
