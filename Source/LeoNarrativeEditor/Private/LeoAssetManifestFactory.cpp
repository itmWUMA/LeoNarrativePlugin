#include "LeoAssetManifestFactory.h"

#include "Data/LeoAssetManifest.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LeoAssetManifestFactory)

ULeoAssetManifestFactory::ULeoAssetManifestFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = ULeoAssetManifest::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true; // 新建后直接打开专属清单编辑器
}

bool ULeoAssetManifestFactory::CanCreateNew() const
{
	return true;
}

UObject* ULeoAssetManifestFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf<ULeoAssetManifest>());
	ULeoAssetManifest* Manifest = NewObject<ULeoAssetManifest>(InParent, Class, Name, Flags);
	// 预填内置类别节：拖资产前结构就位（自定义类别随时可加）
	for (const FName Cat : {
		ULeoAssetManifest::BgmCategory(), ULeoAssetManifest::SeCategory(), ULeoAssetManifest::VoiceCategory(),
		ULeoAssetManifest::BgCategory(), ULeoAssetManifest::CharCategory(), ULeoAssetManifest::SeqCategory(),
	})
	{
		FLeoManifestCategory& Node = Manifest->Categories.AddDefaulted_GetRef();
		Node.Name = Cat;
	}
	return Manifest;
}
