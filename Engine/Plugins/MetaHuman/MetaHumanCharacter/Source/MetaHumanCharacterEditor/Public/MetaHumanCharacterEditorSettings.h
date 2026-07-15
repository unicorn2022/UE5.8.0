// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/Scene.h"
#include "Delegates/Delegate.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorRenderingQualitySettings.h"
#include "Settings/EditorViewportSettings.h"

#include "MetaHumanCharacterEditorSettings.generated.h"

class UMaterialInstanceConstant;

UENUM()
enum class EMetaHumanCharacterMigrationAction : uint8
{
	// When adding a MetaHuman, prompt for the action to take
	Prompt,

	// Import the legacy MetaHuman to the project
	Import,

	// Migrate the MetaHuman to its new representation
	Migrate,

	// Performs both an import and migrate operations
	ImportAndMigrate,
};

/**
 * Settings for the MetaHuman Character Editor plug-in.
 */
UCLASS(BlueprintType, config = MetaHumanCharacter, defaultconfig)
class METAHUMANCHARACTEREDITOR_API UMetaHumanCharacterEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	/** Constructor */
	UMetaHumanCharacterEditorSettings();

	//~Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
	virtual void PostTransacted(const FTransactionObjectEvent& InTransactionEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	//~End UObject interface

	//~Begin UDeveloperSettings interface
	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("MetaHumanCharacter"); }

	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	//~End UDeveloperSettings interface

	/**
	 * @brief Whether or not we should use virtual textures in the MetaHuman Character Editor
	 */
	const bool ShouldUseVirtualTextures() const;

	/** Gets a reference to the OnPresetsDirectoriesChanged delegate */
	FSimpleDelegate& GetOnPresetsDirectoriesChanged() { return OnPresetsDirectoriesChanged; }
	
	/** Gets a reference to the OnThumbnailTypeChanged delegate */
	FSimpleMulticastDelegate& GetOnThumbnailTypeChanged() { return OnThumbnailTypeChanged; }

	static FString MakeUniqueKey(const FString& InDesiredKey, TFunctionRef<bool(const FString&)> KeyExists);

	void ShowInvalidOperationError(const FText& ErrorText);

	/** Returns true if the given name matches a built-in lighting environment name (e.g. Studio, Fireside, etc.) */
	static bool IsReservedLightingEnvironmentName(const FString& InName);

	/**
	 * Resolves DefaultLightingEnvironment into an environment enum and (when applicable) the matching custom preset.
	 * Falls back to Studio if the name is invalid or the matching preset is empty/null. Guarantees that when
	 * OutEnvironment == Custom, OutCustomPresetKey and OutCustomPresetWorld both refer to a valid (non-null) preset.
	 * @param OutEnvironment 		The resolved environment enum value
	 * @param OutCustomPresetKey 	If the resolved environment is Custom, the preset key; otherwise empty
	 * @param OutCustomPresetWorld 	If the resolved environment is Custom, the preset world asset; otherwise null
	 */
	void ResolveDefaultLightingEnvironment(EMetaHumanCharacterEnvironment& OutEnvironment, FString& OutCustomPresetKey, TSoftObjectPtr<UWorld>& OutCustomPresetWorld) const;

private:
	UFUNCTION()
	bool CustomLightPresetAssetsFilter(const FAssetData& AssetData);

	UFUNCTION()
	TArray<FName> GetDefaultLightingEnvironmentOptions() const;

	void OnAssetRemoved(const FAssetData& InAssetData);

	TWeakPtr<class SNotificationItem> InvalidOperationError;

	/** The delegate executed when the presets directory paths have been changed */
	FSimpleDelegate OnPresetsDirectoriesChanged;

	/** The delegate executed when the ThumbnailType has changed */
	FSimpleMulticastDelegate OnThumbnailTypeChanged;

	/** Default rendering quality profiles (Epic, High, Medium). Populated in PostInitProperties and never serialized */
	UPROPERTY(Transient)
	TArray<FMetaHumanCharacterRenderingQualityProfile> DefaultRenderingQualityProfiles;

	/** User-created rendering quality profiles. Serialized to config. */
	UPROPERTY(config, VisibleAnywhere, Category = "Lighting Environments")
	TArray<FMetaHumanCharacterRenderingQualityProfile> UserRenderingQualityProfiles;

public:

	UPROPERTY(Config, EditAnywhere, Category = "Face Editing|Texture Synthesis", DisplayName = "Texture Synthesis Model Directory")
	FDirectoryPath TextureSynthesisModelDir;

	UPROPERTY(Config, EditAnywhere, Category = "Face Editing|Texture Synthesis")
	int32 TextureSynthesisThreadCount = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Face Editing|Manipulators")
	TSoftObjectPtr<class UStaticMesh> SculptManipulatorMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Face Editing|Manipulators")
	TSoftObjectPtr<class UStaticMesh> MoveManipulatorMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Mesh Import|Manipulators")
	TSoftObjectPtr<class UStaticMesh> KeyPointCharacterManipulatorMesh;
	
	UPROPERTY(Config, EditAnywhere, Category = "Mesh Import|Manipulators")
	TSoftObjectPtr<class UStaticMesh> KeyPointTargetManipulatorMesh;

	UPROPERTY(Config, EditAnywhere, Category = "Fixed Body Types")
	bool bShowCompatibilityModeBodies = false;

	UPROPERTY(Config, EditAnywhere, Category = "Pipeline")
	bool bEnableExperimentalWorkflows = false;

	UPROPERTY(Config, EditAnywhere, Category = "Pipeline")
	TMap<FName, TSoftObjectPtr<UMaterialInstanceConstant>> ScalableNormalsTypeMaterials;

	UPROPERTY(Config, EditAnywhere, Category = "Animation")
	TArray<FSoftObjectPath> TemplateAnimationDataTableAssets;

	UPROPERTY(Config, EditAnywhere, Category = "Animation")
	TSoftObjectPtr<class UAnimSequence> ShowHandsPose;

	// Where MetaHuman Character presets are going to be searched
	UPROPERTY(Config, EditAnywhere, Category = "Presets", DisplayName = "Presets Directories", meta = (ContentDir))
	TArray<FDirectoryPath> PresetsDirectories;

	UPROPERTY(Config, EditAnywhere, Category = "Presets")
	EMetaHumanCharacterThumbnailCameraPosition PresetsThumbnailType = EMetaHumanCharacterThumbnailCameraPosition::Face;

	// What happens when adding a MetaHuman from Bridge
	UPROPERTY(Config, EditAnywhere, Category = "Migration")
	EMetaHumanCharacterMigrationAction MigrationAction = EMetaHumanCharacterMigrationAction::Prompt;

	// Where new MetaHuman Character assets are going to be placed
	UPROPERTY(Config, EditAnywhere, Category = "Migration", meta = (ContentDir))
	FDirectoryPath MigratedPackagePath;

	// Prefix to be added to the name of the migrated MetaHuman Character asset
	UPROPERTY(Config, EditAnywhere, Category = "Migration")
	FString MigratedNamePrefix;

	// Suffix to be added to the name of the migrated MetaHuman Character asset
	UPROPERTY(Config, EditAnywhere, Category = "Migration")
	FString MigratedNameSuffix;

	// Boost factor to apply when streaming textures in the MetaHumanCharacter asset editor. A higher boost value will stream higher resolution textures in the viewport
	UPROPERTY(Config, EditAnywhere, Category = "Viewport", meta = (ClampMin = "1", ClampMax = "5"))
	int32 TextureStreamingBoost = 5;

	/** The default lighting environment to use when opening a character. Can be any built-in environment name (Studio, Split, etc.) or a CustomLightPresets key */
	UPROPERTY(Config, EditAnywhere, Category = "Lighting Environments", meta = (GetOptions = "GetDefaultLightingEnvironmentOptions"))
	FName DefaultLightingEnvironment = "Studio";

	UPROPERTY(Config, BlueprintReadWrite, EditAnywhere, Category = "Lighting Environments", meta = (GetAssetFilter="CustomLightPresetAssetsFilter"))
	TMap<FString, TSoftObjectPtr<UWorld>> CustomLightPresets;

	/** When enabled, automatically opens the duplicated Custom Light Preset level in the Level Editor after creation */
	UPROPERTY(Config, EditAnywhere, Category = "Lighting Environments")
	bool bOpenCustomLightPresetInLevelEditor = true;

	/** When enabled, automatically spawns the MetaHuman preview actor when the Custom Light Preset level is opened */
	UPROPERTY(Config, EditAnywhere, Category = "Lighting Environments", meta = (EditCondition = "bOpenCustomLightPresetInLevelEditor"))
	bool bAutoSpawnPreviewActor = true;

	/** 
	 * When enabled, the MetaHuman lighting environments are validated and MetaHuman Message Log shows validation warnings when a lighting environment 
	 * uses World Partition or is missing the actors that drive the Light Rig Rotation and Background Color controls. Turn off to silence these messages.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lighting Environments")
	bool bValidateLightingEnvironments = true;

	UPROPERTY(Config)
	int32 CameraSpeed_DEPRECATED = 2;

	UPROPERTY(Config, EditAnywhere, Category = "Camera Options")
	FEditorViewportCameraSpeedSettings CameraSpeedSettings;

	UPROPERTY(Config, EditAnywhere, Category = "Camera Options", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MouseSensitivityModifier = 0.4f;

	/** User defined wardrobe paths */
	UPROPERTY(config, EditAnywhere, Category = "Wardrobe", meta=(DisplayName="New Wardrobe Default Asset Paths"))
	TArray<FMetaHumanCharacterAssetsSection> WardrobePaths;

	// Whether or not to run validation of wardrobe items. Disabling this will allow potentially incompatible items to be applied
	// to a MetaHuman Character
	UPROPERTY(config, EditAnywhere, Category = "Wardrobe|Validation")
	bool bEnableWardrobeItemValidation = true;

	// If enabled, prevents the Message Log from automatically opening even if there are warnings or errors.
	UPROPERTY(config, EditAnywhere, Category = "Wardrobe|Validation")
	bool bSuppressMessageLogWhenValidatingItems = false;

	/** Returns all rendering quality profiles, with defaults first followed by user profiles */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	TArray<FMetaHumanCharacterRenderingQualityProfile> GetAllRenderingQualityProfiles() const;

	UFUNCTION(BlueprintCallable, Category = "Rendering")
	int32 GetAllRenderingQualityProfilesNum() const;

	/** Returns the rendering quality profile at the given combined index */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	FMetaHumanCharacterRenderingQualityProfile GetRenderingQualityProfile(const int32 InIndex) const;

	/** Sets the rendering quality profile at the given combined index, routing to default or user array based on bMetaHumanCharacterDefault */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	void SetRenderingQualityProfile(const int32 InIndex, const FMetaHumanCharacterRenderingQualityProfile& InProfile);

	/** Adds a new user rendering quality profile */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	void AddUserRenderingQualityProfile(const FMetaHumanCharacterRenderingQualityProfile& InProfile);

	/** Removes the user rendering quality profile at the given index. Returns false if index is invalid */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	bool RemoveUserRenderingQualityProfile(const int32 InIndex);

	/** Converts a combined profile index to a user profile index. Returns INDEX_NONE if the index falls within the default profiles range */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	int32 GetUserRenderingQualityProfileIndex(const int32 InCombinedIndex) const;

	/** Returns true if the given combined index is valid across both default and user profiles */
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	bool IsValidRenderingQualityProfileIndex(const int32 InIndex) const;

	// Controls which type of materials are created when assembling a MetaHuman Character.
	// When enabled, the assembled character will use materials that support Virtual Textures.
	// This option is ignored if Virtual Textures are disabled in the Project Settings,
	// or if Allow Static Lighting is enabled (r.AllowStaticLighting), as MetaHuman materials that have
	// Virtual Texture support enabled will fail to compile if static lighting is enabled for the project.
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	bool bUseVirtualTextures = true;

	/** Triggers when we change the wardrobe paths */
	DECLARE_MULTICAST_DELEGATE(FOnWardrobePathsChanged);
	FOnWardrobePathsChanged OnWardrobePathsChanged;

	/** The delegate executed when the experimental assembly options enable state has changed */
	FSimpleDelegate OnExperimentalAssemblyOptionsStateChanged;

	/** The delegate executed when the CustomLightPresets has changed */
	FSimpleMulticastDelegate OnCustomLightPresetsChanged;

	/** The delegate executed when bUseVirtualTextures has changed */
	FSimpleMulticastDelegate OnUseVirtualTexturesChanged;
};
