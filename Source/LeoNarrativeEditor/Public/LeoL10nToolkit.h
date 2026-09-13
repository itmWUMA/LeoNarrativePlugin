// 本地化 CSV 工具库：提取/合并/freeze 的核心实现。
// 消费方：LeoL10n 命令行（headless）与校验中心面板（编辑器按钮/校验）——同一套语义，不分叉。
#pragma once

#include "CoreMinimal.h"
#include "ScriptRuntime/LeoScriptBridge.h"

namespace LeoL10nToolkit
{
	// 一条可翻译条目（.leo 编译产物中枚举；ID 拼法与 VM 同源）
	struct FTextEntry
	{
		FString Id, Speaker, Source;
		bool bExplicit = false; // id= 显式指定；自动 ID 与显式撞号时译文会错位（见 CollectEntries 返回值）
	};

	// 译文 CSV 行模型（列契约：ID,Speaker,Source,Translation,Status）
	struct FCsvRow
	{
		FString Id, Speaker, Source, Translation, Status;
	};

	struct FMergeStats
	{
		int32 New = 0, Stale = 0, Gone = 0;
	};

	// 编译产物 → 条目列表。返回撞号数（0 = 干净；>0 = 建议 freeze 固化新行）
	int32 CollectEntries(const leo::FLeoProgram& P, const FString& Chapter, TArray<FTextEntry>& Out);

	// 译文 CSV 读写（读失败时 OutError 带原因；文件不存在也走 OutError）
	bool ReadCsvRows(const FString& Path, TArray<FCsvRow>& Out, FString& OutError);
	FString WriteCsvRows(const TArray<FCsvRow>& Rows);

	// 合并语义（提取器核心——译文永远不丢）：
	// 新条目更新 Source/Speaker；源变 → 有译文标 STALE、无译文标 NEW；源未变保留 Status；
	// 脚本里已消失的旧条目移末尾标 GONE（人工确认后删行）
	void MergeRows(const TArray<FTextEntry>& Entries, TArray<FCsvRow>& InOut, FMergeStats& Stats);

	// extract：全部 Content/Scripts/*.leo → Content/L10n/<culture>/<章节>.csv（增量合并）
	// 返回失败数（读/编译/写失败）；过程日志走 LogLeoL10n
	int32 ExtractForCultures(const TArray<FString>& Cultures);

	// freeze：自动 ID 以 " id=xxx" 显式写回 .leo（新行自动避让既有 ID 加 -N 后缀）
	// OnlyChapter 为空 = 全部章节。返回失败数
	int32 FreezeChapters(const FString& OnlyChapter);

	// Content/L10n 下已有文化的目录名（升序）
	void DiscoverCultures(TArray<FString>& Out);

	// 语言选择项（校验中心下拉数据源）：已有译文的目录置顶（标 ●），
	// 其余为引擎本地化语言全集（FTextLocalizationManager），显示名用母语名
	struct FCultureChoice
	{
		FString Code;   // 文化码/目录名（extract 用这个）
		FString Label;  // 显示名，如 "日本語 (ja)"
		bool bHasCsv = false;
	};
	void BuildCultureChoices(TArray<FCultureChoice>& Out);
}
