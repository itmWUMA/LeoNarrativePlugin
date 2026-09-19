#include "Presentation/LeoDialogueWidget.h"
#include "Subsystem/LeoNarrativeSubsystem.h"
#include "Settings/LeoNarrativeSettings.h"
#include "Data/LeoAssetManifest.h"
#include "Stage/LeoStage.h"
#include "VM/LeoVM.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"

TSharedRef<SWidget> ULeoDialogueWidget::RebuildWidget()
{
	Super::RebuildWidget();
	if (!bBuiltTree)
	{
		BuildUmgTree();
		bBuiltTree = true;
	}
	check(WidgetTree->RootWidget);
	return WidgetTree->RootWidget->TakeWidget();
}

void ULeoDialogueWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (IsDesignTime()) { return; }
	if (!bBuiltTree)
	{
		BuildUmgTree();
		bBuiltTree = true;
	}
	// auto/skip 节奏经 Project Settings 配置（换项目零代码改动）
	AutoDelay = FMath::Max(ULeoNarrativeSettings::Get()->AutoAdvanceDelay, 0.01f);
	SkipDelay = FMath::Max(ULeoNarrativeSettings::Get()->SkipAdvanceDelay, 0.f);
	if (ULeoNarrativeSubsystem* Sub = GetGameInstance()->GetSubsystem<ULeoNarrativeSubsystem>())
	{
		EventHandle = Sub->OnLeoEvent.AddUObject(this, &ULeoDialogueWidget::HandleLeoEvent);
	}
}

void ULeoDialogueWidget::NativeDestruct()
{
	if (ULeoNarrativeSubsystem* Sub = GetGameInstance()->GetSubsystem<ULeoNarrativeSubsystem>())
	{
		Sub->OnLeoEvent.Remove(EventHandle);
	}
	Super::NativeDestruct();
}

void ULeoDialogueWidget::BuildUmgTree()
{
	UWidgetTree* Tree = WidgetTree;
	check(Tree);

	auto MkText = [Tree](const TCHAR* Name) -> UTextBlock*
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		return T;
	};

	// 根：覆盖层
	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	Tree->RootWidget = Root;

	auto AddOverlayChild = [Root](UWidget* Child, EVerticalAlignment VA, EHorizontalAlignment HA, const FMargin& Pad) -> UOverlaySlot*
	{
		UOverlaySlot* Slot = Root->AddChildToOverlay(Child);
		Slot->SetVerticalAlignment(VA);
		Slot->SetHorizontalAlignment(HA);
		Slot->SetPadding(Pad);
		return Slot;
	};

	// [0] 背景（初始深色底;WhiteBrush 加 tint 保证可绘制,空画刷会画白块）
	BgImage = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Bg"));
	FSlateBrush InitialBg(*FCoreStyle::Get().GetBrush("WhiteBrush"));
	InitialBg.TintColor = FSlateColor(FLinearColor(0.06f, 0.07f, 0.12f, 1.f));
	BgImage->SetBrush(InitialBg);
	AddOverlayChild(BgImage, VAlign_Fill, HAlign_Fill, FMargin());

	// [1] 立绘层（全幅覆盖，内部各立绘按 at 对齐、立于底部）
	CharRow = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CharRow"));
	AddOverlayChild(CharRow, VAlign_Fill, HAlign_Fill, FMargin());

	// [2] 选项列表（居中，定宽）
	ChoiceBoxSizer = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ChoiceSizer"));
	ChoiceBoxSizer->SetWidthOverride(560.f);
	ChoiceBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChoiceBox"));
	ChoiceBoxSizer->SetContent(ChoiceBox);
	AddOverlayChild(ChoiceBoxSizer, VAlign_Center, HAlign_Center, FMargin());

	// [3] 对话盒（底部）
	UBorder* DialogueBorder = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogueBox"));
	DialogueBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.72f));
	DialogueBorder->SetPadding(FMargin(20, 14));
	UVerticalBox* DialogueStack = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogueStack"));
	DialogueBorder->SetContent(DialogueStack);

	SpeakerBlock = MkText(TEXT("Speaker"));
	SpeakerBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Roboto"), 15));
	SpeakerBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.85f, 0.35f, 1.f)));
	UVerticalBoxSlot* SpeakerSlot = DialogueStack->AddChildToVerticalBox(SpeakerBlock);
	SpeakerSlot->SetSize(ESlateSizeRule::Automatic);

	BodyBlock = MkText(TEXT("Body"));
	BodyBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Roboto"), 18));
	BodyBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	BodyBlock->SetAutoWrapText(true);
	UVerticalBoxSlot* BodySlot = DialogueStack->AddChildToVerticalBox(BodyBlock);
	BodySlot->SetSize(ESlateSizeRule::Automatic);
	BodySlot->SetPadding(FMargin(0, 4, 0, 0));

	AddOverlayChild(DialogueBorder, VAlign_Bottom, HAlign_Fill, FMargin(24, 0, 24, 24));

	// [4] 状态脚注（右下）
	FooterBlock = MkText(TEXT("Footer"));
	FooterBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Roboto"), 11));
	FooterBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 0.8f)));
	AddOverlayChild(FooterBlock, VAlign_Bottom, HAlign_Right, FMargin(8));

	// [5] 全屏推进按钮（NoDrawing 画刷组;空 FSlateBrush 会以白色兜底绘制成全屏白块）
	ClickButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ClickCatcher"));
	FButtonStyle BtnStyle;
	FSlateBrush NoDrawBrush;
	NoDrawBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	BtnStyle.Normal = NoDrawBrush;
	BtnStyle.Hovered = NoDrawBrush;
	BtnStyle.Pressed = NoDrawBrush;
	BtnStyle.Disabled = NoDrawBrush;
	ClickButton->SetStyle(BtnStyle);
	ClickButton->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnAdvanceClicked);
	AddOverlayChild(ClickButton, VAlign_Fill, HAlign_Fill, FMargin());

	RebuildCharRow();
}

void ULeoDialogueWidget::HandleLeoEvent(const FLeoEvent& Ev)
{
	UGameInstance* GI = GetGameInstance();
	ULeoNarrativeSubsystem* Sub = GI ? GI->GetSubsystem<ULeoNarrativeSubsystem>() : nullptr;
	if (!Sub) { return; }

	switch (Ev.Kind)
	{
	case ELeoEventKind::Text:
		SpeakerBlock->SetText(FText::FromString(Ev.Speaker));
		BodyBlock->SetText(FText::FromString(Ev.Text));
		FooterBlock->SetText(FText::FromString(Ev.TextId));
		AutoTimer = AutoDelay;
		SkipTimer = SkipDelay;
		break;

	case ELeoEventKind::Bg:
	{
		// 清单路径需要 TryLoad 触发加载(ResolveObject 只查内存);失败一律回落深色底
		UTexture2D* Tex = nullptr;
		if (ULeoStage* Stage = Sub->GetStage())
		{
			Tex = Cast<UTexture2D>(Stage->GetCurrentBgPath().TryLoad());
		}
		if (Tex)
		{
			BgImage->SetBrushFromTexture(Tex);
		}
		else
		{
			FSlateBrush DarkBrush(*FCoreStyle::Get().GetBrush("WhiteBrush"));
			DarkBrush.TintColor = FSlateColor(FLinearColor(0.06f, 0.07f, 0.12f, 1.f));
			BgImage->SetBrush(DarkBrush);
		}
		break;
	}

	case ELeoEventKind::Char:
		RebuildCharRow();
		break;

	case ELeoEventKind::ChoiceShown:
		RebuildChoices(Ev);
		ClickButton->SetVisibility(ESlateVisibility::HitTestInvisible);
		break;

	case ELeoEventKind::ChoiceMade:
		ChoiceBox->ClearChildren();
		ChoiceButtons.Reset();
		ClickButton->SetVisibility(ESlateVisibility::Visible);
		break;

	case ELeoEventKind::RuntimeError:
		FooterBlock->SetText(FText::FromString(FString::Printf(TEXT("错误 %s(%d): %s"), *Ev.DiagCode, Ev.Line, *Ev.DiagMsg)));
		FooterBlock->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.3f, 0.3f, 1.f)));
		break;

	default:
		break;
	}
}

void ULeoDialogueWidget::OnAdvanceClicked()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (ULeoNarrativeSubsystem* Sub = GI->GetSubsystem<ULeoNarrativeSubsystem>())
		{
			Sub->Advance();
		}
	}
}

void ULeoDialogueWidget::ChooseOption(int32 Index)
{
	UGameInstance* GI = GetGameInstance();
	if (ULeoNarrativeSubsystem* Sub = GI ? GI->GetSubsystem<ULeoNarrativeSubsystem>() : nullptr)
	{
		Sub->Choose(Index);
	}
}

void ULeoDialogueWidget::RebuildChoices(const FLeoEvent& Ev)
{
	ChoiceBox->ClearChildren();
	ChoiceButtons.Reset();
	UWidgetTree* Tree = WidgetTree;

	// 选项 ≤8（spec §5.2）：AddDynamic 需要编译期函数指针，按索引 switch 绑定
	for (int32 i = 0; i < Ev.Options.Num() && i < 8; ++i)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		switch (i)
		{
		case 0: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice0); break;
		case 1: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice1); break;
		case 2: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice2); break;
		case 3: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice3); break;
		case 4: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice4); break;
		case 5: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice5); break;
		case 6: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice6); break;
		case 7: Btn->OnClicked.AddDynamic(this, &ULeoDialogueWidget::OnChoice7); break;
		}
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Ev.Options[i].Text));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Roboto"), 16));
		Btn->SetContent(Label);
		UVerticalBoxSlot* BoxSlot = ChoiceBox->AddChildToVerticalBox(Btn);
		BoxSlot->SetSize(ESlateSizeRule::Automatic);
		BoxSlot->SetPadding(FMargin(4));
		ChoiceButtons.Add(Btn);
	}
}

void ULeoDialogueWidget::RebuildCharRow()
{
	if (!CharRow) { return; }
	CharRow->ClearChildren();
	UGameInstance* GI = GetGameInstance();
	ULeoNarrativeSubsystem* Sub = GI ? GI->GetSubsystem<ULeoNarrativeSubsystem>() : nullptr;
	if (!Sub || !Sub->GetStage()) { return; }
	UWidgetTree* Tree = WidgetTree;
	ULeoAssetManifest* Manifest = Sub->GetManifest();

	for (const TPair<FString, FLeoCharState>& KV : Sub->GetStage()->GetChars())
	{
		const FLeoCharState& S = KV.Value;

		// 每个槽位一个包络:立绘图原尺寸,按 at 横向对齐、立于画面底部
		UOverlay* Cell = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		UImage* Portrait = Tree->ConstructWidget<UImage>(UImage::StaticClass());

		bool bLoaded = false;
		if (Manifest)
		{
			FSoftObjectPath Path;
			if (Manifest->TryResolve(S.AssetId, Path))
			{
				if (UTexture2D* Tex = Cast<UTexture2D>(Path.TryLoad()))
				{
					Portrait->SetBrushFromTexture(Tex);
					bLoaded = true;
				}
			}
		}
		if (!bLoaded)
		{
			// 无清单/缺资产回落:WhiteBrush 加 tint 画色块 + 槽位文字占位
			FSlateBrush FallbackBrush(*FCoreStyle::Get().GetBrush("WhiteBrush"));
			FallbackBrush.TintColor = FSlateColor(FLinearColor(0.2f, 0.24f, 0.34f, 0.9f));
			FallbackBrush.ImageSize = FVector2D(220, 130);
			Portrait->SetBrush(FallbackBrush);
			UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Label->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s [%s]"), *KV.Key, *S.AssetId.ToString(), *S.At)));
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			UOverlaySlot* LabelSlot = Cell->AddChildToOverlay(Label);
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		UOverlaySlot* Inner = Cell->AddChildToOverlay(Portrait);
		Inner->SetHorizontalAlignment(HAlign_Center);
		Inner->SetVerticalAlignment(VAlign_Bottom);

		UOverlaySlot* CharSlot = CharRow->AddChildToOverlay(Cell);
		CharSlot->SetVerticalAlignment(VAlign_Bottom);
		CharSlot->SetHorizontalAlignment(
			S.At == TEXT("left") ? HAlign_Left :
			S.At == TEXT("right") ? HAlign_Right : HAlign_Center);
	}
}

void ULeoDialogueWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);
	UGameInstance* GI = GetGameInstance();
	ULeoNarrativeSubsystem* Sub = GI ? GI->GetSubsystem<ULeoNarrativeSubsystem>() : nullptr;
	if (!Sub) { return; }
	ULeoVM* VM = Sub->GetActiveVM();
	if (!VM) { return; }

	// skip > auto；二者都在 choice 处停下（WaitChoice 不推进）
	if (VM->GetState() == ELeoVMState::WaitClick)
	{
		if (Sub->IsSkip())
		{
			SkipTimer -= DeltaSeconds;
			if (SkipTimer <= 0.f)
			{
				Sub->Advance();
				SkipTimer = SkipDelay;
			}
		}
		else if (Sub->IsAuto())
		{
			AutoTimer -= DeltaSeconds;
			if (AutoTimer <= 0.f)
			{
				Sub->Advance();
				AutoTimer = AutoDelay;
			}
		}
	}
}
