// ULeoAssetManifest 的资产工厂：新建时预填 6 个内置类别节（bgm/se/voice/bg/char/seq，
// 期望类走框架默认表不重复声明）——作者不必记 token 名即可直接开始拖资产入库。
// 与 UAssetDefinition_LeoAssetManifest 成对——工厂管"能建"，定义管"分类/颜色/打开"。
#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "LeoAssetManifestFactory.generated.h"

class FFeedbackContext;
class UClass;
class UObject;

UCLASS()
class ULeoAssetManifestFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
	virtual bool CanCreateNew() const override;
	// End of UFactory interface
};
