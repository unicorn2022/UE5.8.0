// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterViewport.h"
#include "MetaHumanSDKSettings.h"
#include "MetaHumanCharacterEditorLog.h"
#include "Logging/MessageLog.h"
#include "MetaHumanSDKEditor.h"
#include "Misc/ConfigCacheIni.h"
#include "ObjectTools.h"
#include "Misc/TransactionObjectEvent.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Algo/Transform.h"
#include "Algo/AllOf.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Interfaces/IProjectManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorSettings"

void UMetaHumanCharacterEditorSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Only run migration for the CDO.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	// One-time migration from EditorPerProjectUserSettings to project-level config (DefaultEditor.ini).
	// Previously this class used config=EditorPerProjectUserSettings which saved per-user.
	if (GConfig && FPaths::FileExists(GEditorPerProjectIni))
	{
		const TCHAR* OldConfigSection = TEXT("/Script/MetaHumanCharacterEditor.MetaHumanCharacterEditorSettings");

		if (GConfig->DoesSectionExist(OldConfigSection, GEditorPerProjectIni))
		{
			// Load old values now so settings are correct immediately.
			LoadConfig(StaticClass(), *GEditorPerProjectIni);

			if (!TryUpdateDefaultConfigFile())
			{
				const FString AbsConfigPath = FPaths::ConvertRelativePathToFull(GetDefaultConfigFilename());

				FMessageLog(UE::MetaHuman::MessageLogName)
					.Warning(LOCTEXT("MigrateConfigFailed",
						"Failed to migrate MetaHuman Character Editor settings to project config. "
						"Please ensure the config file is writable or checked out from source control: "))
					->AddToken(FURLToken::Create(
						FString::Printf(TEXT("file://%s"), *AbsConfigPath),
						FText::FromString(AbsConfigPath)));

				FMessageLog(UE::MetaHuman::MessageLogName)
					.Open();
			}
			else
			{
				GConfig->EmptySection(OldConfigSection, GEditorPerProjectIni);

				const bool bRemoveFromCache = false;
				GConfig->Flush(bRemoveFromCache, GEditorPerProjectIni);

				UE_LOGF(LogMetaHumanCharacterEditor, Log,
					"Migrated MetaHumanCharacterEditorSettings from EditorPerProjectUserSettings to project config: %ls.",
					*GetClass()->GetConfigName());
			}
		}
	}
}

UMetaHumanCharacterEditorSettings::UMetaHumanCharacterEditorSettings()
{
	// Set the initial value of MigratePackagePath to be same as the one defined for cinematic characters in the SDK settings
	const UMetaHumanSDKSettings* MetaHumanSDKSettings = GetDefault<UMetaHumanSDKSettings>();
	MigratedPackagePath = MetaHumanSDKSettings->CinematicImportPath;

	DefaultRenderingQualityProfiles =
	{
		FMetaHumanCharacterRenderingQualityProfile::Epic,
		FMetaHumanCharacterRenderingQualityProfile::High,
		FMetaHumanCharacterRenderingQualityProfile::Medium
	};

	IAssetRegistry::GetChecked().OnAssetRemoved().AddUObject(this, &UMetaHumanCharacterEditorSettings::OnAssetRemoved);
}

TArray<FMetaHumanCharacterRenderingQualityProfile> UMetaHumanCharacterEditorSettings::GetAllRenderingQualityProfiles() const
{
	TArray<FMetaHumanCharacterRenderingQualityProfile> Result;
	Result.Reserve(DefaultRenderingQualityProfiles.Num() + UserRenderingQualityProfiles.Num());
	Result.Append(DefaultRenderingQualityProfiles);
	Result.Append(UserRenderingQualityProfiles);
	return Result;
}

int32 UMetaHumanCharacterEditorSettings::GetAllRenderingQualityProfilesNum() const
{
	return DefaultRenderingQualityProfiles.Num() + UserRenderingQualityProfiles.Num();
}

FMetaHumanCharacterRenderingQualityProfile UMetaHumanCharacterEditorSettings::GetRenderingQualityProfile(const int32 InIndex) const
{
	if (!ensureMsgf(IsValidRenderingQualityProfileIndex(InIndex), TEXT("Invalid profile index %d"), InIndex))
	{
		return FMetaHumanCharacterRenderingQualityProfile::Invalid;
	}

	if (InIndex < DefaultRenderingQualityProfiles.Num())
	{
		return DefaultRenderingQualityProfiles[InIndex];
	}

	return UserRenderingQualityProfiles[GetUserRenderingQualityProfileIndex(InIndex)];
}

void UMetaHumanCharacterEditorSettings::SetRenderingQualityProfile(const int32 InIndex, const FMetaHumanCharacterRenderingQualityProfile& InProfile)
{
	if (!ensureMsgf(IsValidRenderingQualityProfileIndex(InIndex), TEXT("Invalid profile index %d"), InIndex))
	{
		return;
	}

	if (InIndex < DefaultRenderingQualityProfiles.Num())
	{
		DefaultRenderingQualityProfiles[InIndex] = InProfile;
	}
	else
	{
		UserRenderingQualityProfiles[GetUserRenderingQualityProfileIndex(InIndex)] = InProfile;
	}
}

void UMetaHumanCharacterEditorSettings::AddUserRenderingQualityProfile(const FMetaHumanCharacterRenderingQualityProfile& InProfile)
{
	UserRenderingQualityProfiles.Add(InProfile);
}

bool UMetaHumanCharacterEditorSettings::RemoveUserRenderingQualityProfile(const int32 InIndex)
{
	if (!UserRenderingQualityProfiles.IsValidIndex(InIndex))
	{
		return false;
	}

	UserRenderingQualityProfiles.RemoveAt(InIndex);
	return true;
}

int32 UMetaHumanCharacterEditorSettings::GetUserRenderingQualityProfileIndex(const int32 InCombinedIndex) const
{
	const int32 UserIndex = InCombinedIndex - DefaultRenderingQualityProfiles.Num();
	return UserIndex >= 0 ? UserIndex : INDEX_NONE;
}

bool UMetaHumanCharacterEditorSettings::IsValidRenderingQualityProfileIndex(const int32 InIndex) const
{
	return InIndex >= 0 && InIndex < GetAllRenderingQualityProfilesNum();
}

void UMetaHumanCharacterEditorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	const FName PropertyName = InPropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = InPropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MigratedPackagePath))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedPackagePath.Path, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MigratedNamePrefix))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedNamePrefix, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MigratedNameSuffix))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedNameSuffix, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bEnableExperimentalWorkflows))
	{
		OnExperimentalAssemblyOptionsStateChanged.ExecuteIfBound();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, WardrobePaths)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, WardrobePaths))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedPackagePath.Path, INVALID_LONGPACKAGE_CHARACTERS);
		OnWardrobePathsChanged.Broadcast();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, PresetsDirectories)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, PresetsDirectories))
	{
		for (FDirectoryPath& PresetDirectory : PresetsDirectories)
		{
			ObjectTools::SanitizeInvalidCharsInline(PresetDirectory.Path, INVALID_LONGPACKAGE_CHARACTERS);
		}

		OnPresetsDirectoriesChanged.ExecuteIfBound();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, CustomLightPresets)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, CustomLightPresets))
	{
		TMap<FString, TSoftObjectPtr<UWorld>> UpdatedCustomLightPresets;
		for (auto It = CustomLightPresets.CreateIterator(); It; ++It)
		{
			const bool bIsEmptyKey = !It->Value.IsNull() && It->Key.TrimStartAndEnd().IsEmpty();
			const bool bIsReservedKey = IsReservedLightingEnvironmentName(It->Key);
			if (bIsEmptyKey || bIsReservedKey)
			{
				const FString UniqueKey = MakeUniqueKey(It->Value.GetAssetName(), [this, &UpdatedCustomLightPresets](const FString& Key)
				{
					return CustomLightPresets.Contains(Key) || UpdatedCustomLightPresets.Contains(Key) || IsReservedLightingEnvironmentName(Key);
				});
				UpdatedCustomLightPresets.Add(UniqueKey, MoveTemp(It->Value));

				if (bIsReservedKey)
				{
					ShowInvalidOperationError(FText::Format(LOCTEXT("ReservedLightingEnvName", "\"{0}\" is reserved for a default lighting environment and cannot be used as a Custom Light Preset name"), FText::FromString(It->Key)));
				}
			}
			else
			{
				UpdatedCustomLightPresets.Add(It->Key, MoveTemp(It->Value));
			}
		}

		CustomLightPresets = MoveTemp(UpdatedCustomLightPresets);
		OnCustomLightPresetsChanged.Broadcast();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bUseVirtualTextures))
	{
		OnUseVirtualTexturesChanged.Broadcast();
	}
}

void UMetaHumanCharacterEditorSettings::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);

	if (InTransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		TArray<FName> PropertiesChanged = InTransactionEvent.GetChangedProperties();

		if (PropertiesChanged.Contains(GET_MEMBER_NAME_CHECKED(ThisClass, WardrobePaths)))
		{
			OnWardrobePathsChanged.Broadcast();
		}
	}
}

bool UMetaHumanCharacterEditorSettings::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, bUseVirtualTextures))
	{
		static const auto* AllowStaticLightingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
		const bool bStaticLightingAllowed = AllowStaticLightingCVar && AllowStaticLightingCVar->GetValueOnGameThread() != 0;
		return UTexture::IsVirtualTexturingEnabled() && !bStaticLightingAllowed;
	}

	return Super::CanEditChange(InProperty);
}

FText UMetaHumanCharacterEditorSettings::GetSectionText() const
{
	return LOCTEXT("MetaHumanCharacterEditorSettingsName", "MetaHuman Character");
}

FText UMetaHumanCharacterEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("MetaHumanCharacterEditorSettingsDescription", "Configure the MetaHuman Character Editor plugin");
}

const bool UMetaHumanCharacterEditorSettings::ShouldUseVirtualTextures() const
{
	ITargetPlatformManagerModule& TargetPlatformManagerModule = GetTargetPlatformManagerRef();

	const TArray<ITargetPlatform*>& TargetPlatforms = TargetPlatformManagerModule.GetActiveTargetPlatforms();
	const bool bAllEnabledPlatformsSupportVTs = Algo::AllOf(TargetPlatforms, UTexture::IsVirtualTexturingEnabled);

	// Static lighting adds extra SRVs that push VT-enabled skin materials over the shader resource limit
	static const auto* AllowStaticLightingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bStaticLightingAllowed = AllowStaticLightingCVar && AllowStaticLightingCVar->GetValueOnGameThread() != 0;

	return bAllEnabledPlatformsSupportVTs && bUseVirtualTextures && !bStaticLightingAllowed;
}

FString UMetaHumanCharacterEditorSettings::MakeUniqueKey(const FString& InDesiredKey, TFunctionRef<bool(const FString&)> KeyExists)
{
	if (!KeyExists(InDesiredKey))
	{
		return InDesiredKey;
	}

	int32 Suffix = 1;
	FString UniqueKey;
	do
	{
		UniqueKey = FString::Printf(TEXT("%s_%d"), *InDesiredKey, Suffix++);
	}
	while (KeyExists(UniqueKey));

	return UniqueKey;
}

bool UMetaHumanCharacterEditorSettings::CustomLightPresetAssetsFilter(const FAssetData& AssetData)
{
	return AssetData.PackagePath.ToString().StartsWith(TEXT("/" UE_PLUGIN_NAME "/LightingEnvironments"));
}

void UMetaHumanCharacterEditorSettings::OnAssetRemoved(const FAssetData& InAssetData)
{
	if (InAssetData.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
	{
		return;
	}
	
	for (auto It= CustomLightPresets.CreateIterator(); It; ++It)
	{
		if (It->Value.ToSoftObjectPath() == InAssetData.ToSoftObjectPath())
		{
			It.RemoveCurrent();
			SaveConfig();
			OnCustomLightPresetsChanged.Broadcast();
			break;
		}
	}
}

void UMetaHumanCharacterEditorSettings::ShowInvalidOperationError(const FText& ErrorText)
{
	if (!InvalidOperationError.IsValid())
	{
		FNotificationInfo InvalidOperation(ErrorText);
		InvalidOperation.ExpireDuration = 3.0f;
		InvalidOperationError = FSlateNotificationManager::Get().AddNotification(InvalidOperation);
	}
}

bool UMetaHumanCharacterEditorSettings::IsReservedLightingEnvironmentName(const FString& InName)
{
	const UEnum* EnvEnum = StaticEnum<EMetaHumanCharacterEnvironment>();
	for (EMetaHumanCharacterEnvironment Value : TEnumRange<EMetaHumanCharacterEnvironment>())
	{
		if (InName.Equals(EnvEnum->GetAuthoredNameStringByValue(static_cast<uint8>(Value)), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

TArray<FName> UMetaHumanCharacterEditorSettings::GetDefaultLightingEnvironmentOptions() const
{
	TArray<FName> Options;

	// Add built-in environment names (TEnumRange already skips Custom, Default, and Count)
	const UEnum* EnvEnum = StaticEnum<EMetaHumanCharacterEnvironment>();
	for (EMetaHumanCharacterEnvironment Value : TEnumRange<EMetaHumanCharacterEnvironment>())
	{
		Options.Add(FName(*EnvEnum->GetAuthoredNameStringByValue(static_cast<uint8>(Value))));
	}

	// Add valid custom light preset keys
	for (const TPair<FString, TSoftObjectPtr<UWorld>>& Preset : CustomLightPresets)
	{
		if (!Preset.Value.IsNull())
		{
			Options.AddUnique(FName(*Preset.Key));
		}
	}

	return Options;
}

void UMetaHumanCharacterEditorSettings::ResolveDefaultLightingEnvironment(EMetaHumanCharacterEnvironment& OutEnvironment, FString& OutCustomPresetKey, TSoftObjectPtr<UWorld>& OutCustomPresetWorld) const
{
	OutCustomPresetKey.Reset();
	OutCustomPresetWorld.Reset();

	const FString NameStr = DefaultLightingEnvironment.ToString();

	// Try matching a built-in environment name (TEnumRange already skips Custom, Default, and Count)
	const UEnum* EnvEnum = StaticEnum<EMetaHumanCharacterEnvironment>();
	for (EMetaHumanCharacterEnvironment Value : TEnumRange<EMetaHumanCharacterEnvironment>())
	{
		if (NameStr.Equals(EnvEnum->GetAuthoredNameStringByValue(static_cast<uint8>(Value)), ESearchCase::IgnoreCase))
		{
			OutEnvironment = Value;
			return;
		}
	}

	// Try matching a custom light preset key. The path may still be serialized after the asset
	// has been deleted, so check the asset registry rather than just IsNull (which only tells us
	// the path is empty). This avoids loading the asset.
	const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	for (const TPair<FString, TSoftObjectPtr<UWorld>>& Preset : CustomLightPresets)
	{
		if (Preset.Key.Equals(NameStr, ESearchCase::IgnoreCase)
			&& !Preset.Value.IsNull()
			&& AssetRegistry.GetAssetByObjectPath(Preset.Value.ToSoftObjectPath()).IsValid())
		{
			OutEnvironment = EMetaHumanCharacterEnvironment::Custom;
			OutCustomPresetKey = Preset.Key;
			OutCustomPresetWorld = Preset.Value;
			return;
		}
	}

	// Fallback to Studio
	OutEnvironment = EMetaHumanCharacterEnvironment::Studio;
}

#undef LOCTEXT_NAMESPACE
