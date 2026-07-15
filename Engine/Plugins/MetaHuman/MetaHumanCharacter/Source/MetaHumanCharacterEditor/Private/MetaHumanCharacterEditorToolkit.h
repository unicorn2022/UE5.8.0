// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/BaseAssetToolkit.h"

class SMetaHumanCharacterEditorRenderingQualityView;
enum class EMetaHumanCharacterEnvironment : uint8;
enum class EMetaHumanQualityLevel : uint8;
enum class ERequestTextureResolution : int32;
enum class EMetaHumanRigType : uint8;

class FMetaHumanCharacterEditorToolkit : public FBaseAssetToolkit
{
public:

	FMetaHumanCharacterEditorToolkit(class UMetaHumanCharacterAssetEditor* InOwningAssetEditor);
	virtual ~FMetaHumanCharacterEditorToolkit();

	//~Begin FBaseAssetToolkit interface
	virtual FName GetToolkitFName() const;
	virtual FText GetBaseToolkitName() const;
	virtual void CreateEditorModeManager() override;
	virtual void SaveAsset_Execute() override;
	virtual void InitToolMenuContext(struct FToolMenuContext& InMenuContext) override;
	virtual void OnClose() override;
	//~End FBaseAssetToolkit interface

protected:

	//~Begin FBaseAssetToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual AssetEditorViewportFactoryFunction GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;
	virtual void PostInitAssetEditor() override;
	virtual void OnToolkitHostingStarted(const TSharedRef<IToolkit>& InToolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<IToolkit>& InToolkit) override;
	//~End FBaseAssetToolkit interface

private:

	/**/
	static const FName MetaHumanCharacterPreviewTabID;
	static const FName MetaHumanCharcterAnimationPanelID;

	/** Utility to get the MetaHumanCharacterEditorMode */
	TNotNull<class UMetaHumanCharacterEditorMode*> GetMetaHumanCharacterEditorMode() const;

	/** Returns false if there is a current active tool */
	bool HasActiveTool() const;

	/** Returns the label for the Download Texture Sources  */
	FText GetDownloadTextureSourcesLabel() const;

	/** Returns the tooltip to display for the Download Texture Sources command */
	FText GetDownloadTextureSourcesTooltip() const;

	/** Returns true if the Character can request texture sources from the service */
	bool CanRequestTextureSources() const;

	/** Start a service request to downloaded texture sources */
	void RequestTextureSources();
	
	/** Returns true if it's safe to call AutoRigFace */
	bool CanAutoRigFace() const;

	/** Function which triggers the call to AutoRigService */
	void AutoRigFace(EMetaHumanRigType InRigType);

	/** Returns true if the is a rig to remove */
	bool CanRemoveFaceRig() const;

	/** Function which triggers removal of the face rig */
	void RemoveFaceRig();

	/** Generic function for loading a level as streaming, optionally at a specific transform */
	class ULevelStreaming* LoadLevelInWorld(const FSoftObjectPath& LevelPath, const FTransform& LevelTransform = FTransform::Identity);

	/** Recursively stream LevelInstance sublevels found in the streamed level */
	void LoadLevelInstanceSubLevelsInWorldRecursive(const TNotNull<ULevel*> InLoadedLevel);
	void LoadLevelInstanceSubLevelsInWorldRecursive(const TNotNull<ULevel*> InLoadedLevel, const FTransform& InParentTransform, TArray<AActor*>& OutActorsToDestroy);

	/**This function is called on PostInitEditor when we need to load all of the lighting scenarios that we use in viewport*/
	void LoadLightingScenariosInWorld(const TArray<FSoftObjectPath>& LevelPaths);

	/**This function is called on PostInitEditor when we need to load all of the PostProcess scenarios that we use in viewport*/
	void LoadPostProcessScenariosInWorld(const FSoftObjectPath& BaseLevelPath);

	void LoadCustomLightingEnvironment(const FSoftObjectPath& EnvironmentPath);

	void UnloadCustomLightingEnvironment();

	/** Ensures that a valid lighting environment is selected if selected CustomLightPreset is updated or became invalid
	 * @return True if environment needs update, else false
	 */
	bool EnsureValidLightingEnvIsSelected() const;

	/**Changes lighting environment by streaming lighting scenario in the world*/
	UE_DEPRECATED(5.7, "Deprecated! Please use OnEnvironmentChanged.")
	void OnLightingStudioEnvironmentChanged(const EMetaHumanCharacterEnvironment NewStudioEnvironment);

	UE_DEPRECATED(5.7, "Deprecated! Please use OnEnvironmentChanged.")
	/** Called when PostProcess volume option changes*/
	void OnTonemapperEnvironmentChanged(const bool InTonemapperEnabled);

	/**Changes lighting environment by streaming lighting scenario in the world*/
	void OnEnvironmentChanged();

	/** Applies a rotation to the current light rig's parent actor, if one exists. */
	void SetLightRigParentRotation(const FRotator& InRotation);

	/** Called when the light rotation changes so the world can be updated */
	void OnLightRotationChanged(float InRotation);

	/** Scans the preview world for actors implementing IMetaHumanCharacterEnvironmentLightRig and IMetaHumanCharacterEnvironmentBackground, pushes the results into the viewport client, and emits Message Log warnings for each missing capability. */
	void RefreshEnvironmentCapabilityState();

	/** Logs a Message Log warning if the loaded custom environment world uses World Partition. */
	void WarnIfWorldPartitionEnvironment(const FSoftObjectPath& InEnvironmentPath, const class UWorld* InLoadedWorld) const;

	/** Called when the background color changes */
	void OnBackgroundColorChanged(const FLinearColor& InBackgroundColor);

	void OnRenderingQualityProfileUpdate(const int32 InActiveProfileIndex);

	/** Re-runs the preview build pipeline to regenerate the content used by the preview actor */
	void RefreshPreview();

	/** Extend the editor's toolbar with custom entries */
	void ExtendToolbar();

	/** Extend the editor's main menu with custom entries */
	void ExtendMenu();

	void BindCommands();

private:
	// Functions to assist debugging

	bool CanSaveStates() const;					// Returns true if the character has body or face state
	bool CanSaveTextures() const;				// Returns true if the character has synthesized textures
	bool CanSaveEyePreset() const;

	void SaveFaceState();			// Save the identity state of the face to a file
	void SaveFaceStateToDNA();		// Save the face state as part of a DNA, uses either the MH asset DNA or the preview one
	void DumpFaceStateDataForAR();	// Save the face state debug data to a folder
	void SaveBodyState();			// Save the identity state of the body to a file
	void SaveFaceTextures();		// Save all synthesized textures of the edited character as images files
	void SaveEyePreset();			// Save the current eye as a preset
	void TakeHighResScreenshot();	// Take a high resolution screenshot of the asset editor

private:

	// The preview scene displayed in the viewport of the asset editor. It holds the world and all the components that operate on the world
	// The advanced preview scene is used because of the post processing settings needed in editor
	TUniquePtr<class FPreviewScene> PreviewScene;

	// The actor spawned in the world of the preview scene. It is used to hold any components required to render the MetaHuman in the preview world
	TScriptInterface<class IMetaHumanCharacterEditorActorInterface> PreviewActor;

	// Handles the hosting of mode toolkits. Builds the UI from the toolkit being hosted
	TSharedPtr<class FMetaHumanCharacterEditorModeUILayer> ModeUILayer;

	// This is set in ModeUILayer to be the menu category where new tabs are registered to be enabled by the user
	TSharedPtr<FWorkspaceItem> MetaHumanCharacterEditorMenuCategory;

	/** Description where all of the preview scene details specific to the MetaHuman Character are stored */
	TStrongObjectPtr<class UMetaHumanCharacterEditorPreviewSceneDescription> PreviewSceneDescription;

	/** Base and Tonemapper post process levels*/
	TArray<class ULevelStreaming*> PostProcessLevels;

	/** Rotation of the light rig's parent actor, captured before an environment swap so it can be restored on the new level's rig. */
	TOptional<FRotator> CurrentLightRigParentRotation;

	// The preview settings widget where different options for preview can be adjusted and the tab where it's docked
	TSharedPtr<SWidget> PreviewSettingsWidget;

	TSharedPtr<SMetaHumanCharacterEditorRenderingQualityView> RenderingQualityWidget;

	TSharedRef<SDockTab> SpawnTab_AnimationBar(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnTab_PreviewSceneDetails(const FSpawnTabArgs& Args);

	void InitPreviewSceneDetails();

	class UMetaHumanCharacterEditorPreviewSceneDescription* GetPreviewSceneDescription();

	/** Protected levels that should never be unloaded during unloading of custom lighting environment */
	TSet<FSoftObjectPath> ResidentLevelPaths;
	TArray<TWeakObjectPtr<ULevelStreaming>> SelectedCustomLightingEnvStreamingLevels;
};