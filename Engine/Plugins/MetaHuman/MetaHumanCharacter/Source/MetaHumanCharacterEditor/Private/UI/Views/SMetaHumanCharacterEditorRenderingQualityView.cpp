// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorRenderingQualityView.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SourceControlHelpers.h"
#include "Templates/GuardValueAccessors.h"
#include "Misc/NotNull.h"
#include "Widgets/Input/SButton.h"

#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorRenderingQualitySettings.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTextComboBox.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorRenderingQualityView"

namespace UE::MetaHuman::Private
{
	/**
	 * Persists UMetaHumanCharacterEditorSettings to its default config file (e.g.
	 * <Project>/Config/DefaultMetaHumanCharacter.ini).
	 *
	 * Bare UObject::SaveConfig() ignores the `defaultconfig` UCLASS specifier and writes to the
	 * per-platform destination ini under <Project>/Saved/Config/<Platform>/, which is not what we
	 * want for shared/checked-in settings. TryUpdateDefaultConfigFile() targets the default layer
	 * directly, mirroring how the Project Settings UI (SSettingsEditor::HandleSettingsPropertyChanged)
	 * saves edits.
	 *
	 * If the target file is read-only we first try to check it out via source control so the user
	 * ends up with a properly tracked edit. If source control isn't available or the checkout
	 * fails, the RAII guard below temporarily clears the read-only attribute for the duration of
	 * the save and restores the original state on scope exit, so we don't silently leave a
	 * checked-in file marked writable.
	 *
	 * NOTE: UMetaHumanCharacterEditorSettings MUST keep the `defaultconfig` UCLASS specifier
	 * (CLASS_DefaultConfig) for this to do the right thing. The check() below enforces it.
	 */
	static void SaveDefaultConfig(TNotNull<UMetaHumanCharacterEditorSettings*> InSettings)
	{
		check(InSettings->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig));

		const FString FullPath = FPaths::ConvertRelativePathToFull(InSettings->GetDefaultConfigFilename());

		// Best-effort source-control checkout. If it succeeds the file is now writable and tracked,
		// and the RAII guard below becomes a no-op (it captures the post-checkout state).
		const bool bFileExists = FPaths::FileExists(FullPath);
		const bool bFileIsReadOnly = bFileExists && IFileManager::Get().IsReadOnly(*FullPath);
		const bool bSourceControlEnabled = ISourceControlModule::Get().IsEnabled();

		if (bFileIsReadOnly && bSourceControlEnabled)
		{
			const bool bSilentCheckout = false;
			const bool bCheckedOut = USourceControlHelpers::CheckOutOrAddFile(FullPath, bSilentCheckout);
			if (!bCheckedOut)
			{
				UE_LOGF(LogMetaHumanCharacterEditor, Warning,
					"Could not check out '%ls' from source control; falling back to a local read-only override for this save.",
					*FullPath);
			}
		}

		// Force the file writable for the duration of the save and restore the original read-only
		// state on scope exit (RAII). TGuardValueAccessors captures the current read-only state via
		// the getter, applies the scoped value (false = writable), and on destruction calls the
		// setter again with the originally captured value to restore it.
		const bool bSetReadOnly = false;
		TGuardValueAccessors<bool> ReadOnlyGuard(
			[&FullPath]()
			{
				return IFileManager::Get().IsReadOnly(*FullPath);
			},
			[&FullPath](const bool bNewReadOnly)
			{
				FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*FullPath, bNewReadOnly);
			},
			bSetReadOnly);

		InSettings->TryUpdateDefaultConfigFile();
	}
}

void SMetaHumanCharacterEditorRenderingQualityView::Construct(const FArguments& InArgs, const int32 InActiveProfileIndex)
{
	OnRenderingQualityProfileUpdate = InArgs._OnRenderingQualityProfileUpdate;

	CreateSettingsView();
	RebuildProfileNames();
	SwitchToProfile(InActiveProfileIndex, /* bBroadcastSwitch */ false);

	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(2.0f, 1.0f, 2.0f, 1.0f)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(2.0f)
			[
				SAssignNew(ProfileComboBox, SMetaHumanCharacterEditorTextComboBox, ProfileNames, ProfileNames.IsValidIndex(Proxy->ActiveProfileIndex) ? ProfileNames[Proxy->ActiveProfileIndex] : nullptr)
				.ToolTipText(LOCTEXT("RenderingQualityProfileComboBoxToolTip", "Allows for switching between scene environment and lighting profiles."))
				.OnSelectionChanged(this, &SMetaHumanCharacterEditorRenderingQualityView::SwitchToProfile)
				.IsEnabled_Lambda([this]() -> bool
				{
					return !ProfileNames.IsEmpty();
				})
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("AddProfileButton", "Add Profile"))
				.ToolTipText(LOCTEXT("RenderingQualityProfileAddProfile", "Adds a new profile."))
				.OnClicked(this, &SMetaHumanCharacterEditorRenderingQualityView::OnAddProfileClicked)
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("CloneProfileButton", "Clone Profile"))
				.ToolTipText(LOCTEXT("RenderingQualityProfileCloneProfile", "Clones this profile into a new profile."))
				.OnClicked(this, &SMetaHumanCharacterEditorRenderingQualityView::OnCloneProfileClicked)
			]
			+ SHorizontalBox::Slot()
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.Text_Lambda([this]()
				{
					if (Proxy->RenderingQualityProfile.bMetaHumanCharacterDefault)
					{
						return LOCTEXT("ResetProfileButton", "Reset Profile");
					}
					return LOCTEXT("RemoveProfileButton", "Remove Profile");
				})
				.ToolTipText_Lambda([this]()
				{
					if (Proxy->RenderingQualityProfile.bMetaHumanCharacterDefault)
					{
						return LOCTEXT("RenderingQualityProfileResetProfile", "Resets the currently selected profile.");
					}
					return LOCTEXT("RenderingQualityProfileRemoveProfile", "Removes the currently selected profile.");
				})
				.IsEnabled_Lambda([this]()->bool
				{
					// There should more than one profile to delete a profile
					return ProfileNames.Num() > 1 || Proxy->RenderingQualityProfile.bMetaHumanCharacterDefault;
				})
				.OnClicked(this, &SMetaHumanCharacterEditorRenderingQualityView::OnRemoveOrResetProfileClicked)
			]
		]
		+ SVerticalBox::Slot()
		.Padding(2.0f, 1.0f, 2.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				SettingsView->AsShared()
			]
		]
	];

	if (!Proxy->OnProfileNameUpdated.IsBoundToObject(this))
	{
		Proxy->OnProfileNameUpdated.AddSPLambda(this, [this]()
		{
			RebuildProfileNames();
			ProfileComboBox->SetOptions(ProfileNames);
			SetSelectedItem(Proxy->ActiveProfileIndex);
		});
	}
	if (!Proxy->OnProfileUpdated.IsBoundToObject(this))
	{
		Proxy->OnProfileUpdated.AddSPLambda(this, [this]()
		{
			OnRenderingQualityProfileUpdate.ExecuteIfBound(Proxy->ActiveProfileIndex);
		});
	}
}

void SMetaHumanCharacterEditorRenderingQualityView::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Proxy);
}

FString SMetaHumanCharacterEditorRenderingQualityView::GetReferencerName() const
{
	return TEXT("MetaHumanCharacterEditorRenderingQualityView");
}

void SMetaHumanCharacterEditorRenderingQualityView::SetSelectedItem(const int32 InIndex) const
{
	ProfileComboBox->SetSelectedItem(InIndex);
}

void SMetaHumanCharacterEditorRenderingQualityView::CreateSettingsView()
{
	Proxy = NewObject<UMetaHumanCharacterRenderingQualityProfileProxy>();

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	SettingsView = EditModule.CreateDetailView(DetailsViewArgs);

	SettingsView->SetObject(Proxy);
}

void SMetaHumanCharacterEditorRenderingQualityView::RebuildProfileNames()
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);
	ProfileNames.Empty();
	for (const FMetaHumanCharacterRenderingQualityProfile& RenderingQualityProfile: Settings->GetAllRenderingQualityProfiles())
	{
		ProfileNames.Add(MakeShared<FString>(RenderingQualityProfile.ProfileName));
	}
}

void SMetaHumanCharacterEditorRenderingQualityView::SwitchToProfile(const int32 InIndex) const
{
	SwitchToProfile(InIndex, /* bBroadcastSwitch */ true);
}

void SMetaHumanCharacterEditorRenderingQualityView::SwitchToProfile(const int32 InIndex, const bool bBroadcastSwitch) const
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);
	if (Settings->IsValidRenderingQualityProfileIndex(InIndex))
	{
		Proxy->ActiveProfileIndex = InIndex;
		Proxy->SetActiveProfile(Settings->GetRenderingQualityProfile(InIndex));
		SettingsView->ForceRefresh();
		if (bBroadcastSwitch)
		{
			OnRenderingQualityProfileUpdate.ExecuteIfBound(Proxy->ActiveProfileIndex);
		}
	}
}

FReply SMetaHumanCharacterEditorRenderingQualityView::OnAddProfileClicked()
{
	FMetaHumanCharacterRenderingQualityProfile RenderingQualityProfile (TEXT("NewProfile"));
	CreateAndSelectNewProfile(RenderingQualityProfile);

	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorRenderingQualityView::OnCloneProfileClicked()
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);
	if (Settings->IsValidRenderingQualityProfileIndex(Proxy->ActiveProfileIndex))
	{
		FMetaHumanCharacterRenderingQualityProfile RenderingQualityProfile = Settings->GetRenderingQualityProfile(Proxy->ActiveProfileIndex);
		RenderingQualityProfile.ProfileName += TEXT("_Clone");
		RenderingQualityProfile.bMetaHumanCharacterDefault = false;
		CreateAndSelectNewProfile(RenderingQualityProfile);
	}
	return FReply::Handled();
}

FReply SMetaHumanCharacterEditorRenderingQualityView::OnRemoveOrResetProfileClicked()
{
	UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	if (Proxy->RenderingQualityProfile.bMetaHumanCharacterDefault)
	{
		if (Proxy->RenderingQualityProfile.ProfileName == FMetaHumanCharacterRenderingQualityProfile::Epic.ProfileName)
		{
			Settings->SetRenderingQualityProfile(Proxy->ActiveProfileIndex, FMetaHumanCharacterRenderingQualityProfile::Epic);
		}
		else if (Proxy->RenderingQualityProfile.ProfileName == FMetaHumanCharacterRenderingQualityProfile::High.ProfileName)
		{
			Settings->SetRenderingQualityProfile(Proxy->ActiveProfileIndex, FMetaHumanCharacterRenderingQualityProfile::High);
		}
		else if (Proxy->RenderingQualityProfile.ProfileName == FMetaHumanCharacterRenderingQualityProfile::Medium.ProfileName)
		{
			Settings->SetRenderingQualityProfile(Proxy->ActiveProfileIndex, FMetaHumanCharacterRenderingQualityProfile::Medium);
		}

		UE::MetaHuman::Private::SaveDefaultConfig(Settings);
		SwitchToProfile(Proxy->ActiveProfileIndex);
	}
	else
	{
		const int32 UserProfileIndex = Settings->GetUserRenderingQualityProfileIndex(Proxy->ActiveProfileIndex);
		Settings->RemoveUserRenderingQualityProfile(UserProfileIndex);

		UE::MetaHuman::Private::SaveDefaultConfig(Settings);

		RebuildProfileNames();
		ProfileComboBox->SetOptions(ProfileNames);

		if (!ProfileNames.IsEmpty())
		{
			SetSelectedItem(0);
		}
	}

	return FReply::Handled();
}

void SMetaHumanCharacterEditorRenderingQualityView::CreateAndSelectNewProfile(FMetaHumanCharacterRenderingQualityProfile& InRenderingQuality)
{
	UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);
	
	const TArray<FMetaHumanCharacterRenderingQualityProfile> AllProfiles = Settings->GetAllRenderingQualityProfiles();
	const FString UniqueProfileName = UMetaHumanCharacterEditorSettings::MakeUniqueKey(
		InRenderingQuality.ProfileName,
		[&AllProfiles](const FString& Key)
		{
			return AllProfiles.ContainsByPredicate([&Key](const FMetaHumanCharacterRenderingQualityProfile& RenderingQualityProfile)
			{
				return RenderingQualityProfile.ProfileName == Key;
			});
		}
	);

	InRenderingQuality.ProfileName = UniqueProfileName;
	Settings->AddUserRenderingQualityProfile(InRenderingQuality);
	UE::MetaHuman::Private::SaveDefaultConfig(Settings);

	RebuildProfileNames();
	ProfileComboBox->SetOptions(ProfileNames);

	SetSelectedItem(Settings->GetAllRenderingQualityProfilesNum() - 1);
}

#undef LOCTEXT_NAMESPACE
