#include "LeoRunCommandlet.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoRun, Log, All);

ULeoRunCommandlet::ULeoRunCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 ULeoRunCommandlet::Main(const FString& Params)
{
	// -exec="leo.autotest chapter01"（可含分号串联）；-seconds=30 泵帧上限
	FString ExecStr;
	FParse::Value(*Params, TEXT("exec="), ExecStr);
	float Seconds = 30.f;
	FParse::Value(*Params, TEXT("seconds="), Seconds);
	if (ExecStr.IsEmpty())
	{
		UE_LOG(LogLeoRun, Error, TEXT("用法: -run=LeoRun -exec=\"leo.autotest chapter01\" -seconds=30"));
		return 1;
	}

	// 命令行环境手工搭一个游戏世界：GameInstance::InitializeStandalone
	// 会建 World + 初始化子系统集合（叙事子系统在此编译剧本并注册 ticker）。
	// outer 必须是 GEngine——GetEngine() 直接 Cast 外链
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone(TEXT("LeoRunWorld"));
	UWorld* World = GameInstance->GetWorld();
	if (!World)
	{
		UE_LOG(LogLeoRun, Error, TEXT("无 World"));
		return 1;
	}
	UE_LOG(LogLeoRun, Display, TEXT("游戏实例就绪，执行: %s"), *ExecStr);

	GEngine->Exec(World, *ExecStr);

	// 泵帧：只驱动核心 ticker（VM 驱动）。世界 tick 在命令行环境可能长时间阻塞，
	// 且序列播放可由测试侧手动泵（leo.demo seq 内部已处理）。
	const double Start = FPlatformTime::Seconds();
	const double Deadline = Start + FMath::Max(1.f, Seconds);
	while (FPlatformTime::Seconds() < Deadline)
	{
		FTSTicker::GetCoreTicker().Tick(1.f / 30.f);
		FPlatformProcess::Sleep(0.f); // 让出时间片
	}
	UE_LOG(LogLeoRun, Display, TEXT("泵帧结束（%.1fs），会话状态见上方 Leo 日志"), FPlatformTime::Seconds() - Start);
	GameInstance->Shutdown();
	return 0;
}
