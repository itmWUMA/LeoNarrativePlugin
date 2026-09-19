#include "Blackboard/NarrativeBlackboard.h"
#include "ScriptRuntime/LeoScriptBridge.h"

bool UNarrativeBlackboard::GetValue(FName Key, leo::FLeoValue& Out) const
{
	if (Values.Contains(Key)) { Out = Values.FindChecked(Key); return true; }
	if (Parent) { return Parent->GetValue(Key, Out); }
	return false;
}

bool UNarrativeBlackboard::GetOwnValue(FName Key, leo::FLeoValue& Out) const
{
	if (const leo::FLeoValue* V = Values.Find(Key)) { Out = *V; return true; }
	return false;
}

bool UNarrativeBlackboard::HasValue(FName Key) const
{
	if (Values.Contains(Key)) { return true; }
	return Parent && Parent->HasValue(Key);
}

void UNarrativeBlackboard::SetValue(FName Key, const leo::FLeoValue& Val)
{
	leo::FLeoValue Old;
	const leo::FLeoValue* Existing = Values.Find(Key);
	if (Existing) { Old = *Existing; }
	Values.Add(Key, Val);
	OnValueChanged.Broadcast(Key, Old, Val);
	OnValueChangedBP.Broadcast(Key, LeoBridge::ToFString(Old.ToString()), LeoBridge::ToFString(Val.ToString()));
}

bool UNarrativeBlackboard::RemoveValue(FName Key)
{
	return Values.Remove(Key) > 0;
}

void UNarrativeBlackboard::GetOwnKeys(TArray<FName>& OutKeys) const
{
	Values.GetKeys(OutKeys);
}

void UNarrativeBlackboard::DumpToMap(TMap<FName, leo::FLeoValue>& OutMap) const
{
	OutMap = Values;
}

void UNarrativeBlackboard::RestoreFromMap(const TMap<FName, leo::FLeoValue>& InMap)
{
	for (const TPair<FName, leo::FLeoValue>& KV : InMap)
	{
		SetValue(KV.Key, KV.Value);
	}
}

// ---- 蓝图扁平 API（转换在 UE 侧；bFound = 键存在，含 Parent 链）----

namespace
{
	ELeoBBValueType ToBPType(leo::FLeoValue::EKind K)
	{
		switch (K)
		{
		case leo::FLeoValue::EKind::Bool:   return ELeoBBValueType::Bool;
		case leo::FLeoValue::EKind::Int:    return ELeoBBValueType::Int;
		case leo::FLeoValue::EKind::Float:  return ELeoBBValueType::Float;
		case leo::FLeoValue::EKind::String: return ELeoBBValueType::String;
		default:                            return ELeoBBValueType::Null;
		}
	}
}

int32 UNarrativeBlackboard::GetInt(FName Key, bool& bFound) const
{
	leo::FLeoValue V;
	bFound = GetValue(Key, V);
	return V.IsNumber() ? static_cast<int32>(V.AsDouble()) : 0;
}

float UNarrativeBlackboard::GetFloat(FName Key, bool& bFound) const
{
	leo::FLeoValue V;
	bFound = GetValue(Key, V);
	return V.IsNumber() ? static_cast<float>(V.AsDouble()) : 0.f;
}

bool UNarrativeBlackboard::GetBool(FName Key, bool& bFound) const
{
	leo::FLeoValue V;
	bFound = GetValue(Key, V);
	return V.Kind == leo::FLeoValue::EKind::Bool ? V.B : false;
}

FString UNarrativeBlackboard::GetString(FName Key, bool& bFound) const
{
	leo::FLeoValue V;
	bFound = GetValue(Key, V);
	if (!bFound) { return FString(); }
	return V.Kind == leo::FLeoValue::EKind::String
		? LeoBridge::ToFString(V.S)
		: LeoBridge::ToFString(V.ToString()); // 非字符串值给可读形式（同调试器显示）
}

ELeoBBValueType UNarrativeBlackboard::GetValueType(FName Key, bool& bFound) const
{
	leo::FLeoValue V;
	bFound = GetValue(Key, V);
	return bFound ? ToBPType(V.Kind) : ELeoBBValueType::Null;
}

void UNarrativeBlackboard::SetInt(FName Key, int32 Value)
{
	SetValue(Key, leo::FLeoValue::MakeInt(Value));
}

void UNarrativeBlackboard::SetFloat(FName Key, float Value)
{
	SetValue(Key, leo::FLeoValue::MakeFloat(Value));
}

void UNarrativeBlackboard::SetBool(FName Key, bool Value)
{
	SetValue(Key, leo::FLeoValue::MakeBool(Value));
}

void UNarrativeBlackboard::SetString(FName Key, const FString& Value)
{
	SetValue(Key, leo::FLeoValue::MakeString(LeoBridge::ToUtf8(Value)));
}
