// NarrativeBlackboard：自研轻量叙事黑板（不用 AIModule 的 UBlackboardComponent）
// 值模型复用编译器内核的 leo::FLeoValue（动态类型，跨 DLL 安全：同 CRT 动态链接）
#pragma once

#include "CoreMinimal.h"
#include "Script/LeoTypes.h"
#include "NarrativeBlackboard.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FLeoBBValueChanged, FName, const leo::FLeoValue& /*Old*/, const leo::FLeoValue& /*New*/);

UCLASS()
class LEONARRATIVE_API UNarrativeBlackboard : public UObject
{
	GENERATED_BODY()

public:
	// 读：本层命中返回；否则沿 Parent 链回落（局部→全局）
	bool GetValue(FName Key, leo::FLeoValue& Out) const;
	// 仅查本层（不含 Parent）
	bool GetOwnValue(FName Key, leo::FLeoValue& Out) const;
	bool HasValue(FName Key) const; // 含 Parent 链

	// 写：永远写本层（set 语义；遮蔽同名全局变量由读取回落规则自然处理）
	void SetValue(FName Key, const leo::FLeoValue& Val);
	bool RemoveValue(FName Key);

	void GetOwnKeys(TArray<FName>& OutKeys) const;
	// 本层快照 / 恢复（存档用；Restore 覆盖式合并）
	void DumpToMap(TMap<FName, leo::FLeoValue>& OutMap) const;
	void RestoreFromMap(const TMap<FName, leo::FLeoValue>& InMap);

	// 作用域链：局部黑板的 Parent 指向全局黑板
	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> Parent;

	// 变更通知（C++ 多播；Old 为 Null 表示新建）
	FLeoBBValueChanged OnValueChanged;

private:
	// 纯 C++ 值不做反射；序列化走 Dump/Restore 手动管道
	TMap<FName, leo::FLeoValue> Values;
};
