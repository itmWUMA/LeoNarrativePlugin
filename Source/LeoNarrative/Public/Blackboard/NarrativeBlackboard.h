// NarrativeBlackboard：自研轻量叙事黑板（不用 AIModule 的 UBlackboardComponent）
// 值模型复用编译器内核的 leo::FLeoValue（动态类型，跨 DLL 安全：同 CRT 动态链接）
#pragma once

#include "CoreMinimal.h"
#include "Script/LeoTypes.h"
#include "NarrativeBlackboard.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FLeoBBValueChanged, FName, const leo::FLeoValue& /*Old*/, const leo::FLeoValue& /*New*/);

// 蓝图变更通知（值为 ToString 可读形式；Old 为 "null" 表示新建）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FLeoBBValueChangedBP, FName, Key, FString, OldValue, FString, NewValue);

// BP 侧的值类型视图（FLeoValue 不做反射——铁律 #1 边界，枚举镜像而非类型暴露）
UENUM()
enum class ELeoBBValueType : uint8 { Null, Bool, Int, Float, String };

UCLASS()
class LEONARRATIVE_API UNarrativeBlackboard : public UObject
{
	GENERATED_BODY()

public:
	// 读：本层命中返回；否则沿 Parent 链回落（局部→全局）
	bool GetValue(FName Key, leo::FLeoValue& Out) const;
	// 仅查本层（不含 Parent）
	bool GetOwnValue(FName Key, leo::FLeoValue& Out) const;
	UFUNCTION(BlueprintPure, Category = "LeoNarrative|Blackboard")
	bool HasValue(FName Key) const; // 含 Parent 链

	// 写：永远写本层（set 语义；遮蔽同名全局变量由读取回落规则自然处理）
	void SetValue(FName Key, const leo::FLeoValue& Val);
	UFUNCTION(BlueprintCallable, Category = "LeoNarrative|Blackboard")
	bool RemoveValue(FName Key);

	void GetOwnKeys(TArray<FName>& OutKeys) const;
	// 本层快照 / 恢复（存档用；Restore 覆盖式合并）
	void DumpToMap(TMap<FName, leo::FLeoValue>& OutMap) const;
	void RestoreFromMap(const TMap<FName, leo::FLeoValue>& InMap);

	// ---- 蓝图扁平 API（FLeoValue 不做反射；类型转换在 UE 侧完成）----
	// bFound = 键存在（含 Parent 链）；类型不符时：数值类型按 double 互相强转，其余回默认值
	UFUNCTION(BlueprintPure, Category = "LeoNarrative|Blackboard")
	int32 GetInt(FName Key, bool& bFound) const;
	UFUNCTION(BlueprintPure, Category = "LeoNarrative|Blackboard")
	float GetFloat(FName Key, bool& bFound) const;
	UFUNCTION(BlueprintPure, Category = "LeoNarrative|Blackboard")
	bool GetBool(FName Key, bool& bFound) const;
	UFUNCTION(BlueprintPure, Category = "LeoNarrative|Blackboard", meta = (ToolTip = "字符串值原样返回；其他类型返回可读形式（同调试器显示）"))
	FString GetString(FName Key, bool& bFound) const;
	UFUNCTION(BlueprintPure, Category = "LeoNarrative|Blackboard")
	ELeoBBValueType GetValueType(FName Key, bool& bFound) const;

	UFUNCTION(BlueprintCallable, Category = "LeoNarrative|Blackboard")
	void SetInt(FName Key, int32 Value);
	UFUNCTION(BlueprintCallable, Category = "LeoNarrative|Blackboard")
	void SetFloat(FName Key, float Value);
	UFUNCTION(BlueprintCallable, Category = "LeoNarrative|Blackboard")
	void SetBool(FName Key, bool Value);
	UFUNCTION(BlueprintCallable, Category = "LeoNarrative|Blackboard")
	void SetString(FName Key, const FString& Value);

	// 作用域链：局部黑板的 Parent 指向全局黑板
	UPROPERTY()
	TObjectPtr<UNarrativeBlackboard> Parent;

	// 变更通知（C++ 多播；Old 为 Null 表示新建）
	FLeoBBValueChanged OnValueChanged;

	// 蓝图变更通知（与 C++ OnValueChanged 同流）
	UPROPERTY(BlueprintAssignable, Category = "LeoNarrative|Blackboard")
	FLeoBBValueChangedBP OnValueChangedBP;

private:
	// 纯 C++ 值不做反射；序列化走 Dump/Restore 手动管道
	TMap<FName, leo::FLeoValue> Values;
};
