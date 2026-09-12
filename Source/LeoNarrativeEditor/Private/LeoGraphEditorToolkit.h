// Leo 编排图专属编辑器（T2 / v2 布局）：
// 单 Tab 宿纳整个编辑器——顶部工具条（加删/入口/自动布局/PIE）｜
// 主区 左列表 · 中画布（引擎同源渲染）· 右详情 ｜ 底部整宽干跑模拟抽屉。
// 编辑模型 = 直编资产（无 EdGraph 镜像）：选中节点经包装 UObject 在 Details 焦点编辑，
// NotifyPostChange 写回资产并标脏；画布只是渲染视图。
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Data/LeoScenarioGraph.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeoGraphCanvas.h"
#include "Widgets/Views/SListView.h"
#include "LeoGraphEditorToolkit.generated.h"

class IDetailsView;
class SLeoGraphCanvas;
class SLeoGraphSimulator;
class ULeoNodeEditWrapper;
class ULeoEdgeEditWrapper;

// 节点编辑包装：Details 面板焦点编辑的载体（写回 Owner->Nodes[SourceIndex]）
UCLASS()
class ULeoNodeEditWrapper : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "节点")
	FLeoScenarioNode Node;
	UPROPERTY()
	TObjectPtr<ULeoScenarioGraph> Owner;
	UPROPERTY()
	int32 SourceIndex = INDEX_NONE;
};

// 出边编辑包装：选中连线时 Details 焦点编辑（写回 Owner->Nodes[NodeIndex].Edges[EdgeIndex]）
UCLASS()
class ULeoEdgeEditWrapper : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "连线")
	FLeoScenarioEdge Edge;
	UPROPERTY()
	TObjectPtr<ULeoScenarioGraph> Owner;
	UPROPERTY()
	int32 NodeIndex = INDEX_NONE;
	UPROPERTY()
	int32 EdgeIndex = INDEX_NONE;
};

class FLeoGraphEditorToolkit : public FAssetEditorToolkit, public FNotifyHook
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

	// ---- FNotifyHook：Details 属性变更 → 写回资产 ----
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

	FName GetSelectedNodeId() const;

private:
	TSharedRef<SWidget> MakeGraphTab();       // 单 Tab：工具条 + 主区 + 模拟抽屉

	// 交互
	void OnCanvasSelection(const FLeoGraphSelection& Sel);
	void SelectNode(int32 Index);
	void ShowDetailsForSelection();
	void AddNode(ELeoScenarioNodeType Type);                  // 网格级联落位
	void AddNodeAt(ELeoScenarioNodeType Type, FVector2D Pos); // 右键指定位置（重叠则级联）
	void OnConnectRequested(int32 FromIdx, int32 ToIdx);      // 画布拖拽建边
	void OnEdgeSelected(int32 FromIdx, int32 EdgeIdx);        // 画布点击选边
	void DeleteSelectedEdge();                                // Delete 键 / 按钮
	void ShowCanvasContextMenu(FVector2D GraphPos, FVector2D ScreenPos);
	FVector2D FindFreePosition() const;                       // 第一个不与现有节点重叠的网格位
	void DeleteSelectedNode();
	void AutoLayout();                                        // 分层排布（层距=节点宽+96，行距=节点高+48）
	void RefreshAll();
	void RefreshNodeRows();

	// PIE 联动
	void OnBeginPIE(bool bIsSimulating);
	bool TickLiveHighlight(float Dt);

	ULeoScenarioGraph* GetGraph() const { return Graph.Get(); }

	TWeakObjectPtr<ULeoScenarioGraph> Graph;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SLeoGraphCanvas> Canvas;
	TSharedPtr<SLeoGraphSimulator> Simulator;
	TStrongObjectPtr<ULeoNodeEditWrapper> NodeWrapper;
	TStrongObjectPtr<ULeoEdgeEditWrapper> EdgeWrapper;
	FLeoGraphSelection Selection;
	FDelegateHandle BeginPIEHandle;
	FTSTicker::FDelegateHandle LiveTickHandle;
	bool bRunFromSelectionInPIE = false;

	// 节点列表
	TArray<TSharedPtr<FString>> NodeRows;
	TSharedPtr<SListView<TSharedPtr<FString>>> NodeList;
};
