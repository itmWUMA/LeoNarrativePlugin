#include "LeoGraphCustomizations.h"

#include "LeoConditionCodec.h"
#include "LeoVariableHarvest.h"

#include "Data/LeoScenarioGraph.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ScriptRuntime/LeoScriptBridge.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "LeoGraphCustomizations"

// ---- SLeoVarText：文本框 + 变量建议 ----

void SLeoVarText::Construct(const FArguments& InArgs)
{
	TextChangedEvent = InArgs._OnTextChanged;
	TextCommittedEvent = InArgs._OnTextCommitted;
	Mode = InArgs._Mode;

	ChildSlot
	[
		SNew(SBox)
		[
			SAssignNew(Anchor, SMenuAnchor)
			.Placement(EMenuPlacement::MenuPlacement_ComboBox)
			.OnGetMenuContent(this, &SLeoVarText::MakeMenuContent)
			[
				SAssignNew(Box, SEditableTextBox)
				.Text(InArgs._InitialText)
				.SelectAllTextWhenFocused(false)
				.RevertTextOnEscape(false)
				.OnTextChanged(this, &SLeoVarText::HandleTextChanged)
				.OnTextCommitted(this, &SLeoVarText::HandleTextCommitted)
			]
		]
	];
}

void SLeoVarText::HandleTextChanged(const FText& NewText)
{
	TextChangedEvent.ExecuteIfBound(NewText);
	UpdateSuggestions();
}

void SLeoVarText::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter) { return; }
	if (Anchor.IsValid()) { Anchor->SetIsOpen(false); }
	TextCommittedEvent.ExecuteIfBound(NewText, CommitType);
}

TSharedRef<SWidget> SLeoVarText::MakeMenuContent()
{
	return SNew(SBox)
		.WidthOverride(280.f)
		.HeightOverride(180.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
			.Padding(2.f)
			[
				SNew(SListView<FSuggestionPtr>)
				.ListItemsSource(&Suggestions)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SLeoVarText::MakeRow)
				.OnMouseButtonClick(this, &SLeoVarText::HandlePick)
			]
		];
}

TSharedRef<ITableRow> SLeoVarText::MakeRow(FSuggestionPtr Item, const TSharedRef<STableViewBase>& Owner) const
{
	FText Tooltip;
	if (Item.IsValid())
	{
		TArray<FString> Bits;
		Bits.Add(Item->bWritten
			? (Item->bGlobal ? TEXT("全局变量（setg/边副作用 bGlobal）") : TEXT("局部变量（set/边副作用）"))
			: TEXT("仅被引用、未见写入（可能由游戏代码定义，拼写错误高发区）"));
		if (!Item->Kind.IsEmpty()) { Bits.Add(FString::Printf(TEXT("类型提示: %s"), *Item->Kind)); }
		if (!Item->Source.IsNone()) { Bits.Add(FString::Printf(TEXT("来源: %s"), *Item->Source.ToString())); }
		Tooltip = FText::FromString(FString::Join(Bits, TEXT("\n")));
	}
	return SNew(STableRow<FSuggestionPtr>, Owner)
		.ToolTipText(Tooltip)
		[
			SNew(STextBlock).Text(Item.IsValid() ? FText::FromName(Item->Name) : FText::GetEmpty())
		];
}

void SLeoVarText::HandlePick(FSuggestionPtr Item)
{
	if (!Item.IsValid() || !Box.IsValid()) { return; }
	FString Text = Box->GetText().ToString();
	FString NewText;
	if (Mode == EMode::Whole)
	{
		NewText = Item->Name.ToString();
	}
	else
	{
		NewText = Text.LeftChop(TrailingToken(Text).Len()) + Item->Name.ToString();
	}
	Box->SetText(FText::FromString(NewText));
	if (Anchor.IsValid()) { Anchor->SetIsOpen(false); }
	TextCommittedEvent.ExecuteIfBound(FText::FromString(NewText), ETextCommit::OnEnter);
}

void SLeoVarText::UpdateSuggestions()
{
	if (!Box.IsValid() || !Anchor.IsValid()) { return; }
	const FString Text = Box->GetText().ToString();
	const FString Prefix = (Mode == EMode::Whole) ? Text.TrimStartAndEnd() : TrailingToken(Text);

	Suggestions.Reset();
	if (!Prefix.IsEmpty() || Mode == EMode::Whole)
	{
		const TMap<FName, FLeoKnownVar>& Vars = FLeoVariableHarvest::Get().GetVars();
		for (const TPair<FName, FLeoKnownVar>& KV : Vars)
		{
			if (KV.Key.ToString().StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				Suggestions.Add(MakeShared<FLeoKnownVar>(KV.Value));
				if (Suggestions.Num() >= 8) { break; }
			}
		}
	}
	if (List.IsValid()) { List->RequestListRefresh(); }
	Anchor->SetIsOpen(Suggestions.Num() > 0, /*bFocusMenu*/false);
}

FString SLeoVarText::TrailingToken(const FString& Text) const
{
	// 尾部标识符：字母/数字/下划线 + 非 ASCII（变量名词法上允许 CJK）
	int32 End = Text.Len();
	while (End > 0)
	{
		const TCHAR C = Text[End - 1];
		const bool bIdent = FChar::IsAlnum(C) || C == TEXT('_') || C >= 0x80;
		if (!bIdent) { break; }
		--End;
	}
	return Text.RightChop(End);
}

// ---- SLeoConditionEditor：条件双模式（下拉行 / 自定义表达式） ----

namespace
{
	TArray<SLeoConditionEditor::FOpOption> GetOpOptions()
	{
		TArray<SLeoConditionEditor::FOpOption> Options;
		for (const FLeoCondOpInfo& Info : LeoConditionCodec::GetOps())
		{
			Options.Add(MakeShared<FLeoCondOpInfo>(Info));
		}
		return Options;
	}
}

void SLeoConditionEditor::Construct(const FArguments& InArgs)
{
	OnCommitted = InArgs._OnCommitted;
	CustomText = InArgs._InitialCondition;
	Mode = LeoConditionCodec::Parse(CustomText, Rows)
		? ELeoCondEditMode::Structured
		: ELeoCondEditMode::Custom; // 拆不开的存量表达式直接进自定义模式

	RefreshVarOptions();

	ChildSlot
	[
		SNew(SVerticalBox)

		// 模式行
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("CondMode", "模式"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
			[
				MakeModeCombo()
			]
		]

		// 下拉模式
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(StructuredArea, SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return Rows.IsEmpty()
						? LOCTEXT("CondEmpty", "（无条件——顺序边 / 默认边）")
						: LOCTEXT("CondRowsHint", "全部满足时才走这条边（行间 = 且）");
				})
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(RowsBox, SVerticalBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("CondAddRow", "+ 添加条件"))
				.ButtonStyle(FAppStyle::Get(), "Button")
				.ContentPadding(FMargin(4, 1))
				.HAlign(HAlign_Left)
				.OnClicked_Lambda([this]()
				{
					AddRow();
					return FReply::Handled();
				})
			]
		]

		// 自定义模式
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(CustomArea, SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SLeoVarText)
				.Mode(SLeoVarText::EMode::Trailing)
				.InitialText(FText::FromString(CustomText))
				.OnTextCommitted(FOnTextCommitted::CreateLambda([this](const FText& Text, ETextCommit::Type)
				{
					CommitCustom(Text.ToString());
				}))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CondCustomHint", "完整 .leo 表达式（输入变量名有补全）"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		]

		// 行内错误（值非法 / 无法切回下拉模式）
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return FText::FromString(Error); })
			.ColorAndOpacity(FStyleColors::Error)
			.Visibility_Lambda([this]() { return Error.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		]
	];

	RebuildRows();
	StructuredArea->SetVisibility(Mode == ELeoCondEditMode::Structured ? EVisibility::Visible : EVisibility::Collapsed);
	CustomArea->SetVisibility(Mode == ELeoCondEditMode::Custom ? EVisibility::Visible : EVisibility::Collapsed);
}

TSharedRef<SWidget> SLeoConditionEditor::MakeModeCombo()
{
	static const TArray<FModeOption> Modes =
	{
		MakeShared<ELeoCondEditMode>(ELeoCondEditMode::Structured),
		MakeShared<ELeoCondEditMode>(ELeoCondEditMode::Custom),
	};
	return SNew(SComboBox<FModeOption>)
		.OptionsSource(&Modes)
		.OnGenerateWidget_Lambda([](FModeOption M)
		{
			return SNew(STextBlock).Text(*M == ELeoCondEditMode::Structured
				? LOCTEXT("ModeStructured", "下拉条件")
				: LOCTEXT("ModeCustom", "自定义表达式"));
		})
		.OnSelectionChanged_Lambda([this](FModeOption M, ESelectInfo::Type)
		{
			if (M.IsValid()) { SwitchMode(*M); }
		})
		.Content()
		[
			SNew(STextBlock).Text_Lambda([this]()
			{
				return Mode == ELeoCondEditMode::Structured
					? LOCTEXT("ModeStructured", "下拉条件")
					: LOCTEXT("ModeCustom", "自定义表达式");
			})
		];
}

void SLeoConditionEditor::SwitchMode(ELeoCondEditMode NewMode)
{
	if (NewMode == Mode) { return; }
	if (NewMode == ELeoCondEditMode::Structured)
	{
		TArray<FLeoCondRow> Parsed;
		if (!LeoConditionCodec::Parse(CustomText, Parsed))
		{
			Error = TEXT("当前表达式无法拆解为条件行（含 || 或复杂子式）——先简化或清空");
			return;
		}
		Rows = MoveTemp(Parsed);
	}
	else
	{
		CustomText = LeoConditionCodec::Generate(Rows);
	}
	Error.Reset();
	Mode = NewMode;
	StructuredArea->SetVisibility(Mode == ELeoCondEditMode::Structured ? EVisibility::Visible : EVisibility::Collapsed);
	CustomArea->SetVisibility(Mode == ELeoCondEditMode::Custom ? EVisibility::Visible : EVisibility::Collapsed);
	RebuildRows();
	if (Mode == ELeoCondEditMode::Custom)
	{
		CommitCustom(CustomText); // 模式切换不改语义，但把行生成的规范化串写回
	}
}

void SLeoConditionEditor::CommitRows()
{
	const FString Text = LeoConditionCodec::Generate(Rows);
	leo::FLeoDiag D;
	if (!Text.IsEmpty() && !LeoBridge::CompileExpr(Text, D))
	{
		Error = FString::Printf(TEXT("条件无法编译（多半是值填错：%s）"), LeoBridge::DiagName(D.Code));
		return;
	}
	Error.Reset();
	OnCommitted.ExecuteIfBound(Text);
}

void SLeoConditionEditor::CommitCustom(const FString& Text)
{
	leo::FLeoDiag D;
	if (!Text.TrimStartAndEnd().IsEmpty() && !LeoBridge::CompileExpr(Text, D))
	{
		Error = FString::Printf(TEXT("表达式无法编译：%s"), LeoBridge::DiagName(D.Code));
		return;
	}
	Error.Reset();
	CustomText = Text;
	OnCommitted.ExecuteIfBound(Text);
}

void SLeoConditionEditor::AddRow()
{
	FLeoCondRow Row;
	Row.Var = VarOptions.IsEmpty() ? NAME_None : *VarOptions[0];
	Rows.Add(MoveTemp(Row));
	RebuildRows();
	CommitRows();
}

void SLeoConditionEditor::RemoveRow(int32 RowIdx)
{
	if (Rows.IsValidIndex(RowIdx)) { Rows.RemoveAt(RowIdx); }
	RebuildRows();
	CommitRows();
}

void SLeoConditionEditor::RefreshVarOptions()
{
	VarOptions.Reset();
	for (const FName& Name : FLeoVariableHarvest::Get().GetSortedNames())
	{
		VarOptions.Add(MakeShared<FName>(Name));
	}
}

void SLeoConditionEditor::RebuildRows()
{
	RefreshVarOptions();
	if (!RowsBox.IsValid()) { return; }
	RowsBox->ClearChildren();
	for (int32 Idx = 0; Idx < Rows.Num(); ++Idx)
	{
		RowsBox->AddSlot().AutoHeight().Padding(0, 1)
		[
			MakeRowWidget(Idx)
		];
	}
}

TSharedRef<SWidget> SLeoConditionEditor::MakeRowWidget(int32 Idx)
{
	const static TArray<FOpOption> OpOptions = GetOpOptions();
	return SNew(SHorizontalBox)

		// 变量
		+ SHorizontalBox::Slot().AutoWidth().MaxWidth(190)
		[
			SNew(SComboBox<FVarOption>)
			.OptionsSource(&VarOptions)
			.OnGenerateWidget_Lambda([](FVarOption Item)
			{
				return SNew(STextBlock).Text(FText::FromName(*Item));
			})
			.OnSelectionChanged_Lambda([this, Idx](FVarOption Item, ESelectInfo::Type)
			{
				if (Item.IsValid() && Rows.IsValidIndex(Idx))
				{
					Rows[Idx].Var = *Item;
					CommitRows();
				}
			})
			.Content()
			[
				SNew(STextBlock).Text_Lambda([this, Idx]()
				{
					return Rows.IsValidIndex(Idx) && !Rows[Idx].Var.IsNone()
						? FText::FromName(Rows[Idx].Var)
						: LOCTEXT("PickVar", "选择变量…");
				})
			]
		]

		// 比较符
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0).VAlign(VAlign_Center)
		[
			SNew(SComboBox<FOpOption>)
			.OptionsSource(&OpOptions)
			.OnGenerateWidget_Lambda([](FOpOption Item)
			{
				return SNew(STextBlock).Text(FText::FromString(Item->Display));
			})
			.OnSelectionChanged_Lambda([this, Idx](FOpOption Item, ESelectInfo::Type)
			{
				if (Item.IsValid() && Rows.IsValidIndex(Idx))
				{
					Rows[Idx].Op = Item->Op;
					CommitRows();
				}
			})
			.Content()
			[
				SNew(STextBlock).Text_Lambda([this, Idx]()
				{
					const FLeoCondOpInfo* Info = Rows.IsValidIndex(Idx) ? LeoConditionCodec::FindOp(Rows[Idx].Op) : nullptr;
					return Info ? FText::FromString(Info->Display) : FText::GetEmpty();
				})
			]
		]

		// 值（为真/为假无值）
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.Text_Lambda([this, Idx]()
			{
				return Rows.IsValidIndex(Idx) ? FText::FromString(Rows[Idx].Value) : FText::GetEmpty();
			})
			.ToolTipText(LOCTEXT("CondValueTip", "字面量值：数字 / true / false / \"文本\""))
			.OnTextCommitted_Lambda([this, Idx](const FText& Text, ETextCommit::Type)
			{
				if (Rows.IsValidIndex(Idx))
				{
					Rows[Idx].Value = Text.ToString();
					CommitRows();
				}
			})
			.Visibility_Lambda([this, Idx]()
			{
				const bool bNoValue = Rows.IsValidIndex(Idx)
					&& (Rows[Idx].Op == ELeoCondOp::IsTrue || Rows[Idx].Op == ELeoCondOp::IsFalse);
				return bNoValue ? EVisibility::Hidden : EVisibility::Visible;
			})
		]

		// 删行（标准 Button 样式：样式名必须真实存在于样式集，曾用不存在的
		// "NoBorderFlatButton" 导致查找失败、按钮渲染成裸文本）
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("CondRemoveRow", "删除"))
			.ButtonStyle(FAppStyle::Get(), "Button")
			.ContentPadding(FMargin(4, 1))
			.ToolTipText(LOCTEXT("CondRemoveRowTip", "删除此条件行"))
			.OnClicked_Lambda([this, Idx]()
			{
				RemoveRow(Idx);
				return FReply::Handled();
			})
		];
}

// ---- 结构定制：FLeoEdgeAction / FLeoScenarioEdge ----

namespace
{

// 当前值快照（初始显示用；提交走 SetValueFromFormattedString 走标准事务）
FString HandleString(const TSharedPtr<IPropertyHandle>& Handle)
{
	FString Value;
	if (Handle.IsValid())
	{
		Handle->GetValueAsFormattedString(Value);
	}
	return Value;
}

// 建议文本框行：名字原生 + 值区自绘 SLeoVarText，提交写回属性
void AddVarTextRow(IDetailChildrenBuilder& Builder, const TSharedPtr<IPropertyHandle>& Handle, SLeoVarText::EMode Mode)
{
	IDetailPropertyRow& Row = Builder.AddProperty(Handle.ToSharedRef());
	Row.CustomWidget()
		.NameContent()
		[
			Handle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(500.f)
		[
			SNew(SLeoVarText)
			.Mode(Mode)
			.InitialText(FText::FromString(HandleString(Handle)))
			.OnTextCommitted(FOnTextCommitted::CreateLambda([Handle](const FText& Text, ETextCommit::Type)
			{
				if (Handle.IsValid()) { Handle->SetValueFromFormattedString(Text.ToString()); }
			}))
		];
}

} // namespace

// FLeoEdgeAction：Key（变量建议）→ bGlobal/Operation（原生）→ Expr（变量补全）
class FLeoEdgeActionCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShared<FLeoEdgeActionCustomization>(); }

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils&) override
	{
		const TSharedPtr<IPropertyHandle> KeyH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, Key));
		const TSharedPtr<IPropertyHandle> OpH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, Operation));
		const TSharedPtr<IPropertyHandle> ExprH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, Expr));
		HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(400.f)
		[
			SNew(STextBlock)
			.Text_Lambda([KeyH, OpH, ExprH]()
			{
				return FText::FromString(FString::Printf(TEXT("%s %s %s"),
					*HandleString(KeyH), *HandleString(OpH), *HandleString(ExprH)));
			})
		];
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils&) override
	{
		const TSharedPtr<IPropertyHandle> KeyH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, Key));
		const TSharedPtr<IPropertyHandle> GlobalH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, bGlobal));
		const TSharedPtr<IPropertyHandle> OpH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, Operation));
		const TSharedPtr<IPropertyHandle> ExprH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoEdgeAction, Expr));

		AddVarTextRow(StructBuilder, KeyH, SLeoVarText::EMode::Whole);
		StructBuilder.AddProperty(GlobalH.ToSharedRef());
		StructBuilder.AddProperty(OpH.ToSharedRef()); // 枚举 → 原生下拉
		AddVarTextRow(StructBuilder, ExprH, SLeoVarText::EMode::Trailing);
	}
};

// FLeoScenarioEdge：To/Priority/Actions 原生，Condition 变量补全
class FLeoScenarioEdgeCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShared<FLeoScenarioEdgeCustomization>(); }

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils&) override
	{
		HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(400.f)
		[
			SNew(STextBlock)
			.Text_Lambda([StructPropertyHandle]()
			{
				const TSharedPtr<IPropertyHandle> CondH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoScenarioEdge, Condition));
				const FString Cond = HandleString(CondH);
				return FText::FromString(Cond.TrimStartAndEnd().IsEmpty() ? TEXT("(无条件)") : Cond);
			})
		];
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils&) override
	{
		const TSharedPtr<IPropertyHandle> ToH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoScenarioEdge, To));
		const TSharedPtr<IPropertyHandle> CondH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoScenarioEdge, Condition));
		const TSharedPtr<IPropertyHandle> PrioH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoScenarioEdge, Priority));
		const TSharedPtr<IPropertyHandle> ActionsH = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLeoScenarioEdge, Actions));

		StructBuilder.AddProperty(ToH.ToSharedRef());

		// 条件：双模式编辑（默认下拉行，可切自定义表达式）
		IDetailPropertyRow& CondRow = StructBuilder.AddProperty(CondH.ToSharedRef());
		CondRow.CustomWidget()
			.NameContent()
			[
				CondH->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(420.f)
			.MaxDesiredWidth(650.f)
			[
				SNew(SLeoConditionEditor)
				.InitialCondition(HandleString(CondH))
				.OnCommitted(FOnLeoConditionCommitted::CreateLambda([CondH](const FString& Text)
				{
					if (CondH.IsValid()) { CondH->SetValueFromFormattedString(Text); }
				}))
			];

		StructBuilder.AddProperty(PrioH.ToSharedRef());
		StructBuilder.AddProperty(ActionsH.ToSharedRef()).ShouldAutoExpand(true); // 元素行走 FLeoEdgeActionCustomization
	}
};

// ---- 注册 ----

void RegisterLeoGraphCustomizations()
{
	FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	// 注意：注册键 = UScriptStruct 的反射名，UHT 会剥掉 F 前缀（"LeoEdgeAction"/"LeoScenarioEdge"），
	// 手写字符串带 F 前缀会静默不匹配（定制不生效）。用 StaticStruct()->GetFName() 从根上杜绝。
	const FName EdgeActionName = FLeoEdgeAction::StaticStruct()->GetFName();
	const FName EdgeName = FLeoScenarioEdge::StaticStruct()->GetFName();
	Module.RegisterCustomPropertyTypeLayout(EdgeActionName,
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLeoEdgeActionCustomization::MakeInstance));
	Module.RegisterCustomPropertyTypeLayout(EdgeName,
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLeoScenarioEdgeCustomization::MakeInstance));
	UE_LOG(LogTemp, Display, TEXT("[Leo] 边属性定制已注册: %s / %s"), *EdgeActionName.ToString(), *EdgeName.ToString());
}

void UnregisterLeoGraphCustomizations()
{
	if (FPropertyEditorModule* Module = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		Module->UnregisterCustomPropertyTypeLayout(FLeoEdgeAction::StaticStruct()->GetFName());
		Module->UnregisterCustomPropertyTypeLayout(FLeoScenarioEdge::StaticStruct()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE
