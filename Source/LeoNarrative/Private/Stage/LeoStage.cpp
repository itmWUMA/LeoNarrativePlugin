#include "Stage/LeoStage.h"
#include "Data/LeoAssetManifest.h"

void ULeoStage::HandleEvent(const FLeoEvent& Ev)
{
	switch (Ev.Kind)
	{
	case ELeoEventKind::Bg:
		CurrentBg = Ev.AssetId;
		CurrentBgPath = FSoftObjectPath();
		if (Manifest)
		{
			FSoftObjectPath Path;
			if (Manifest->TryResolve(Ev.AssetId, Path))
			{
				CurrentBgPath = Path;
			}
		}
		break;

	case ELeoEventKind::Char:
	{
		FLeoCharState& S = Chars.FindOrAdd(Ev.Slot);
		S.AssetId = Ev.AssetId;
		if (!Ev.At.IsEmpty()) { S.At = Ev.At; }
		if (!Ev.Pose.IsEmpty()) { S.Pose = Ev.Pose; }
		if (!Ev.Motion.IsEmpty()) { S.Motion = Ev.Motion; }
		if (Ev.AssetId == TEXT("-")) { Chars.Remove(Ev.Slot); }
		break;
	}
	default:
		break;
	}
}
