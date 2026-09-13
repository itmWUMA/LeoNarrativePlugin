#include "L10n/LeoLocalization.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoL10n, Log, All);

namespace LeoL10n
{

FString MakeTextId(const FString& Chapter, const FString& Label, int32 Seq)
{
	return FString::Printf(TEXT("%s/%s/%d"), *Chapter, *Label, Seq);
}

FString MakeOptionId(const FString& Chapter, const FString& Label, int32 ChoiceSeq, int32 OptIndex)
{
	return FString::Printf(TEXT("%s/%s/c%d/%d"), *Chapter, *Label, ChoiceSeq, OptIndex);
}

FString BaseCulture(const FString& Culture)
{
	const int32 Dash = Culture.Find(TEXT("-"));
	return Dash == INDEX_NONE ? Culture : Culture.Left(Dash);
}

} // namespace LeoL10n

namespace LeoCsv
{

bool Parse(const FString& Content, TArray<TArray<FString>>& OutRows, FString& OutError)
{
	OutRows.Reset();
	TArray<FString> Row;
	FString Field;
	bool bInQuotes = false;
	const TCHAR* S = *Content;
	for (; *S; ++S)
	{
		const TCHAR C = *S;
		if (bInQuotes)
		{
			if (C == TEXT('"'))
			{
				if (S[1] == TEXT('"')) { Field.AppendChar(TEXT('"')); ++S; }
				else { bInQuotes = false; }
			}
			else
			{
				Field.AppendChar(C);
			}
			continue;
		}
		if (C == TEXT('"') && Field.IsEmpty()) { bInQuotes = true; }
		else if (C == TEXT(','))               { Row.Add(MoveTemp(Field)); }
		else if (C == TEXT('\r'))              { /* 与 \n 成对，忽略 */ }
		else if (C == TEXT('\n'))
		{
			Row.Add(MoveTemp(Field));
			OutRows.Add(MoveTemp(Row));
		}
		else                                   { Field.AppendChar(C); }
	}
	if (bInQuotes)
	{
		OutError = TEXT("引号字段未闭合");
		return false;
	}
	if (!Field.IsEmpty() || !Row.IsEmpty()) // 末行无换行符的余量
	{
		Row.Add(MoveTemp(Field));
		OutRows.Add(MoveTemp(Row));
	}
	return true;
}

FString WriteRow(const TArray<FString>& Cells)
{
	// 统一加引号：写读两侧同规则，规避"何时需要引号"的边界判断
	FString Out;
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (i > 0) { Out.AppendChar(TEXT(',')); }
		Out.AppendChar(TEXT('"'));
		for (const TCHAR C : Cells[i])
		{
			Out.AppendChar(C);
			if (C == TEXT('"')) { Out.AppendChar(TEXT('"')); }
		}
		Out.AppendChar(TEXT('"'));
	}
	return Out;
}

} // namespace LeoCsv

namespace
{
	// 表头列名 → 列下标（大小写不敏感）；返回是否具备 ID 与 Translation 两列
	bool MapHeader(const TArray<FString>& Header, int32& OutId, int32& OutSpeaker, int32& OutSource, int32& OutTrans, int32& OutStatus)
	{
		OutId = OutSpeaker = OutSource = OutTrans = OutStatus = INDEX_NONE;
		for (int32 i = 0; i < Header.Num(); ++i)
		{
			const FString H = Header[i].TrimStartAndEnd().ToLower();
			if (H == TEXT("id"))              { OutId = i; }
			else if (H == TEXT("speaker"))    { OutSpeaker = i; }
			else if (H == TEXT("source"))     { OutSource = i; }
			else if (H == TEXT("translation")){ OutTrans = i; }
			else if (H == TEXT("status"))     { OutStatus = i; }
		}
		return OutId != INDEX_NONE && OutTrans != INDEX_NONE;
	}
} // namespace

FString FLeoL10nTable::L10nDir()
{
	return FPaths::ProjectContentDir() / TEXT("L10n");
}

void FLeoL10nTable::ListAvailableCultures(TArray<FString>& Out)
{
	Out.Reset();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(L10nDir() / TEXT("*")), false, true);
	for (const FString& D : Dirs)
	{
		const FString Full = L10nDir() / D;
		if (IFileManager::Get().DirectoryExists(*Full))
		{
			Out.Add(FPaths::GetBaseFilename(Full)); // FindFiles 返回的目录名可能带相对成分
		}
	}
}

bool FLeoL10nTable::LoadCulture(const FString& Culture)
{
	Reset();
	TArray<FString> Candidates = { Culture, LeoL10n::BaseCulture(Culture) };
	if (Candidates[0] == Candidates[1]) { Candidates.SetNum(1); }

	for (const FString& Cand : Candidates)
	{
		const FString Dir = L10nDir() / Cand;
		if (!IFileManager::Get().DirectoryExists(*Dir)) { continue; }

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.csv")), true, false);
		Files.Sort();
		for (const FString& F : Files)
		{
			const FString Path = Dir / F;
			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *Path))
			{
				UE_LOG(LogLeoL10n, Warning, TEXT("译文文件读取失败: %s"), *Path);
				continue;
			}
			TArray<TArray<FString>> Rows;
			FString Err;
			if (!LeoCsv::Parse(Content, Rows, Err) || Rows.Num() < 2)
			{
				UE_LOG(LogLeoL10n, Warning, TEXT("译文文件解析失败: %s (%s)"), *Path, *Err);
				continue;
			}
			int32 ColId, ColSpeaker, ColSource, ColTrans, ColStatus;
			if (!MapHeader(Rows[0], ColId, ColSpeaker, ColSource, ColTrans, ColStatus))
			{
				UE_LOG(LogLeoL10n, Warning, TEXT("译文文件缺少 ID/Translation 列: %s"), *Path);
				continue;
			}
			++FileCount;
			for (int32 r = 1; r < Rows.Num(); ++r)
			{
				const TArray<FString>& Row = Rows[r];
				const FString& Id = ColId < Row.Num() ? Row[ColId] : FString();
				const FString& Tr = ColTrans < Row.Num() ? Row[ColTrans] : FString();
				if (Id.IsEmpty() || Tr.IsEmpty()) { continue; }
				if (!Entries.Contains(Id)) { ++EntryCount; }
				Entries.Add(Id, Tr);
			}
		}
		CultureDir = Dir;
		UE_LOG(LogLeoL10n, Display, TEXT("本地化表: %s（%d 文件 / %d 条译文）"), *Dir, FileCount, EntryCount);
		return true;
	}
	UE_LOG(LogLeoL10n, Display, TEXT("本地化表: 无 %s 译文目录（%s），全部回落原文"), *Culture, *L10nDir());
	return false;
}

bool FLeoL10nTable::Resolve(const FString& TextId, FString& OutText) const
{
	if (const FString* Found = Entries.Find(TextId))
	{
		OutText = *Found;
		return true;
	}
	return false;
}

void FLeoL10nTable::Reset()
{
	Entries.Reset();
	CultureDir.Reset();
	FileCount = 0;
	EntryCount = 0;
}
