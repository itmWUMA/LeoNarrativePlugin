#include "Settings/LeoNarrativeSettings.h"
#include "Misc/Paths.h"

FString ULeoNarrativeSettings::GetScriptsDirPath() const
{
	const FString Name = ScriptsDirName.IsEmpty() ? FString(TEXT("Scripts")) : ScriptsDirName;
	return FPaths::ProjectContentDir() / Name;
}

FString ULeoNarrativeSettings::GetL10nDirPath() const
{
	const FString Name = L10nDirName.IsEmpty() ? FString(TEXT("L10n")) : L10nDirName;
	return FPaths::ProjectContentDir() / Name;
}
