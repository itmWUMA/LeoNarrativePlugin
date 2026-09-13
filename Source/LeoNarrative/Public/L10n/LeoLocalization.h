// 剧本台词本地化：CSV 译文表（纯文本事实源）+ 运行时查表回落。
// .leo 是源语言唯一事实源；译文按 <TextId,译文> 存 Content/L10n/<culture>/<章节>.csv，
// 打包经 DirectoriesToAlwaysStageAsUFS 原样进 pak，运行时 IFileManager 读取（与 .leo 同通道）。
// 查不到译文 = 显示剧本原文——未翻译条目与源语言包的天然回落。
// CSV 列契约：ID,Speaker,Source,Translation,Status（Speaker/Status 为提取器工作列，运行时只读 ID+Translation）
#pragma once

#include "CoreMinimal.h"

// 自动文本 ID 的唯一生成处（VM 运行期与编辑器提取器/freeze 共用，防止两边漂移）
namespace LeoL10n
{
	LEONARRATIVE_API FString MakeTextId(const FString& Chapter, const FString& Label, int32 Seq);
	LEONARRATIVE_API FString MakeOptionId(const FString& Chapter, const FString& Label, int32 ChoiceSeq, int32 OptIndex);
	LEONARRATIVE_API FString BaseCulture(const FString& Culture); // "en-US" → "en"
}

// RFC4180 子集：引号包裹、内嵌逗号/引号（"" 转义）/换行
namespace LeoCsv
{
	LEONARRATIVE_API bool Parse(const FString& Content, TArray<TArray<FString>>& OutRows, FString& OutError);
	LEONARRATIVE_API FString WriteRow(const TArray<FString>& Cells);
}

class LEONARRATIVE_API FLeoL10nTable
{
public:
	// 依次尝试 Content/L10n/<完整文化> 与 Content/L10n/<基础文化>（en-US 先于 en）；
	// 都不存在 → 表为空（全部回落原文），返回 false
	bool LoadCulture(const FString& Culture);

	bool Resolve(const FString& TextId, FString& OutText) const;

	void AddEntry(const FString& TextId, const FString& Text) { Entries.Add(TextId, Text); } // 测试/演示注入
	void Reset();

	const FString& GetLoadedCultureDir() const { return CultureDir; }
	int32 NumEntries() const { return Entries.Num(); }
	int32 NumFiles() const { return FileCount; }

	static FString L10nDir();                                   // <ProjectContent>/L10n
	static void ListAvailableCultures(TArray<FString>& Out);    // 子目录名（供 leo.lang 列举）

private:
	TMap<FString, FString> Entries; // TextId → 译文（空译文不入表）
	FString CultureDir;
	int32 FileCount = 0;
	int32 EntryCount = 0;
};
