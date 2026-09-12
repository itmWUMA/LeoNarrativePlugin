// ULeoScenarioGraph 的资产工厂：Content 新建菜单的准入条件（UFactory::CanCreateNew）。
// 与 UAssetDefinition_LeoScenarioGraph 成对——工厂管"能建"，定义管"分类/颜色/打开"。
#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "LeoScenarioGraphFactory.generated.h"

class FFeedbackContext;
class UClass;
class UObject;

UCLASS()
class ULeoScenarioGraphFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
		UObject* Context, FFeedbackContext* Warn) override;
	virtual bool CanCreateNew() const override;
	// End of UFactory interface
};
