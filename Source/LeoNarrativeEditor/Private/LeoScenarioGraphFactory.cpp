#include "LeoScenarioGraphFactory.h"

#include "Data/LeoScenarioGraph.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LeoScenarioGraphFactory)

ULeoScenarioGraphFactory::ULeoScenarioGraphFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = ULeoScenarioGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true; // 新建后直接打开专属编辑器
}

bool ULeoScenarioGraphFactory::CanCreateNew() const
{
	return true;
}

UObject* ULeoScenarioGraphFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf<ULeoScenarioGraph>());
	return NewObject<ULeoScenarioGraph>(InParent, Class, Name, Flags);
}
