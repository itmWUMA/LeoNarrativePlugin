#include "Blackboard/NarrativeBlackboard.h"

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
