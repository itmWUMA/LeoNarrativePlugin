#include "LeoL10nToolkit.h"
#include "Settings/LeoNarrativeSettings.h"

#include "L10n/LeoLocalization.h"
#include "HAL/FileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoL10nCmd, Log, All);

namespace
{
	// UTF-8 读取（与 Registry 同规则：可带可不带 BOM）
	bool ReadUtf8File(const FString& Path, FString& OutContent)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return false;
		}
		int32 Start = 0;
		if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
		{
			Start = 3; // UTF-8 BOM
		}
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()) + Start, Bytes.Num() - Start);
		OutContent = FString(Conv.Length(), Conv.Get());
		return true;
	}

	FString ScriptsDir() { return ULeoNarrativeSettings::Get()->GetScriptsDirPath(); }

	void ListChapterFiles(TArray<FString>& OutFiles)
	{
		OutFiles.Reset();
		IFileManager::Get().FindFiles(OutFiles, *(ScriptsDir() / TEXT("*.leo")), true, false);
		OutFiles.Sort();
	}

	bool MapCsvHeader(const TArray<TArray<FString>>& Rows, int32& ColId, int32& ColSpk, int32& ColSrc, int32& ColTr, int32& ColSt)
	{
		ColId = ColSpk = ColSrc = ColTr = ColSt = INDEX_NONE;
		if (Rows.Num() < 1) { return false; }
		for (int32 i = 0; i < Rows[0].Num(); ++i)
		{
			const FString H = Rows[0][i].TrimStartAndEnd().ToLower();
			if (H == TEXT("id"))               { ColId = i; }
			else if (H == TEXT("speaker"))     { ColSpk = i; }
			else if (H == TEXT("source"))      { ColSrc = i; }
			else if (H == TEXT("translation")) { ColTr = i; }
			else if (H == TEXT("status"))      { ColSt = i; }
		}
		return ColId != INDEX_NONE && ColTr != INDEX_NONE;
	}
} // namespace

namespace LeoL10nToolkit
{

int32 CollectEntries(const leo::FLeoProgram& P, const FString& Chapter, TArray<FTextEntry>& Out)
{
	FString CurLabel = TEXT("_root");
	int32 TextSeq = 0, ChoiceSeq = 0;
	for (const leo::FLeoCommand& Cmd : P.Commands)
	{
		switch (Cmd.Kind)
		{
		case leo::ELeoCmd::Label:
			CurLabel = LeoBridge::ToFString(Cmd.Label);
			TextSeq = 0;
			ChoiceSeq = 0;
			break;
		case leo::ELeoCmd::Text:
		{
			FTextEntry E;
			E.bExplicit = !Cmd.TextId.empty();
			E.Id = E.bExplicit
				? LeoBridge::ToFString(Cmd.TextId)
				: LeoL10n::MakeTextId(Chapter, CurLabel, TextSeq);
			E.Speaker = LeoBridge::ToFString(Cmd.Speaker);
			E.Source = LeoBridge::ToFString(Cmd.Body);
			Out.Add(MoveTemp(E));
			++TextSeq;
			break;
		}
		case leo::ELeoCmd::Choice:
			for (int32 i = 0; i < static_cast<int32>(Cmd.Options.size()); ++i)
			{
				FTextEntry E;
				E.bExplicit = !Cmd.Options[i].TextId.empty();
				E.Id = E.bExplicit
					? LeoBridge::ToFString(Cmd.Options[i].TextId)
					: LeoL10n::MakeOptionId(Chapter, CurLabel, ChoiceSeq, i);
				E.Source = LeoBridge::ToFString(Cmd.Options[i].Text);
				Out.Add(MoveTemp(E));
			}
			++ChoiceSeq;
			break;
		default:
			break;
		}
	}
	// 撞号检测（显式 ID 间重复编译期已拦；这里是自动 vs 显式 / 提取层面的重复）
	TSet<FString> Seen;
	int32 Collisions = 0;
	for (const FTextEntry& E : Out)
	{
		if (Seen.Contains(E.Id)) { ++Collisions; }
		Seen.Add(E.Id);
	}
	return Collisions;
}

bool ReadCsvRows(const FString& Path, TArray<FCsvRow>& Out, FString& OutError)
{
	Out.Reset();
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *Path))
	{
		OutError = TEXT("文件读取失败（不存在或被占用）");
		return false;
	}
	TArray<TArray<FString>> Rows;
	if (!LeoCsv::Parse(Content, Rows, OutError) || Rows.Num() < 1)
	{
		if (OutError.IsEmpty()) { OutError = TEXT("CSV 为空"); }
		return false;
	}
	int32 ColId, ColSpk, ColSrc, ColTr, ColSt;
	if (!MapCsvHeader(Rows, ColId, ColSpk, ColSrc, ColTr, ColSt))
	{
		OutError = TEXT("缺少 ID/Translation 列");
		return false;
	}
	auto Cell = [&](const TArray<FString>& Row, int32 Col) -> const FString&
	{
		static const FString Empty;
		return Col != INDEX_NONE && Col < Row.Num() ? Row[Col] : Empty;
	};
	for (int32 r = 1; r < Rows.Num(); ++r)
	{
		FCsvRow R;
		R.Id = Cell(Rows[r], ColId);
		R.Speaker = Cell(Rows[r], ColSpk);
		R.Source = Cell(Rows[r], ColSrc);
		R.Translation = Cell(Rows[r], ColTr);
		R.Status = Cell(Rows[r], ColSt);
		if (!R.Id.IsEmpty()) { Out.Add(MoveTemp(R)); }
	}
	return true;
}

FString WriteCsvRows(const TArray<FCsvRow>& Rows)
{
	FString Out = LeoCsv::WriteRow({ TEXT("ID"), TEXT("Speaker"), TEXT("Source"), TEXT("Translation"), TEXT("Status") });
	for (const FCsvRow& R : Rows)
	{
		Out += LINE_TERMINATOR;
		Out += LeoCsv::WriteRow({ R.Id, R.Speaker, R.Source, R.Translation, R.Status });
	}
	return Out;
}

void MergeRows(const TArray<FTextEntry>& Entries, TArray<FCsvRow>& InOut, FMergeStats& Stats)
{
	TMap<FString, int32> ById;
	for (int32 i = 0; i < InOut.Num(); ++i) { ById.Add(InOut[i].Id, i); }
	TArray<bool> Consumed;
	Consumed.Init(false, InOut.Num());
	TArray<FCsvRow> Result;
	for (const FTextEntry& E : Entries)
	{
		FCsvRow R;
		R.Id = E.Id;
		R.Speaker = E.Speaker;
		R.Source = E.Source;
		if (const int32* Idx = ById.Find(E.Id))
		{
			const FCsvRow& Old = InOut[*Idx];
			R.Translation = Old.Translation;
			if (Old.Source != E.Source)
			{
				R.Status = Old.Translation.IsEmpty() ? TEXT("NEW") : TEXT("STALE");
				++Stats.Stale;
			}
			else
			{
				R.Status = Old.Status.IsEmpty() && !Old.Translation.IsEmpty() ? TEXT("OK") : Old.Status;
			}
			Consumed[*Idx] = true;
		}
		else
		{
			R.Status = TEXT("NEW");
			++Stats.New;
		}
		Result.Add(MoveTemp(R));
	}
	for (int32 i = 0; i < InOut.Num(); ++i)
	{
		if (!Consumed[i])
		{
			FCsvRow R = MoveTemp(InOut[i]);
			R.Status = TEXT("GONE");
			Result.Add(MoveTemp(R));
			++Stats.Gone;
		}
	}
	InOut = MoveTemp(Result);
}

int32 ExtractForCultures(const TArray<FString>& Cultures)
{
	TArray<FString> Files;
	ListChapterFiles(Files);
	if (Files.Num() == 0)
	{
		UE_LOG(LogLeoL10nCmd, Error, TEXT("Content/Scripts 下没有 .leo 文件"));
		return 1;
	}
	struct FChapter
	{
		FString Name;
		TArray<FTextEntry> Entries;
	};
	TArray<FChapter> Chapters;
	int32 Failures = 0;
	for (const FString& F : Files)
	{
		const FString Path = ScriptsDir() / F;
		FString Source;
		if (!ReadUtf8File(Path, Source))
		{
			UE_LOG(LogLeoL10nCmd, Error, TEXT("读取失败: %s"), *Path);
			++Failures;
			continue;
		}
		const FString Name = FPaths::GetBaseFilename(F);
		const leo::FLeoProgram P = LeoBridge::CompileChapter(Source, Name);
		if (!P.Ok)
		{
			UE_LOG(LogLeoL10nCmd, Error, TEXT("编译失败（跳过）: %s —— %s(%d) %s"),
				*Name, LeoBridge::DiagName(P.Diags[0].Code), P.Diags[0].Line, *LeoBridge::ToFString(P.Diags[0].Msg));
			++Failures;
			continue;
		}
		FChapter Ch;
		Ch.Name = Name;
		const int32 Collisions = CollectEntries(P, Name, Ch.Entries);
		if (Collisions > 0)
		{
			UE_LOG(LogLeoL10nCmd, Warning,
				TEXT("%s：%d 个文本 ID 撞号（freeze 后插行未再 freeze？译文会错位）。建议：-action=freeze -chapter=%s"),
				*Name, Collisions, *Name);
		}
		Chapters.Add(MoveTemp(Ch));
	}
	for (const FString& Culture : Cultures)
	{
		const FString Dir = FLeoL10nTable::L10nDir() / Culture;
		IFileManager::Get().MakeDirectory(*Dir, true);
		for (const FChapter& Ch : Chapters)
		{
			const FString Path = Dir / (Ch.Name + TEXT(".csv"));
			TArray<FCsvRow> Rows;
			FString ReadErr; // 不存在 = 全新文件，不算失败
			ReadCsvRows(Path, Rows, ReadErr);
			FMergeStats Stats;
			MergeRows(Ch.Entries, Rows, Stats);
			if (FFileHelper::SaveStringToFile(WriteCsvRows(Rows), *Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogLeoL10nCmd, Display, TEXT("[extract] %s/%s.csv：%d 条（NEW %d / STALE %d / GONE %d）"),
					*Culture, *Ch.Name, Ch.Entries.Num(), Stats.New, Stats.Stale, Stats.Gone);
			}
			else
			{
				UE_LOG(LogLeoL10nCmd, Error, TEXT("写入失败: %s"), *Path);
				++Failures;
			}
		}
	}
	return Failures;
}

int32 FreezeChapters(const FString& OnlyChapter)
{
	TArray<FString> Files;
	ListChapterFiles(Files);
	int32 Failures = 0, Touched = 0;
	for (const FString& F : Files)
	{
		const FString Name = FPaths::GetBaseFilename(F);
		if (!OnlyChapter.IsEmpty() && Name != OnlyChapter) { continue; }
		const FString Path = ScriptsDir() / F;
		FString Raw;
		if (!ReadUtf8File(Path, Raw))
		{
			UE_LOG(LogLeoL10nCmd, Error, TEXT("读取失败: %s"), *Path);
			++Failures;
			continue;
		}
		const leo::FLeoProgram P = LeoBridge::CompileChapter(Raw, Name);
		if (!P.Ok)
		{
			UE_LOG(LogLeoL10nCmd, Error, TEXT("编译失败（跳过）: %s —— %s(%d)"),
				*Name, LeoBridge::DiagName(P.Diags[0].Code), P.Diags[0].Line);
			++Failures;
			continue;
		}

		const bool bCRLF = Raw.Contains(TEXT("\r\n"));
		TArray<FString> Lines;
		{
			// 手工按 \n 切行并剥行尾 \r：保留空行（行号与命令的对应关系不能乱），
			// 未修改的行原样保留，重join时按原行尾风格还原
			FString Cur;
			const TCHAR* S = *Raw;
			for (; *S; ++S)
			{
				if (*S == TEXT('\n'))
				{
					Lines.Add(MoveTemp(Cur));
					Cur.Reset();
				}
				else if (*S != TEXT('\r'))
				{
					Cur.AppendChar(*S);
				}
			}
			if (!Cur.IsEmpty() || Lines.Num() > 0) { Lines.Add(MoveTemp(Cur)); } // 末行无换行的余量
		}

		// 已有显式 ID 集合：freeze 后插进来的新行分配 ID 时避让，防止与冻结 ID 撞号错位
		TSet<FString> Taken;
		for (const leo::FLeoCommand& Cmd : P.Commands)
		{
			if (Cmd.Kind == leo::ELeoCmd::Text && !Cmd.TextId.empty()) { Taken.Add(LeoBridge::ToFString(Cmd.TextId)); }
			if (Cmd.Kind == leo::ELeoCmd::Choice)
			{
				for (const leo::FLeoOption& Opt : Cmd.Options)
				{
					if (!Opt.TextId.empty()) { Taken.Add(LeoBridge::ToFString(Opt.TextId)); }
				}
			}
		}
		auto AllocId = [&Taken](const FString& Base)
		{
			FString Id = Base;
			int32 N = 0;
			while (Taken.Contains(Id)) { Id = Base + FString::Printf(TEXT("-%d"), ++N); }
			Taken.Add(Id);
			return Id;
		};

		// 镜像遍历：编译成功 ⇒ 产出命令的行与命令序列一一对应。
		// choice 块内的选项行消费当前 Choice 命令的 Options，不推进 CmdIdx。
		auto RTrimCopy = [](const FString& S)
		{
			FString R = S;
			int32 End = R.Len();
			while (End > 0 && FChar::IsWhitespace(R[End - 1])) { --End; }
			R.LeftInline(End);
			return R;
		};
		int32 CmdIdx = 0, OptIdx = 0, TextSeq = 0, ChoiceSeq = 0, Appended = 0;
		bool bInChoice = false;
		FString CurLabel = TEXT("_root");
		auto CloseChoice = [&]()
		{
			if (bInChoice) { bInChoice = false; ++ChoiceSeq; ++CmdIdx; }
		};
		for (int32 Li = 0; Li < Lines.Num(); ++Li)
		{
			const FString& Line = Lines[Li];
			int32 First = INDEX_NONE;
			for (int32 c = 0; c < Line.Len(); ++c)
			{
				if (!FChar::IsWhitespace(Line[c])) { First = c; break; }
			}
			if (First == INDEX_NONE || Line[First] == TEXT('#')) { continue; }
			const int32 IndentSpaces = First;
			if (bInChoice && IndentSpaces == 4)
			{
				const leo::FLeoCommand& Cmd = P.Commands[CmdIdx];
				const leo::FLeoOption& Opt = Cmd.Options[OptIdx];
				if (Opt.TextId.empty())
				{
					const FString Id = AllocId(LeoL10n::MakeOptionId(Name, CurLabel, ChoiceSeq, OptIdx));
					Lines[Li] = RTrimCopy(Line) + TEXT(" id=") + Id;
					++Appended;
				}
				++OptIdx;
				if (OptIdx >= static_cast<int32>(Cmd.Options.size())) { CloseChoice(); }
				continue;
			}
			CloseChoice();
			if (IndentSpaces != 0) { continue; } // 非法缩进在编译期已拦截
			if (CmdIdx >= static_cast<int32>(P.Commands.size())) { continue; }

			const FString Head = Line.RightChop(First);
			const int32 Space = Head.Find(TEXT(" "));
			const FString Word = Space == INDEX_NONE ? Head : Head.Left(Space);
			const leo::FLeoCommand& Cmd = P.Commands[CmdIdx];
			if (Word == TEXT("choice"))
			{
				bInChoice = true;
				OptIdx = 0;
			}
			else if (Word == TEXT("label"))
			{
				CurLabel = LeoBridge::ToFString(Cmd.Label);
				TextSeq = 0;
				ChoiceSeq = 0;
				++CmdIdx;
			}
			else if (Word == TEXT("text"))
			{
				if (Cmd.TextId.empty())
				{
					const FString Id = AllocId(LeoL10n::MakeTextId(Name, CurLabel, TextSeq));
					Lines[Li] = RTrimCopy(Line) + TEXT(" id=") + Id;
					++Appended;
				}
				++TextSeq;
				++CmdIdx;
			}
			else
			{
				++CmdIdx; // 其余顶层命令每行一个命令
			}
		}
		CloseChoice();

		if (Appended == 0)
		{
			UE_LOG(LogLeoL10nCmd, Display, TEXT("[freeze] %s：已全部显式（无需写入）"), *Name);
			continue;
		}
		FString Joined;
		const FString Sep = bCRLF ? TEXT("\r\n") : TEXT("\n");
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			if (i > 0) { Joined += Sep; }
			Joined += Lines[i];
		}
		Joined += Sep; // 文件尾随换行
		if (!FFileHelper::SaveStringToFile(Joined, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogLeoL10nCmd, Error, TEXT("写入失败: %s"), *Path);
			++Failures;
			continue;
		}
		// 写回后自检：能编译且 text/选项全带显式 ID
		const leo::FLeoProgram Verify = LeoBridge::CompileChapter(Joined, Name);
		bool bAllExplicit = Verify.Ok;
		for (const leo::FLeoCommand& Cmd : Verify.Commands)
		{
			if (!bAllExplicit) { break; }
			if (Cmd.Kind == leo::ELeoCmd::Text && Cmd.TextId.empty()) { bAllExplicit = false; }
			if (Cmd.Kind == leo::ELeoCmd::Choice)
			{
				for (const leo::FLeoOption& Opt : Cmd.Options) { if (Opt.TextId.empty()) { bAllExplicit = false; } }
			}
		}
		if (!bAllExplicit)
		{
			UE_LOG(LogLeoL10nCmd, Error, TEXT("[freeze] %s 写回自检失败（已保留写入，请检查）"), *Name);
			++Failures;
			continue;
		}
		UE_LOG(LogLeoL10nCmd, Display, TEXT("[freeze] %s：写入 %d 个显式 ID"), *Name, Appended);
		++Touched;
	}
	UE_LOG(LogLeoL10nCmd, Display, TEXT("=== freeze 完成：%d 章 touched / %d 失败 ==="), Touched, Failures);
	return Failures;
}

void DiscoverCultures(TArray<FString>& Out)
{
	Out.Reset();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(FLeoL10nTable::L10nDir() / TEXT("*")), false, true);
	for (const FString& D : Dirs)
	{
		const FString Full = FLeoL10nTable::L10nDir() / D;
		if (IFileManager::Get().DirectoryExists(*Full))
		{
			Out.Add(FPaths::GetBaseFilename(Full));
		}
	}
	Out.Sort();
}

void BuildCultureChoices(TArray<FCultureChoice>& Out)
{
	Out.Reset();
	TArray<FString> Discovered;
	DiscoverCultures(Discovered); // 注意：DiscoverCultures 会 Reset 出参——传独立数组，勿把种子混进来
	auto MakeLabel = [](const FString& Code) -> FString
	{
		const FCulturePtr Culture = FInternationalization::Get().GetCulture(Code);
		return Culture.IsValid()
			? FString::Printf(TEXT("%s (%s)"), *Culture->GetNativeName(), *Code)
			: Code;
	};
	for (const FString& C : Discovered)
	{
		FCultureChoice Ch;
		Ch.Code = C;
		Ch.Label = MakeLabel(C);
		Ch.bHasCsv = true;
		Out.Add(MoveTemp(Ch));
	}
	// 引擎自身本地化过的语言全集（编辑器约十几门，含中日韩英法德等）；
	// 命令行环境拿不到时兜底常用三门
	TArray<FString> EngineCultures = FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Editor);
	if (EngineCultures.Num() == 0)
	{
		EngineCultures = { TEXT("en"), TEXT("zh-Hans"), TEXT("ja") };
	}
	EngineCultures.Sort();
	for (const FString& C : EngineCultures)
	{
		if (Discovered.Contains(C)) { continue; }
		FCultureChoice Ch;
		Ch.Code = C;
		Ch.Label = MakeLabel(C);
		Out.Add(MoveTemp(Ch));
	}
}

} // namespace LeoL10nToolkit
