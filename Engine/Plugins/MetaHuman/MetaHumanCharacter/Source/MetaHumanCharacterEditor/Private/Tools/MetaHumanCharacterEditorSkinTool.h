// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorSubTools.h"
#include "SingleSelectionTool.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterIdentity.h"
#include "Containers/Ticker.h"

#include "MetaHumanCharacterEditorSkinTool.generated.h"

enum class EMetaHumanCharacterAccentRegion : uint8;
enum class EMetaHumanCharacterAccentRegionParameter : uint8;
enum class EMetaHumanCharacterFrecklesParameter : uint8;
class FMetaHumanFaceTextureAttributeMap;
class FMetaHumanFilteredFaceTextureIndices;
class UAnimSequence;

UCLASS()
class UMetaHumanCharacterEditorSkinToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

/**
 * Properties for the Skin tool.
 * These are displayed in the details panel of the Skin Tool and are how the user can edit skin parameters
 * and, for now, are the same as the ones stored in UMetaHumanCharacter
 */
UCLASS()
class UMetaHumanCharacterEditorSkinToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	//~End UObject Interface

	/**
	* Delegate that executes on EPropertyChangeType::ValueSet property change event, i.e. when a property
	* value has finished being updated
	*/
	DECLARE_DELEGATE_OneParam(FOnSkinPropertyValueSetDelegate, const FPropertyChangedEvent& PropertyChangedEvent);
	FOnSkinPropertyValueSetDelegate OnSkinPropertyValueSetDelegate;

	/** Fires after a transaction writes settings into the tool properties. */
	FSimpleMulticastDelegate OnSkinToolCommandChangeApplied;

	/**
	* Utility functions for copying to & from MetaHuman Character Skin Settings and Skin Tool Properties
	*/
	void CopyTo(FMetaHumanCharacterSkinSettings& OutSkinSettings);
	void CopyFrom(const FMetaHumanCharacterSkinSettings& InSkinSettings);

	/**
	* Utility functions for copying to & from MetaHuman Character Evaluation Properties
	*/
	void CopyTo(FMetaHumanCharacterFaceEvaluationSettings& OutFaceEvaluationSettings);
	void CopyFrom(const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings);

	/**
	 * Returns true when rig-restricted skin properties are editable (the character is not rigged
	 * or rig-pending). Rig-restricted properties are the ones that regenerate the synthesized
	 * skin texture in ways that affect the rig: face/body texture indices (including the
	 * texture-attribute filter controls that drive the face texture index) and the Texture
	 * Position Offset.
	 */
	bool CanEditRigRestrictedProperties() const;

	/**
	 * Returns true if InProperty is one of the rig-restricted skin properties. See
	 * CanEditRigRestrictedProperties for the list of restricted properties.
	 */
	static bool IsRigRestrictedProperty(const FProperty* InProperty);

public:

	UPROPERTY(EditAnywhere, Category = "Skin", meta = (ShowOnlyInnerProperties, TransientToolProperty))
	FMetaHumanCharacterSkinProperties Skin;

	UPROPERTY(EditAnywhere, DisplayName = "Use Texture Index Filters", Category = "Skin");
	bool bIsSkinFilterEnabled;

	UPROPERTY(EditAnywhere, Category = "Skin");
	TArray<int32> SkinFilterValues;

	UPROPERTY(EditAnywhere, DisplayName = "Face Filter Index", Category = "Skin");
	int32 SkinFilterIndex;

	UPROPERTY(EditAnywhere, Category = "FaceEvaluation", meta = (ShowOnlyInnerProperties, TransientToolProperty))
	FMetaHumanCharacterFaceEvaluationSettings FaceEvaluationSettings;

	UPROPERTY(EditAnywhere, Category = "Freckles", meta = (ShowOnlyInnerProperties, TransientToolProperty))
	FMetaHumanCharacterFrecklesProperties Freckles;

	UPROPERTY(EditAnywhere, Category = "Accents", meta = (ShowOnlyInnerProperties, TransientToolProperty))
	FMetaHumanCharacterAccentRegions Accents;

	// The desired resolutions to use when requesting to download texture sources
	UPROPERTY(EditAnywhere, Category = "Textures Sources", AdvancedDisplay, meta = (TransientToolProperty))
	FMetaHumanCharacterTextureSourceResolutions DesiredTextureSourcesResolutions;

	/** When enabled, poses the character's hands with an open palm pose so the hand textures and materials can be inspected. The character must be rigged for the pose to take effect. */
	UPROPERTY(EditAnywhere, Category = "Skin")
	bool bShowHands = false;
};

/**
 * The Skin Tool allows the user to edit properties of the MetaHuman Skin
 */
UCLASS()
class UMetaHumanCharacterEditorSkinTool : public USingleSelectionTool
{
	GENERATED_BODY()

public:
	/** Get the Skin Tool properties. */
	UMetaHumanCharacterEditorSkinToolProperties* GetSkinToolProperties() const { return SkinToolProperties; }

	//~Begin USingleTargetWithSelectionTool interface
	virtual void Setup();
	virtual void Shutdown(EToolShutdownType InShutdownType);

	virtual bool HasCancel() const { return true; }
	virtual bool HasAccept() const { return true; }
	virtual bool CanAccept() const { return true; }
	//~End USingleTargetWithSelectionTool interface

	/** Returns true if the filter indices are valid. */
	bool IsFilteredFaceTextureIndicesValid() const;

	/** Enables or disables the show hands pose on the character. Does not move the camera. */
	void SetShowHandsPose(bool bShowHandsPose);

private:

	/**
	 * Handler for changes to UMetaHumanCharacterEditorSkinToolProperties::bShowHands. Toggles the
	 * show-hands pose, manages the camera-focus ticker, and restores the pre-pose camera frame.
	 */
	void OnShowHandsChanged(bool bInShowHands);

protected:

	void UpdateSkinState() const;

private:

	const FText GetCommandChangeDescription() const;

	/**
	 * Updates the Skin Texture. Called whenever one of the skin texture parameters changes
	 * Will prompt the user if the character currently has high resolution textures to avoid loss of data
	 * @return true if a change was applied to character and false otherwise.
	 */
	bool UpdateSkinSynthesizedTexture();

	void UpdateSkinToolProperties(TWeakObjectPtr<UInteractiveToolManager> InToolManager, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings);

	void UpdateFaceTextureFromFilterIndex();

	void SetEnableSkinFilter(bool bInEnableSkinFilter);

	/**
	 * Posts a warning to the mode toolkit when the character is rigged (face/body texture selection
	 * is locked) and clears it otherwise.
	 */
	void UpdateRiggedWarning();


private:

	friend class FMetaHumanCharacterEditorSkinToolCommandChange;

	/** Properties of the Skin Tool. These are displayed in the details panel when the tool is activated. */
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorSkinToolProperties> SkinToolProperties;

	/** Keep track of previously set skin settings */
	FMetaHumanCharacterSkinSettings PreviousSkinSettings;
	FMetaHumanCharacterFaceEvaluationSettings PreviousFaceEvaluationSettings;

	TSharedPtr<FMetaHumanFilteredFaceTextureIndices> FilteredFaceTextureIndices;

	/** Keep track of whether the tool applied any changes */
	bool bActorWasModified = false;
	bool bSkinTextureWasModified = false;

	/** The face state of the actor when the tool was activated */
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState;

	/** Delegate handle for the character's rigging state changed event. */
	FDelegateHandle RiggingStateChangedHandle;

	/** Delegate handle for re-applying the hands pose after the character package is saved. */
	FDelegateHandle PackageSavedHandle;

	/**
	 * Camera frame the character was focused on immediately before the show-hands pose was last enabled.
	 * Seeded at Setup with the frame the character had on tool activation, so a Shutdown that follows a
	 * show-hands activation can return the viewport to where the user started.
	 */
	EMetaHumanCharacterCameraFrame PreShowHandsCameraFrame = EMetaHumanCharacterCameraFrame::Auto;

	/** True if SetShowHandsPose(true) ran during the session. Triggers frame restore on Shutdown. */
	bool bShowHandsPoseActivated = false;

	/** Handle to the ticker that waits for the show-hands pose animation to finish before focusing the camera. */
	TOptional<FTSTicker::FDelegateHandle> ShowHandsPoseFocusTickerHandle;

	/**
	 * Face/body animations that were driving the character immediately before the show-hands pose
	 * was applied. Cached so toggling the pose off restores the previous animation instead of
	 * leaving the character with no animation.
	 */
	TWeakObjectPtr<UAnimSequence> PreShowHandsFaceAnim;
	TWeakObjectPtr<UAnimSequence> PreShowHandsBodyAnim;

	/** True while PreShowHandsFaceAnim/PreShowHandsBodyAnim hold a valid pre-pose snapshot. */
	bool bHasPreShowHandsAnim = false;
};