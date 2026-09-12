// 非 VN 玩法扩展示例：调查模式（investigate）。
// 展示"零框架改动"接入新玩法段：只用公开扩展点——
//   1) 编译期：FLeoCmdSpec 严格注册（参数错误在编辑期报出，带行号）
//   2) 运行期：处理器广播 Custom 事件（参数袋）→ Suspend 挂起为外部断点
//   3) 恢复：玩法层完成后 Subsystem->ResumeWith(token, 结果) → 结果写黑板 → 脚本 jumpif 分流
// 用法：leo.demo investigate（走"调查成功"分支）；leo.autotest chapter02（默认 payload 0 走失败分支）
#include "Examples/LeoInvestigationDemo.h"

#include "Subsystem/LeoNarrativeSubsystem.h"
#include "VM/LeoVM.h"

#include "Containers/Ticker.h"

namespace LeoExamples
{

static ELeoCustomResult HandleInvestigate(ULeoVM& VM, const leo::FLeoCommand& C)
{
	// 广播 Custom 事件：调查 UI/玩法层（若存在）从这里接手，ExtraParams 带场景与模式
	VM.EmitCustomEvent(C);
	// 挂起为外部断点，等待 ResumeWith("investigation", 发现数量)
	VM.Suspend(TEXT("investigation"));
	return ELeoCustomResult::Suspend;
}

void RegisterInvestigationDemo()
{
	LeoBridge::FLeoCmdSpec Spec;
	Spec.Name = TEXT("investigate");
	Spec.MinArgs = 1; // 场景逻辑名
	Spec.MaxArgs = 1;
	Spec.AllowedParams = { TEXT("mode") }; // normal | strict
	ULeoVM::RegisterCustomCommand(TEXT("investigate"), Spec, &HandleInvestigate);
}

} // namespace LeoExamples

// ---- 演示运行器：chapter02 走调查成功分支（ResumeWith payload = 2）----
static ULeoNarrativeSubsystem* GetLeoSubsystemForDemo()
{
	if (!GEngine) { return nullptr; }
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.OwningGameInstance)
		{
			return Ctx.OwningGameInstance->GetSubsystem<ULeoNarrativeSubsystem>();
		}
	}
	return nullptr;
}

static FAutoConsoleCommand GLeoDemoInvestigate(
	TEXT("leo.demo investigate"), TEXT("运行 chapter02 调查示例（自动恢复断点，payload=2 走成功分支）"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		ULeoNarrativeSubsystem* S = GetLeoSubsystemForDemo();
		if (!S || !S->StartChapter(TEXT("chapter02"))) { return; }
		UE_LOG(LogTemp, Display, TEXT("[demo] chapter02 开始：0.5s 后自动完成调查（发现 2 条线索）"));

		static FTSTicker::FDelegateHandle DemoHandle;
		FTSTicker::GetCoreTicker().RemoveTicker(DemoHandle);
		DemoHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float Dt)
		{
			ULeoVM* VM = S->GetActiveVM();
			if (!VM) { return false; }
			switch (VM->GetState())
			{
			case ELeoVMState::WaitClick:
				S->Advance();
				break;
			case ELeoVMState::WaitExternal:
			{
				// 真实项目里这里是调查 UI 完成后的回调；demo 用 0.5s 定时器模拟
				static float Waited = 0.f;
				Waited += Dt;
				if (Waited >= 0.5f)
				{
					Waited = 0.f;
					S->ResumeWith(TEXT("investigation"), leo::FLeoValue::MakeInt(2));
				}
				break;
			}
			case ELeoVMState::Finished:
				UE_LOG(LogTemp, Display, TEXT("[demo] chapter02 完结（调查成功分支）"));
				return false;
			default:
				break;
			}
			return true;
		}));
	}));
