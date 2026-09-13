#include "LeoL10nCommandlet.h"

#include "LeoL10nToolkit.h"
#include "L10n/LeoLocalization.h"
#include "ScriptRuntime/LeoScriptBridge.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoL10nSelftest, Log, All);

namespace
{
	// ---- selftest：CSV 往返 / ID 拼法 / 合并语义 / 编号一致性 / 撞号检测 ----
	int32 SelfTest()
	{
		int32 Fail = 0;
		auto Check = [&Fail](bool b, const TCHAR* What)
		{
			UE_LOG(LogLeoL10nSelftest, Display, TEXT("[%s] %s"), b ? TEXT(" ok ") : TEXT("FAIL"), What);
			if (!b) { ++Fail; }
		};

		// CSV 往返：逗号/引号/换行/CJK
		{
			TArray<FString> Cells = { TEXT("id/with/slash"), TEXT("李雷, 二号"), TEXT("说\"你好\""), TEXT("带\n换行") };
			TArray<TArray<FString>> Rows;
			FString Err;
			Check(LeoCsv::Parse(LeoCsv::WriteRow(Cells) + TEXT("\n"), Rows, Err) && Rows.Num() == 1 && Rows[0] == Cells,
				TEXT("CSV 往返（逗号/引号/换行/CJK）"));
		}
		// ID 拼法
		Check(LeoL10n::MakeTextId(TEXT("ch"), TEXT("lab"), 3) == TEXT("ch/lab/3"), TEXT("MakeTextId 拼法"));
		Check(LeoL10n::MakeOptionId(TEXT("ch"), TEXT("lab"), 1, 2) == TEXT("ch/lab/c1/2"), TEXT("MakeOptionId 拼法"));
		Check(LeoL10n::BaseCulture(TEXT("en-US")) == TEXT("en"), TEXT("BaseCulture"));
		// 合并语义：NEW / STALE / 保留译文 / GONE
		{
			TArray<LeoL10nToolkit::FTextEntry> Entries = {
				{ TEXT("a/1/0"), TEXT(""), TEXT("新句"), false },
				{ TEXT("a/1/1"), TEXT(""), TEXT("改过的句"), false },
				{ TEXT("a/1/2"), TEXT(""), TEXT("没变的句"), false },
			};
			TArray<LeoL10nToolkit::FCsvRow> Rows = {
				{ TEXT("a/1/1"), TEXT(""), TEXT("旧句"), TEXT("Old translation"), TEXT("OK") },
				{ TEXT("a/1/2"), TEXT(""), TEXT("没变的句"), TEXT("Same translation"), TEXT("OK") },
				{ TEXT("a/1/9"), TEXT(""), TEXT("删掉的句"), TEXT("Bye"), TEXT("OK") },
			};
			LeoL10nToolkit::FMergeStats Stats;
			LeoL10nToolkit::MergeRows(Entries, Rows, Stats);
			Check(Rows.Num() == 4 && Stats.New == 1 && Stats.Stale == 1 && Stats.Gone == 1, TEXT("合并统计 NEW/STALE/GONE"));
			Check(Rows[0].Status == TEXT("NEW") && Rows[0].Translation.IsEmpty(), TEXT("新条目标 NEW"));
			Check(Rows[1].Status == TEXT("STALE") && Rows[1].Translation == TEXT("Old translation")
				&& Rows[1].Source == TEXT("改过的句"), TEXT("源变标 STALE 且译文保留"));
			Check(Rows[2].Status == TEXT("OK") && Rows[2].Translation == TEXT("Same translation"), TEXT("源未变保留 OK 与译文"));
			Check(Rows[3].Status == TEXT("GONE") && Rows[3].Translation == TEXT("Bye"), TEXT("消失条目移末尾标 GONE"));
		}
		// freeze→extract 编号一致性（最小章节）
		{
			const FString Src = TEXT("text - | 一\ntext - | 二 id=x/y\nchoice\n    甲 -> lab\n    乙 -> lab\nlabel lab\ntext - | 三\nend\n");
			const leo::FLeoProgram P = LeoBridge::CompileChapter(Src, TEXT("c"));
			TArray<LeoL10nToolkit::FTextEntry> Entries;
			const int32 Collisions = LeoL10nToolkit::CollectEntries(P, TEXT("c"), Entries);
			const bool bIds = Entries.Num() == 5
				&& Entries[0].Id == TEXT("c/_root/0") && Entries[1].Id == TEXT("x/y")
				&& Entries[2].Id == TEXT("c/_root/c0/0") && Entries[3].Id == TEXT("c/_root/c0/1")
				&& Entries[4].Id == TEXT("c/lab/0");
			Check(P.Ok && bIds && Collisions == 0, TEXT("提取器编号与 VM 规则一致（含显式/选项/lab 重置）"));
		}
		{
			// freeze 后插行未再 freeze：显式 ID 与新行自动 ID 相同 → 提取层检出（提示补跑 freeze）
			const FString Src = TEXT("label lab\ntext - | 旧句 id=c/lab/1\ntext - | 新插的行\nend\n");
			const leo::FLeoProgram P = LeoBridge::CompileChapter(Src, TEXT("c"));
			TArray<LeoL10nToolkit::FTextEntry> Entries;
			const int32 Collisions = LeoL10nToolkit::CollectEntries(P, TEXT("c"), Entries);
			Check(P.Ok && Collisions == 1, TEXT("撞号检测（显式 ID 与自动 ID 同号）"));
		}
		// ReadCsvRows 的错误路径
		{
			TArray<LeoL10nToolkit::FCsvRow> Rows;
			FString Err;
			LeoL10nToolkit::ReadCsvRows(FPaths::ProjectContentDir() / TEXT("__no_such__.csv"), Rows, Err);
			Check(!Err.IsEmpty(), TEXT("ReadCsvRows 失败路径带原因"));
		}
		// 语言选项表：引擎本地化语言全集（≥5 门）∪ 已有目录，显示名非空
		{
			TArray<LeoL10nToolkit::FCultureChoice> Choices;
			LeoL10nToolkit::BuildCultureChoices(Choices);
			bool bHasEn = false, bHasJa = false, bLabels = true;
			for (const LeoL10nToolkit::FCultureChoice& C : Choices)
			{
				if (C.Code == TEXT("en")) { bHasEn = true; }
				if (C.Code == TEXT("ja")) { bHasJa = true; }
				if (C.Label.IsEmpty()) { bLabels = false; }
			}
			Check(Choices.Num() >= 5 && bHasEn && bHasJa && bLabels, TEXT("语言选项表（引擎语言全集，含 en/ja，显示名非空）"));
		}
		UE_LOG(LogLeoL10nSelftest, Display, TEXT("=== LeoL10n selftest: %s ==="), Fail == 0 ? TEXT("通过") : TEXT("失败"));
		return Fail;
	}
} // namespace

int32 ULeoL10nCommandlet::Main(const FString& Params)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;

	if (FParse::Param(*Params, TEXT("selftest")))
	{
		return SelfTest() > 0 ? 1 : 0;
	}

	FString Action, Culture;
	FParse::Value(*Params, TEXT("action="), Action);
	FParse::Value(*Params, TEXT("culture="), Culture);
	if (Action == TEXT("extract"))
	{
		TArray<FString> Cultures;
		Culture.ParseIntoArray(Cultures, TEXT(","), true);
		if (Cultures.Num() == 0)
		{
			UE_LOG(LogLeoL10nSelftest, Error, TEXT("extract 需要 -culture=en[,ja,...]"));
			return 1;
		}
		const int32 Failures = LeoL10nToolkit::ExtractForCultures(Cultures);
		UE_LOG(LogLeoL10nSelftest, Display, TEXT("=== extract 完成（%d 失败）。译者工作流：填 Translation 列，改完把 Status 改成 OK ==="), Failures);
		return Failures > 0 ? 1 : 0;
	}
	if (Action == TEXT("freeze"))
	{
		FString Chapter;
		FParse::Value(*Params, TEXT("chapter="), Chapter);
		return LeoL10nToolkit::FreezeChapters(Chapter) > 0 ? 1 : 0;
	}
	UE_LOG(LogLeoL10nSelftest, Display, TEXT("用法: -run=LeoL10n -action=extract -culture=en[,ja] | -action=freeze [-chapter=名] | -selftest"));
	return 0;
}
