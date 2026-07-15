// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandboxedEditingSettings.h"

#include "LogSandboxedEditing.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SandboxedEditingSettings"

USandboxedEditingSettings::FOnCustomDirectoryChanged USandboxedEditingSettings::OnCustomDirectoryChanged;

namespace UE::SandboxedEditing::Private
{
	/** Returns true if the proposed sandbox storage path overlaps with the project content directory. */
	static bool DoesPathOverlapProjectContent(const FString& InProposedPath, FString& OutNormalizedContentDir)
	{
		FString NormalizedProposed = InProposedPath;
		FPaths::NormalizeDirectoryName(NormalizedProposed);
		NormalizedProposed = FPaths::ConvertRelativePathToFull(NormalizedProposed);

		OutNormalizedContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		FPaths::NormalizeDirectoryName(OutNormalizedContentDir);

		if (FPaths::IsSamePath(NormalizedProposed, OutNormalizedContentDir))
		{
			return true;
		}

		// Either the proposed path lives inside Content, or Content lives inside the proposed path.
		return FPaths::IsUnderDirectory(NormalizedProposed, OutNormalizedContentDir)
			|| FPaths::IsUnderDirectory(OutNormalizedContentDir, NormalizedProposed);
	}
}

void USandboxedEditingSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(USandboxedEditingSettings, CustomSandboxStorageDirectory))
	{
		if (!CustomSandboxStorageDirectory.Path.IsEmpty())
		{
			FString ContentDir;
			if (UE::SandboxedEditing::Private::DoesPathOverlapProjectContent(CustomSandboxStorageDirectory.Path, ContentDir))
			{
				UE_LOG(LogSandboxedEditing, Warning,
					TEXT("Rejected custom sandbox storage directory '%s' because it overlaps with the project content directory '%s'."),
					*CustomSandboxStorageDirectory.Path, *ContentDir);

				FMessageDialog::Open(
					EAppMsgType::Ok,
					FText::Format(
						LOCTEXT("OverlapsContentDir",
							"The sandbox storage directory cannot be inside, or contain, the project's Content directory.\n\n"
							"Selected: {0}\nContent directory: {1}\n\n"
							"Please choose a directory outside of the project Content folder."),
						FText::FromString(CustomSandboxStorageDirectory.Path),
						FText::FromString(ContentDir)),
					LOCTEXT("InvalidSandboxStorageDirectory", "Invalid Sandbox Storage Directory"));

				CustomSandboxStorageDirectory.Path.Reset();
				SaveConfig();
				return;
			}
		}

		OnCustomDirectoryChanged.Broadcast();
	}
}

#undef LOCTEXT_NAMESPACE
