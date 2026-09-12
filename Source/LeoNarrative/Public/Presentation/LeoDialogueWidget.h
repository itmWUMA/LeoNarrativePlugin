// 对话 UI：C++ 构建 UMG 树（无需设计器资产），CreateWidget 即可用。
// 订阅子系统事件渲染：对话盒 / 选项列表 / 舞台占位层；auto/skip 在这里驱动 Advance。
#pragma once

#include "Blueprint/UserWidget.h"
#include "VM/LeoEvents.h"
#include "LeoDialogueWidget.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class LEONARRATIVE_API ULeoDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	// 纯 C++ 建树必须在此返回真实根:基类在 RootWidget 为空时包一个隐形 SSpacer,
	// NativeConstruct 建树晚于 Slate 包装,屏幕将永远空白
	virtual TSharedRef<SWidget> RebuildWidget() override;

	void HandleLeoEvent(const FLeoEvent& Ev);

	UFUNCTION()
	void OnAdvanceClicked();

	// 选项 ≤8（spec §5.2），动态委托无法携带索引——按槽位绑定
	UFUNCTION() void OnChoice0() { ChooseOption(0); }
	UFUNCTION() void OnChoice1() { ChooseOption(1); }
	UFUNCTION() void OnChoice2() { ChooseOption(2); }
	UFUNCTION() void OnChoice3() { ChooseOption(3); }
	UFUNCTION() void OnChoice4() { ChooseOption(4); }
	UFUNCTION() void OnChoice5() { ChooseOption(5); }
	UFUNCTION() void OnChoice6() { ChooseOption(6); }
	UFUNCTION() void OnChoice7() { ChooseOption(7); }
	void ChooseOption(int32 Index);

protected:
	void BuildUmgTree();
	void RebuildChoices(const FLeoEvent& Ev);
	void RebuildCharRow();

	// 自动播放/快进的节奏
	float AutoDelay = 2.5f;
	float SkipDelay = 0.05f;

private:
	// UMG 树节点引用（UPROPERTY 防 GC）
	UPROPERTY()
	TObjectPtr<UImage> BgImage;
	// 立绘层：多角色按 at= 各自对齐、立于画面底部
	UPROPERTY()
	TObjectPtr<UOverlay> CharRow;
	UPROPERTY()
	TObjectPtr<UVerticalBox> ChoiceBox;
	UPROPERTY()
	TObjectPtr<USizeBox> ChoiceBoxSizer;
	UPROPERTY()
	TObjectPtr<UTextBlock> SpeakerBlock;
	UPROPERTY()
	TObjectPtr<UTextBlock> BodyBlock;
	UPROPERTY()
	TObjectPtr<UTextBlock> FooterBlock;
	UPROPERTY()
	TObjectPtr<UButton> ClickButton;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> ChoiceButtons;

	float AutoTimer = 0.f;
	float SkipTimer = 0.f;
	FDelegateHandle EventHandle;
	bool bBuiltTree = false;
};
