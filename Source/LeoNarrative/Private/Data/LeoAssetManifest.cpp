#include "Data/LeoAssetManifest.h"
#include "LevelSequence.h"
#include "Sound/SoundBase.h"

void ULeoAssetManifest::PostLoad()
{
	Super::PostLoad();

	// M9 分表 → 类别节一次性迁移：内置 6 表非空者搬进 Categories（框架默认期望类兜底，
	// 不在节上重复声明），迁完清空源字段。已存资产重存一次即净
	struct FLegacyTable { FName Category; TMap<FName, FSoftObjectPath> ULeoAssetManifest::* Field; };
	const FLegacyTable Legacy[] = {
		{ BgmCategory(),   &ULeoAssetManifest::Bgm },
		{ SeCategory(),    &ULeoAssetManifest::Se },
		{ VoiceCategory(), &ULeoAssetManifest::Voice },
		{ BgCategory(),    &ULeoAssetManifest::Bg },
		{ CharCategory(),  &ULeoAssetManifest::Char },
		{ SeqCategory(),   &ULeoAssetManifest::Seq },
	};
	bool bMigrated = false;
	for (const FLegacyTable& L : Legacy)
	{
		TMap<FName, FSoftObjectPath>& Src = this->*L.Field;
		if (Src.Num() == 0) { continue; }
		FLeoManifestCategory* Cat = FindCategory(L.Category);
		if (!Cat)
		{
			Cat = &Categories.AddDefaulted_GetRef();
			Cat->Name = L.Category;
		}
		for (TPair<FName, FSoftObjectPath>& KV : Src)
		{
			Cat->Assets.Add(KV.Key, KV.Value); // 与既有节同键：保留类别节条目
		}
		Src.Reset();
		bMigrated = true;
	}
	if (bMigrated)
	{
		Modify(false);
		MarkPackageDirty();
	}
}

const FLeoManifestCategory* ULeoAssetManifest::FindCategory(FName Category) const
{
	for (const FLeoManifestCategory& Cat : Categories)
	{
		if (Cat.Name == Category) { return &Cat; }
	}
	return nullptr;
}

FLeoManifestCategory* ULeoAssetManifest::FindCategory(FName Category)
{
	for (FLeoManifestCategory& Cat : Categories)
	{
		if (Cat.Name == Category) { return &Cat; }
	}
	return nullptr;
}

bool ULeoAssetManifest::FindCategoryOf(FName LogicalId, FName& OutCategory) const
{
	for (const FLeoManifestCategory& Cat : Categories)
	{
		if (Cat.Assets.Contains(LogicalId))
		{
			OutCategory = Cat.Name;
			return true;
		}
	}
	return false;
}

bool ULeoAssetManifest::TryResolve(FName Category, FName LogicalId, FSoftObjectPath& OutPath) const
{
	if (const FLeoManifestCategory* Cat = FindCategory(Category))
	{
		if (const FSoftObjectPath* P = Cat->Assets.Find(LogicalId))
		{
			OutPath = *P;
			return true;
		}
	}
	if (const FSoftObjectPath* P = Assets.Find(LogicalId)) // 旧平表回落
	{
		OutPath = *P;
		return true;
	}
	return false;
}

bool ULeoAssetManifest::TryResolve(FName LogicalId, FSoftObjectPath& OutPath) const
{
	FName Unused;
	if (FindCategoryOf(LogicalId, Unused))
	{
		OutPath = *FindCategory(Unused)->Assets.Find(LogicalId);
		return true;
	}
	if (const FSoftObjectPath* P = Assets.Find(LogicalId))
	{
		OutPath = *P;
		return true;
	}
	return false;
}

const UClass* ULeoAssetManifest::GetBuiltInExpectedClass(FName Category)
{
	if (Category == BgmCategory() || Category == SeCategory() || Category == VoiceCategory())
	{
		return USoundBase::StaticClass();
	}
	if (Category == SeqCategory())
	{
		return ULevelSequence::StaticClass();
	}
	return nullptr; // bg/char 及自定义类别：默认只核存在性（节上可声明期望类）
}

const UClass* ULeoAssetManifest::ResolveExpectedClass(const FLeoManifestCategory& Cat) const
{
	if (Cat.ExpectedClass.IsValid())
	{
		return Cat.ExpectedClass.Get();
	}
	if (!Cat.ExpectedClass.IsNull()) // 软类未加载：尝试同步加载（校验路径可接受）
	{
		if (UClass* Loaded = Cat.ExpectedClass.LoadSynchronous())
		{
			return Loaded;
		}
	}
	return GetBuiltInExpectedClass(Cat.Name);
}
