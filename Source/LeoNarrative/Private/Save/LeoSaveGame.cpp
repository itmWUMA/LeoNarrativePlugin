#include "Save/LeoSaveGame.h"

FLeoSavedValue FLeoSavedValue::From(const leo::FLeoValue& V)
{
	FLeoSavedValue SV;
	switch (V.Kind)
	{
	case leo::FLeoValue::EKind::Bool:   SV.Type = 1; SV.B = V.B; break;
	case leo::FLeoValue::EKind::Int:    SV.Type = 2; SV.I = V.I; break;
	case leo::FLeoValue::EKind::Float:  SV.Type = 3; SV.F = V.F; break;
	case leo::FLeoValue::EKind::String:
	{
		SV.Type = 4;
		const FUTF8ToTCHAR Conv(V.S.c_str());
		SV.S = FString(Conv.Length(), Conv.Get());
		break;
	}
	default: SV.Type = 0; break;
	}
	return SV;
}

leo::FLeoValue FLeoSavedValue::To(const FLeoSavedValue& SV)
{
	switch (SV.Type)
	{
	case 1: return leo::FLeoValue::MakeBool(SV.B);
	case 2: return leo::FLeoValue::MakeInt(SV.I);
	case 3: return leo::FLeoValue::MakeFloat(SV.F);
	case 4:
	{
		const FTCHARToUTF8 Conv(*SV.S);
		return leo::FLeoValue::MakeString(std::string(Conv.Get(), Conv.Length()));
	}
	default: return leo::FLeoValue();
	}
}
