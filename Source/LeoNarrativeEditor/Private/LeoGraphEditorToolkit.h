// Leo 编排图专属编辑器：
// 单 Tab 宿纳整个编辑器——顶部工具条（加节点/删除/自动布局/PIE）｜
// 主区 左列表 · 中画布（SGraphEditor：BT 卡片 + 状态机连线）· 右详情 ｜ 底部整宽干跑模拟抽屉。
// 编辑模型：资产 ULeoScenarioGraph 唯一事实源，画布 = ULeoEdGraph 瞬态镜像（FLeoGraphMirror 维护）；
// 一切变更经 Mirror 走 FScopedTransaction → Ctrl+Z 可撤；撤销后重建镜像即恢复视图。
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Data/LeoScenarioGraph.h"
#include "EditorUndoClient.h"
#include "Graph/LeoGraphMirror.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "LeoGraphEditorToolkit.generated.h"

class IDetailsView;
class SGraphEditor;
class SLeoGraphSimulator;
class ULeoEdgeEditWrapper;
class ULeoNodeEditWrapper;
class FUICommandList;
class UEdGraphNode;

// 节点编辑包装：Details 面板焦点编辑的载体（写回 Owner 中 NodeId 对应的节点）
UCLASS()
class ULeoNodeEditWrapper : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "节点")
	FLeoScenarioNode Node;
	UPROPERTY()
	TObjectPtr<ULeoScenarioGraph> Owner;
	FName NodeId; // 非 UPROPERTY：会话内定位（写回时按 Id 解析，抗数组重排）
};

// 出边编辑包装：选中连线时 Details 焦点编辑（写回 Owner 中 FromNodeId 节点的 Edges[EdgeIndex]）
UCLASS()
class ULeoEdgeEditWrapper : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "连线")
	FLeoScenarioEdge Edge;
	UPROPERTY()
	TObjectPtr<ULeoScenarioGraph> Owner;
	FName FromNodeId; // 非 UPROPERTY
	int32 EdgeIndex = INDEX_NONE;
};

class FLeoGraphEditorToolkit : public FAssetEditorToolkit, public FNotifyHook, public FEditorUndoClient
{
public:
	void InitLeoGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& Host,
		ULeoScenarioGraph* InGraph);
	virtual ~FLeoGraphEditorToolkit();

	// ---- FAssetEditorToolkit ----
	virtual FName GetToolkitFName() const override { return FName(TEXT("LeoGraphEditor")); }
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("LeoGraph"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.35f, 0.25f, 0.55f, 0.5f); }
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

	// ---- 撤销/重做（FEditorUndoClient）：资产已还原 → 重建镜像 ----
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// ---- FNotifyHook：Details 属性变更 → 写回资产 ----
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

	FName GetSelectedNodeId() const { return SelectedNodeId; }

private:
	TSharedRef<SWidget> MakeGraphTab();       // 单 Tab：工具条 + 主区 + 模拟抽屉
	void BindGraphCommands();                 // Delete 等命令 → Mirror

	// 画布交互
	void OnGraphSelectionChanged(const TSet<class UObject*>& NewSelection);
	void OnNodeDoubleClicked(UEdGraphNode* Node);
	void DeleteSelected();
	void SelectNodeById(FName NodeId);        // 左列表 → 画布选中并跳转
	void ShowDetailsForSelection();
	void AddNode(ELeoScenarioNodeType Type);  // 工具条（网格找空位）
	FVector2D FindFreePosition() const;       // 第一个不与现有节点重叠的网格位
	void AutoLayout();
	void RefreshNodeRows();
	void RefreshAll();
	void OnMirrorRebuilt();                   // 镜像重建回调（刷新 + 失效选中清理）

	// PIE/干跑联动
	void OnBeginPIE(bool bIsSimulating);
	bool TickEditor(float Dt);                // 0.1s：位置提交同步 + PIE 高亮
	void OnSimCurrentNode(FName NodeId);      // 干跑当前节点 → 画布高亮

	ULeoScenarioGraph* GetGraph() const { return Graph.Get(); }
	FLeoGraphMirror* GetMirror() const { return Mirror.Get(); }

	TWeakObjectPtr<ULeoScenarioGraph> Graph;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SGraphEditor> GraphEditorPtr;
	TSharedPtr<FUICommandList> GraphCommands;
	TUniquePtr<FLeoGraphMirror> Mirror;
	TSharedPtr<SLeoGraphSimulator> Simulator;
	TStrongObjectPtr<ULeoNodeEditWrapper> NodeWrapper;
	TStrongObjectPtr<ULeoEdgeEditWrapper> EdgeWrapper;

	// 当前选中（镜像重建后按 Id/边定位，抗数组重排）
	FName SelectedNodeId;
	FName SelectedEdgeFromId;
	int32 SelectedEdgeIndex = INDEX_NONE;

	FDelegateHandle BeginPIEHandle;
	FTSTicker::FDelegateHandle LiveTickHandle;
	bool bRunFromSelectionInPIE = false;
	bool bSyncingListSelection = false; // 列表↔画布选中同步防环

	// 节点列表（按 Id 稳定）
	struct FLeoNodeRow { FName Id; FString Label; };
	TArray<TSharedPtr<FLeoNodeRow>> NodeRows;
	TSharedPtr<SListView<TSharedPtr<FLeoNodeRow>>> NodeList;
};
