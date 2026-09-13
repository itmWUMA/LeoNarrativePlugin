// 编排图属性定制（P0 编辑体验）：
// - FLeoEdgeAction：Key 变量名建议（整值替换）+ Operation 枚举下拉（原生）+ Expr 变量补全
// - FLeoScenarioEdge：Condition 双模式编辑（SLeoConditionEditor——默认下拉条件行 / 自定义表达式），
//   To/Priority/Actions 原生控件
// 变量候选来自 FLeoVariableHarvest 收割注册表（脚本 set/setg + 图副作用，随源推导不落盘）。
#pragma once

#include "Containers/UnrealString.h"
#include "Framework/SlateDelegates.h"
#include "Misc/Attribute.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

class IPropertyHandle;
class SEditableTextBox;
class SMenuAnchor;
class STableViewBase;
class SVerticalBox;
class ITableRow;
struct FLeoKnownVar;
struct FLeoCondRow;

// 文本框 + 变量建议弹出：
// Whole    —— 整文本即变量名（Key 行），前缀过滤，选中整值替换
// Trailing —— 表达式补全（Condition/Expr 行），取尾部标识符前缀，选中只替换该前缀
class SLeoVarText : public SCompoundWidget
{
public:
	enum class EMode : uint8 { Whole, Trailing };

	SLATE_BEGIN_ARGS(SLeoVarText)
		: _Mode(EMode::Trailing)
		{}
		SLATE_EVENT(FOnTextChanged, OnTextChanged)
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
		SLATE_ARGUMENT(FText, InitialText)
		SLATE_ARGUMENT(EMode, Mode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FSuggestionPtr = TSharedPtr<FLeoKnownVar>;

	void HandleTextChanged(const FText& NewText);
	void HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
	TSharedRef<SWidget> MakeMenuContent();
	TSharedRef<ITableRow> MakeRow(FSuggestionPtr Item, const TSharedRef<STableViewBase>& Owner) const;
	void HandlePick(FSuggestionPtr Item);
	void UpdateSuggestions();
	FString TrailingToken(const FString& Text) const;

	FOnTextChanged TextChangedEvent;
	FOnTextCommitted TextCommittedEvent;
	EMode Mode = EMode::Trailing;

	TSharedPtr<SEditableTextBox> Box;
	TSharedPtr<SMenuAnchor> Anchor;
	TSharedPtr<STableViewBase> List;
	TArray<FSuggestionPtr> Suggestions;
};

// 条件提交委托：参数为完整 .leo 表达式串（仍是唯一存储格式）
DECLARE_DELEGATE_OneParam(FOnLeoConditionCommitted, const FString&);

// 条件双模式编辑器：
// - 下拉模式（默认）：N 行「变量 比较符 值」，行间即 &&；0 行 = 无条件（顺序/默认边）
// - 自定义模式：直接编辑表达式（带变量补全）；切回下拉要求当前串可拆解
// 两种模式编译到同一个表达式串——运行时语义零改动
enum class ELeoCondEditMode : uint8 { Structured, Custom };

class SLeoConditionEditor : public SCompoundWidget
{
public:
	using FVarOption = TSharedPtr<FName>;
	using FOpOption = TSharedPtr<struct FLeoCondOpInfo>;
	using FModeOption = TSharedPtr<ELeoCondEditMode>;

	SLATE_BEGIN_ARGS(SLeoConditionEditor) {}
		SLATE_EVENT(FOnLeoConditionCommitted, OnCommitted)
		SLATE_ARGUMENT(FString, InitialCondition)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	void SwitchMode(ELeoCondEditMode NewMode);
	void CommitRows();
	void CommitCustom(const FString& Text);
	void AddRow();
	void RemoveRow(int32 RowIdx);
	void RebuildRows();          // Rows 变动后重建行控件 + 刷新变量候选
	void RefreshVarOptions();

	TSharedRef<SWidget> MakeRowWidget(int32 RowIdx);
	TSharedRef<SWidget> MakeModeCombo();

	FOnLeoConditionCommitted OnCommitted;
	TArray<FLeoCondRow> Rows;
	FString CustomText;                      // 自定义模式当前文本
	ELeoCondEditMode Mode = ELeoCondEditMode::Structured;
	FString Error;                           // 值非法 / 无法切模式时的行内提示

	TArray<FVarOption> VarOptions;
	TSharedPtr<SVerticalBox> RowsBox;
	TSharedPtr<SVerticalBox> StructuredArea;
	TSharedPtr<SVerticalBox> CustomArea;
};

// 挂到 PropertyEditor 的结构定制：注册/反注册见 LeoGraphCustomizations 注册函数
class FLeoEdgeActionCustomization;
class FLeoScenarioEdgeCustomization;

void RegisterLeoGraphCustomizations();
void UnregisterLeoGraphCustomizations();
