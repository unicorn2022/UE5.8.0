// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "Misc/EnumRange.h"
#include "Misc/NotNull.h"
#include "TickableEditorObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "UObject/ObjectKey.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakInterfacePtr.h"
#include "SkelMeshDNAUtils.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanFaceTextureSynthesizer.h"
#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanInvisibleDrivingActor.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "Subsystem/MetaHumanCharacterMeshImportContext.h"
#include "Subsystem/MetaHumanCharacterService.h"
#include "Subsystem/MetaHumanCharacterSkinMaterials.h"
#include "Misc/Change.h"

#include "MetaHumanCharacterEditorSubsystem.generated.h"


struct FMetaHumanCharacterGeneratedAssets;
struct FMetaHumanCharacterGeneratedAssetOptions;
struct FMetaHumanRigEvaluatedState;
enum class EMetaHumanServiceRequestResult;
enum class EMetaHumanClothingVisibilityState : uint8;
class FMetaHumanFaceTextureAttributeMap;

UENUM(BlueprintType)
enum class EImportErrorCode : uint8
{
	FittingError,
	InvalidInputData,
	InvalidInputBones,
	InvalidHeadMesh,
	InvalidLeftEyeMesh,
	InvalidRightEyeMesh,
	InvalidTeethMesh,
	NoHeadMeshPresent,
	NoEyeMeshesPresent,
	NoTeethMeshPresent,
	IdentityNotConformed,
	GeneralError,
	CombinedBodyCannotBeImportedAsWholeRig,
	Success
};

class FRemoveRigCommandChange : public FCommandChange
{
public:

	FRemoveRigCommandChange(
		const TArray<uint8>& InOldDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldState,
		const TArray<uint8>& InOldBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TNotNull<UMetaHumanCharacter*> InCharacter);

	//~Begin FCommandChange interface
	virtual FString ToString() const override
	{
		return FString(TEXT("Remove Rig"));
	}

	virtual void Apply(UObject* InObject) override
	{
		ApplyChange(InObject, NewDNABuffer, NewState, NewBodyDNABuffer, NewBodyState);
	}

	virtual void Revert(UObject* InObject) override
	{
		ApplyChange(InObject, OldDNABuffer, OldState, OldBodyDNABuffer, OldBodyState);
	}

	virtual bool HasExpired(UObject* InObject) const override;

	//~End FCommandChange interface	

protected:

	void ApplyChange(UObject* InObject,
		const TArray<uint8>& InDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InState,
		const TArray<uint8>& InBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState);

	TArray<uint8> OldDNABuffer;
	TArray<uint8> NewDNABuffer;

	TArray<uint8> OldBodyDNABuffer;
	TArray<uint8> NewBodyDNABuffer;

	TSharedRef<const FMetaHumanCharacterIdentity::FState> OldState;
	TSharedRef<const FMetaHumanCharacterIdentity::FState> NewState;

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> OldBodyState;
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> NewBodyState;

	bool bFixedBodyType = false;
};

// a specialization of the above with identical functionality but a different name so it appears correctly in the undo stack
class FAutoRigCommandChange : public FRemoveRigCommandChange
{
public:

	FAutoRigCommandChange(
		const TArray<uint8>& InOldDNABuffer,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldState,
		const TArray<uint8>& InOldBodyDNABuffer,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
		TNotNull<UMetaHumanCharacter*> InCharacter);

	//~Begin FCommandChange interface
	virtual FString ToString() const override
	{
		return FString(TEXT("Apply Auto-rig"));
	}
};

// TODO: Merge this with the enum defined in MetaHumanARServiceRequest.h
UENUM()
enum class EMetaHumanRigType : uint8
{
	JointsOnly,
	JointsAndBlendShapes
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTextureRequestParams
{
	GENERATED_BODY()

	// Weather or not to report the progress of the request in the form of notification items
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Synthesis")
	bool bReportProgress = true;

	// Set to true to make it a blocking request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Synthesis")
	bool bBlocking = false;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAutoRiggingRequestParams
{
	GENERATED_BODY()
	
	// The type of rig to request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto-Rigging")
	EMetaHumanRigType RigType = EMetaHumanRigType::JointsOnly;

	// Weather or not to report the progress of the request in the form of notification items
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto-Rigging")
	bool bReportProgress = true;

	// Set to true to make it a blocking request
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto-Rigging")
	bool bBlocking = false;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterFitToVerticesParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	FFitToTargetOptions Options;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> HeadVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> LeftEyeVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> RightEyeVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conforming")
	TArray<FVector> TeethVertices;
};

namespace UE::MetaHuman
{
	enum class ERigType;
}

DECLARE_DELEGATE(FOnNotifyLightingEnvironmentChanged)
DECLARE_DELEGATE_OneParam(FOnStudioLightRotationChanged, float InRotation);
DECLARE_DELEGATE_OneParam(FOnStudioBackgroundColorChanged, const FLinearColor& InBackgroundColor)
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsyncMeshConformIteration, const FMetaHumanRigEvaluatedState& /*BodyVerticesAndNormals*/, const FMetaHumanRigEvaluatedState& /*FaceVerticesAndNormals*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsyncMeshConformCompleted, bool bSuccess, bool bWasCancelled);
DECLARE_DELEGATE_TwoParams(FOnCameraFocusRequested, UMetaHumanCharacter* /*Character*/, EMetaHumanCharacterCameraFrame /*FrameToFocus*/);
DECLARE_DELEGATE_OneParam(FOnViewportToolbarRenderingQualityProfileChange, int32);

/**
 * Helper struct used to hold a data needed for each character being edited
 */
USTRUCT()
struct FMetaHumanCharacterEditorData
{
	GENERATED_BODY()

	FMetaHumanCharacterEditorData(
		TNotNull<USkeletalMesh*> InFaceMesh,
		TNotNull<USkeletalMesh*> InBodyMesh,
		TSharedRef<class FDNAToSkelMeshMap> InFaceDnaToSkelMeshMap,
		TSharedRef<class FDNAToSkelMeshMap> InBodyDnaToSkelMeshMap,
		TSharedRef<FMetaHumanCharacterIdentity::FState> InFaceState,
		TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InBodyState)
	: FaceMesh(InFaceMesh)
	, BodyMesh(InBodyMesh)
	, FaceDnaToSkelMeshMap(InFaceDnaToSkelMeshMap)
	, BodyDnaToSkelMeshMap(InBodyDnaToSkelMeshMap)
	, FaceState(InFaceState)
	, BodyState(InBodyState)
	{
	}

	// DO NOT USE.
	// For Unreal internals only. Default-constructed instances are not considered valid.
	FMetaHumanCharacterEditorData();

	// List of editor actors for a particular characters
	TArray<TWeakInterfacePtr<class IMetaHumanCharacterEditorActorInterface>> CharacterActorList;

	// Image objects used as temp storage for the texture synthesis output
	TMap<EFaceTextureType, FImage> CachedSynthesizedImages;
	
	// Temporary storage for HF albedo maps returned by the service, used for local texture synthesis
	TStaticArray<TArray<uint8>, 4> CachedHFAlbedoMaps;

	// Maps of futures used to do async loading of texture data
	TSortedMap<EFaceTextureType, TSharedFuture<FSharedBuffer>> SynthesizedFaceTexturesFutures;
	TSortedMap<EBodyTextureType, TSharedFuture<FSharedBuffer>> HighResBodyTexturesFutures;

	// Whether or not virtual textures are being used in the Face Material
	UPROPERTY()
	bool bFaceVirtualTextures = false;

	// Whether or not virtual texture are being used in the Body Material
	UPROPERTY()
	bool bBodyVirtualTextures = false;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	/** Invisible actor driving the preview actor. */
	UPROPERTY()
	TObjectPtr<class AMetaHumanInvisibleDrivingActor> InvisibleDrivingActor;

	// All members of this will be UMaterialInstanceDynamics, so it's safe to cast them
	UPROPERTY()
	FMetaHumanCharacterFaceMaterialSet HeadMaterials;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMap;
	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;

	// Temporary textures used to combine multiple hidden face maps into one for the character preview
	UPROPERTY()
	TObjectPtr<UTexture2D> TempCombinedHeadHiddenFaceMap;
	UPROPERTY()
	TObjectPtr<UTexture2D> TempCombinedBodyHiddenFaceMap;

	// Mirrors the Character's internal Collection. Used to build the character shown in the preview viewport.
	UPROPERTY()
	TObjectPtr<UMetaHumanCollection> PreviewCollection;

	UPROPERTY()
	bool bClothingVisible = true;

	// The latest skin settings to be used for generating textures and setting material parameters
	TOptional<FMetaHumanCharacterSkinSettings> SkinSettings;

	// The latest eyes settings to be used when updating the preview materials
	TOptional<FMetaHumanCharacterEyesSettings> EyesSettings;

	// The latest makeup settings to be used when updating the preview materials
	TOptional<FMetaHumanCharacterMakeupSettings> MakeupSettings;

	// The latest face evaluation settings which include vertex delta scale
	TOptional<FMetaHumanCharacterFaceEvaluationSettings> FaceEvaluationSettings;

	// The latest head model settings which include eyelashes parameters and variants.
	TOptional<FMetaHumanCharacterHeadModelSettings> HeadModelSettings;

	// Reference to the mapping between face DNA and Face Skeletal Mesh
	TSharedRef<class FDNAToSkelMeshMap> FaceDnaToSkelMeshMap;

	// Reference to the mapping between body DNA and Body Skeletal Mesh
	TSharedRef<class FDNAToSkelMeshMap> BodyDnaToSkelMeshMap;

	// Reference to the character identity creator
	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState;

	// Reference to the character body identity creator
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState;
	
	/* Sets if a pose other than MetaHuman A pose should be evaluated from the body state. Used during the import process. */ 
	UPROPERTY()
	bool bEvaluateBodyPose = false;

	/* Override transforms for drawing debug bones */ 
	UPROPERTY()
	TArray<FTransform> OverrideDebugBodyBoneTransforms;

	/** 
	 * Merged head and body mesh used for fast outfit resizing while editing the face or body.
	 * 
	 * Contains only LOD 0. May have other limitations.
	 */
	UPROPERTY()
	TObjectPtr<USkeletalMesh> PreviewMergedHeadAndBody;

	// Info about how the separate face and body mesh verts correspond to the verts in the merged mesh.
	UE::MetaHuman::FMergedMeshMapping PreviewMergedMeshMapping;

	// Delegate called when the Face State changes
	FSimpleMulticastDelegate OnFaceStateChangedDelegate;

	// Delegate called when the Body State changes
	FSimpleMulticastDelegate OnBodyStateChangedDelegate;

	// Delegate called when the target-mesh keypoints collection for this character is modified
	FSimpleMulticastDelegate OnTargetMeshKeyPointsChangedDelegate;
	
	// Delegate called on async mesh iteration
	FOnAsyncMeshConformIteration OnAsyncMeshConformIterationDelegate;
	
	// Delegate called when async mesh conform finishes
	FOnAsyncMeshConformCompleted OnAsyncMeshConformCompletedDelegate;

	// Delegate used for Environment Lighting studio update.
	FOnNotifyLightingEnvironmentChanged NotifyLightingEnvironmentChangedDelegate;
	FOnStudioLightRotationChanged EnvironmentLightRotationChangedDelegate;
	FOnStudioBackgroundColorChanged EnvironmentBackgroundColorChangedDelegate;

	// Delegate used for preview material change used in Import Tool
	FSimpleMulticastDelegate OnPreviewMaterialChangedDelegate;

	//Delegate used for focusing camera on MH Character
	FOnCameraFocusRequested CameraFocusRequestedDelegate;

	// Delegate used for rendering quality update from viewport toolbar.
	FOnViewportToolbarRenderingQualityProfileChange OnViewportToolbarRenderingQualityProfileChange;
};

/** 
 * The set of assets needed for the preview build.
 * 
 * Importantly, these assets belong to the editor subsystem and must not be modified by the preview 
 * build.
 */
USTRUCT()
struct FMetaHumanCharacterPreviewAssets
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> MergedHeadAndBodyMesh;

	UPROPERTY()
	TMap<FString, float> BodyMeasurements;
};

struct FMetaHumanCharacterPreviewAssetOptions
{
	bool bGenerateMergedHeadAndBodyMesh = false;
	bool bGenerateBodyMeasurements = false;
};

USTRUCT(BlueprintType)
struct FImportFromIdentityParams
{
	GENERATED_BODY()
	
	// Set to true to use the eye meshes to fit when importing a MetaHuman Identity asset; if false, they are not used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Identity Options")
	bool bUseEyeMeshes = true;

	// Set to true to use the teeth mesh to fit when importing a MetaHuman Identity asset; if false, it is not used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Identity Options")
	bool bUseTeethMesh = true;

	// Set to true to use the metric scale of the Identity head when importing a MetaHuman Identity asset; if false, the Identity head will be scaled to MetaHuman size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Identity Options")
	bool bUseMetricScale = false;
};

USTRUCT(BlueprintType)
struct FImportFromDNAParams
{
	GENERATED_BODY()

	// Separates the head from the body, preventing the head being updated by any body changes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import DNA Options")
	bool bIsolateHeadFromBody = false;

	// When enabled, the entire rig is replaced from the DNA file (mesh, joints, skin weights and RBFs), resulting in a fixed, non-editable body type. If unchecked, the head DNA file will only be used for neck alignment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import DNA Options")
	bool bImportWholeRig = true;

	// Set the alignment options to use when importing a MetaHuman DNA head asset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import DNA Options", meta = (EditCondition = "!bImportWholeRig", EditConditionHides))
	EAlignmentOptions AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
};

USTRUCT()
struct FImportBodyFromDNAParams
{
	GENERATED_BODY()

	// When enabled, imports mesh, joints, RBF, and skin weights from the DNA file, resulting in a fixed, non-editable body type. Must be body only, using MetaHuman topology. Disabling this option allows for generating a parametric body type using mesh and, optionally, skeleton from DNA.
	UPROPERTY(EditAnywhere, Category = "Import Whole Rig")
	bool bImportWholeRig = false;

	// Set the conform options to use when conforming a MetaHuman DNA body asset
	UPROPERTY(EditAnywhere, Category = "Import DNA Options", meta = (ShowOnlyInnerProperties, EditCondition = "!bImportWholeRig"))
	FConformBodyParams ConformBodyParams;
};

USTRUCT(BlueprintType)
struct FImportFromTemplateParams
{
	GENERATED_BODY()

	// Separates the head from the body, preventing the head being updated by any body changes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bIsolateHeadFromBody = false;

	// Get corresponding vertices from template mesh by matching the template mesh's UVs to MH standard
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bMatchVerticesByUVs = true;

	// Set to true to use the eye meshes to fit when importing a SkelMesh; if false, they are not used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bUseEyeMeshes = true;

	// Set to true to use the teeth mesh to fit when importing a SkelMesh; if false, it is not used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	bool bUseTeethMesh = true;

	// Set the alignment options to use when importing a SkelMesh or Static Mesh head asset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import Template Options")
	EAlignmentOptions AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
};


struct FEditorDataForCharacterCreationParams
{
	/** A parameter to control if we should wait for any async tasks to complete */
	bool bBlockUntilComplete = false;

	/** A parameter to switch between Interchange import from DNA or content mesh duplication */
	bool bCreateMeshFromDNA = false;

	/** An outer package that should be used for created skeletal meshes */
	TNotNull<UObject*> OuterForGeneratedAssets = GetTransientPackage();

	/** The preview material type to be used */
	EMetaHumanCharacterSkinPreviewMaterial PreviewMaterial  = EMetaHumanCharacterSkinPreviewMaterial::Default;
};


/**
 * Subsystem used to interface with the UMetaHumanCharacter asset.
 * Any edits to a MetaHumanCharacter that may need to be exposed as an API
 * should be done as part of this class, as UFUNCTIONs declared here are automatically
 * exposed
 */
UCLASS(BlueprintType)
class METAHUMANCHARACTEREDITOR_API UMetaHumanCharacterEditorSubsystem
	: public UEditorSubsystem
	, public FTickableEditorObject
{
	GENERATED_BODY()

public:

	//~FTickableEditorObject interface
	virtual bool IsTickable() const override;
	virtual void Tick(float InDeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~End of FTickableEditorObject interface

public:
	//
	// Subsystem Initialization
	//
	//~Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~End USubsystem interface

	/**
	 * Utility for obtaining a pointer to the global instance of this subsystem in the editor.
	 */
	static UMetaHumanCharacterEditorSubsystem* Get();

	/**
	 * Registers an object to be edited. The first object registered will
	 * also load the Texture Synthesis model to make it to be used
	 * 
	 * Most functions taking a Character on this class require the Character to be registered for
	 * editing first.
	 * 
	 * Call RemoveObjectToEdit when done editing. If TryAddObjectToEdit returns false, the 
	 * Character is not registered, so there's no need to call RemoveObjectToEdit.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Initialization")
	[[nodiscard]] bool TryAddObjectToEdit(UMetaHumanCharacter* InCharacter);

	/** Returns true if the object is registered for editing */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MetaHuman|Initialization")
	bool IsObjectAddedForEditing(const UMetaHumanCharacter* InCharacter) const;

	/**
	 * Tells the subsystem that a character is no longer being edited.
	 * Unloads the texture synthesis model when the last object being
	 * edited is removed from the subsystem
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Initialization")
	void RemoveObjectToEdit(const UMetaHumanCharacter* InCharacter);

	/**
	* Clears all internal model data for Texture Synthesis and re-loads the model using the path in the settings
	*/
	void ResetTextureSynthesis();

	/**
	 * Returns the Collection used to build the Character's preview.
	 *
	 * If the Character is not being actively edited (i.e. TryAddObjectToEdit hasn't been called)
	 * this will return nullptr, otherwise it will always return a valid Collection.
	 *
	 * @param InCharacter	The character whose preview Collection is requested.
	 * @return				The preview Collection, or nullptr if InCharacter is null
	 *						or has not been added for editing.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline")
	UMetaHumanCollection* GetPreviewCollection(const UMetaHumanCharacter* InCharacter);

	/**
	 * Any code that modifies the preview collection for a Character must call this function
	 * afterwards, in order to propagate the edits back to the Character asset.
	 *
	 * @param InCharacter	The character whose preview Collection was edited.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline")
	void OnEditPreviewCollection(UMetaHumanCharacter* InCharacter);

	/** Runs the editor pipeline (Preview quality) for the given character. Use whenever changes are made that should be reflected in the preview */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline", meta = (ScriptName="AssembleForPreview"))
	void RunCharacterEditorPipelineForPreview(UMetaHumanCharacter* InCharacter);

	/** Gets a readonly view on the character editor data */
	const TSharedRef<FMetaHumanCharacterEditorData>* GetMetaHumanCharacterEditorData(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
private:
	/**
	 * Initializes the editing state for a Character without registering it.
	 *
	 * Textures are guaranteed to be created by this function, but not necessarily filled with
	 * correct image data yet unless bBlockUntilComplete is true.
	 *
	 * @param InCharacter The character to create the editor data for
	 * @param InParams The collection of input parameters that affect editor data creation
	 * @param OutSynthesizedFaceTextures will receive the set of textures for the Character.
	 * @param OutBodyTextures will receive the set of bodytextures for the Character.
	 * @param InFaceTextureSynthesizerLoadTask optional, task to track whether the FaceTextureSynthesizer has finished initialization
	 */
	TSharedPtr<FMetaHumanCharacterEditorData> CreateEditorDataForCharacter(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		const FEditorDataForCharacterCreationParams& InParams,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<class UTexture2D>>& OutBodyTextures,
		UE::Tasks::FTask InFaceTextureSynthesizerLoadTask = {});

	/**
	 * Updates character face by creating facial SkeletalMesh from DNA through DNA Interchange system and attaches it to CharacterData->FaceMesh.
	 *
	 * @param InGeneratedAssetsOuter Outer package for created facial skeletal mesh
	 * @param InDNAReader DNA to create skeletal mesh from
	 * @param OutCharacterData CharacterData to update face mesh for
	 * @return True if updated successfully, else false
	 */
	static bool UpdateCharacterFaceMeshFromDNA(TNotNull<UObject*> InGeneratedAssetsOuter, const TSharedPtr<IDNAReader>& InDNAReader, TSharedRef<FMetaHumanCharacterEditorData>& OutCharacterData);

	/**
	* Creates body SkeletalMesh from DNA through DNA Interchange system and attaches it to CharacterData->BodyMesh.
	*/
	static void UpdateCharacterBodyMeshFromDNA(TNotNull<UObject*> InGeneratedAssetsOuter, const TSharedPtr<IDNAReader>& InDNAReader, TSharedRef<FMetaHumanCharacterEditorData>& OutCharacterData);
	
	//* Setting up and returning the Face and Body states for the character */
	bool InitializeIdentityStateForFaceAndBody(TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedPtr<FMetaHumanCharacterIdentity::FState>& OutFaceState, TSharedPtr<FMetaHumanCharacterBodyIdentity::FState>& OutBodyState);

	/** Creates Face and Body mesh either by duplicating content browser assets or Interchange system from stored or loaded DNA data */
	static void GetFaceAndBodySkeletalMeshes(TNotNull<const UMetaHumanCharacter*> InCharacter, const FEditorDataForCharacterCreationParams& InParams, USkeletalMesh*& OutFaceMesh, USkeletalMesh*& OutBodyMesh);
	
	/** 
	 * Fills in textures with image data for any pending textures that are ready.
	 * 
	 * This should be called repeatedly while textures are pending.
	 */
	static void UpdatePendingSynthesizedTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures);

	static void UpdatePendingHighResBodyTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& OutBodyTextures);

	/** Block until all textures are filled with image data */
	static void WaitForSynthesizedTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter, 
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& OutBodyTextures);

	/** Updates thumbnail assets for the given character. */
	void SaveCharacterThumbnails(TNotNull<UMetaHumanCharacter*> InCharacter);

	/** Creates the MetaHumanCharacterEditorData generating the Face and Body SKMs as needed. */
	[[nodiscard]] TSharedPtr<FMetaHumanCharacterEditorData> CreateEditorDataSKMsFromCharacter(
		TNotNull<const UMetaHumanCharacter*> InCharacter, 
		const FEditorDataForCharacterCreationParams& InParams);

	/** Updates the Face and Body SKMs from the input Character data and state */
	void SetupEditorDataSKMsFromCharacter(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<FMetaHumanCharacterEditorData> CharacterData);
	
	/** Generate the texture objects from the input Character properties */
	void GenerateCharacterTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter, 
		TSharedRef<FMetaHumanCharacterEditorData> CharacterData,
		TMap<EFaceTextureType, TObjectPtr<class UTexture2D>>& OutSynthesizedFaceTextures,
		TMap<EBodyTextureType, TObjectPtr<class UTexture2D>>& OutBodyTextures);

public:
	//
	// Character and Actor Initialization
	//

	/**
	 * Initializes all properties from the given MetaHumanCharacter that require loading data from various sources
	 */
	void InitializeMetaHumanCharacter(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Spawns and initializes an actor implementing IMetaHumanCharacterEditorActorInterface in the given world.
	 * 
	 * The actor will have the all of its components initialized from the state stored in the MetaHumanCharacter Asset.
	 * 
	 * This function will try to spawn the actor specified by the selected MetaHuman Character Pipeline, but falls back
	 * to a default actor type if that fails, so it's guaranteed to return a valid actor.
	 */
	TScriptInterface<IMetaHumanCharacterEditorActorInterface> CreateMetaHumanCharacterEditorActor(TNotNull<UMetaHumanCharacter*> InCharacter, TNotNull<class UWorld*> InWorld);

	/**
	 * @brief Spawns a MetaHuman Editor Actor in the main editor level
	 * 
	 * The spawned actor will reflect any changes made to the character while its added to the subsystem
	 * @param bKeepTransient If true, the spawned actor retains its RF_Transient flag and will not persist in the level
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Actor")
	AActor* SpawnMetaHumanActor(UMetaHumanCharacter* InCharacter, bool bKeepTransient = false);

	/** 
	 * Gets the class of actor that will be spawned by CreateMetaHumanCharacterEditorActor if there
	 * are no errors.
	 * 
	 * If CreateMetaHumanCharacterEditorActor would fall back to spawning a default actor type, this 
	 * function will return false and OutActorClass will be set to null.
	 */
	[[nodiscard]] bool TryGetMetaHumanCharacterEditorActorClass(TNotNull<const UMetaHumanCharacter*> InCharacter, TSubclassOf<AActor>& OutActorClass, FText& OutFailureReason) const;

	/**
	 * Create invisible driving actor.
	 * The invisible driving actor is used to play preview animations on the archetype skeletal meshes for which our animations have been recorded for. This is needed for retargeting.
	 * We use the invisible driving actor to drive the pose in the right proportions and then retarget it onto the preview MetaHuman. This avoids artefacts from inline retargeting while
	 * we can leave the MH Blueprint like it is. Curves will be propagated over as well.
	 */
	void CreateMetaHumanInvisibleDrivingActor(TNotNull<const UMetaHumanCharacter*> InCharacter, TScriptInterface<IMetaHumanCharacterEditorActorInterface> InEditorActorInterface, TNotNull<class UWorld*> InWorld);

	/**
	 * Get the invisible driving actor given the character.
	 */
	TObjectPtr<class AMetaHumanInvisibleDrivingActor> GetInvisibleDrivingActor(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Removes all data that is not needed to be in the character to make it a preset.
	 * Removes any stored textures and rigs.
	 * The caller is responsible for making sure the character is not opened for edit, returns false if the conversion failed
	 */
	bool RemoveTexturesAndRigs(TNotNull<UMetaHumanCharacter*> InCharacter);

public:
	//
	// Build and Export
	//

	/**
	 * Generates assets, such as meshes and textures, so that other code systems can render the 
	 * Character.
	 * 
	 * All generated objects must have the provided InOuterForGeneratedAssets as their Outer, and
	 * be added to the Metadata array on OutGeneratedAssets. If InOuterForGeneratedAssets is
	 * nullptr, the Transient Package will be used as an Outer.
	 * 
	 * If asset generation fails, the function will return false and OutGeneratedAssets will be 
	 * empty. Some assets may have been generated but they will not be referenced from 
	 * OutGeneratedAssets.
	 */
	[[nodiscard]] bool TryGenerateCharacterAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		UObject* InOuterForGeneratedAssets,
		FMetaHumanCharacterGeneratedAssets& OutGeneratedAssets);

	[[nodiscard]] bool TryGenerateCharacterAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		UObject* InOuterForGeneratedAssets,
		const FMetaHumanCharacterGeneratedAssetOptions& InOptions,
		FMetaHumanCharacterGeneratedAssets& OutGeneratedAssets);

	/** 
	 * Fetches editor-owned assets needed for the preview build, such as the meshes being actively
	 * edited by the Character asset editor.
	 * 
	 * These assets are still owned by the editor and must NOT be modified by callers of this 
	 * function.
	 */
	[[nodiscard]] bool TryGetCharacterPreviewAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		FMetaHumanCharacterPreviewAssets& OutPreviewAssets);

	[[nodiscard]] bool TryGetCharacterPreviewAssets(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterPreviewAssetOptions& InOptions,
		FMetaHumanCharacterPreviewAssets& OutPreviewAssets);

	/**
	 * Checks if MetaHuman character is ready for building. If the character cannot be built,
	 * outputs the error message describing the reason why.
	 */
	bool CanBuildMetaHuman(TNotNull<const UMetaHumanCharacter*> InCharacter, FText& OutErrorMessage);

	/**
	 * @brief Checks if MetaHuman character is ready for assembly.
	 *
	 * If the character is not ready a message with the reason why 
	 * can be printed in the logs using bInLogError.
	 * 
	 * @param InCharacter The character to verify
	 * @param bInLogError Set to log an error message if the character can't be built
	 * @return true if the character can be built and false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Assembly")
	bool CanBuildMetaHuman(const UMetaHumanCharacter* InCharacter, bool bInLogError = false);

	/**
	 * @brief Assemble a MetaHuman Character.
	 * 
	 * Which type of assembly and configuration options can be set using InParams
	 * 
	 * @param InCharacter The character to be assembled. The character must be opened for edit or the build will fail
	 * @param InParams Parameter that determine which type of build to make
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Assembly")
	void BuildMetaHuman(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterEditorBuildParameters& InParams);

	/** Returns the material to apply to clothing when it should be translucent */
	class UMaterialInterface* GetTranslucentClothingMaterial() const;

	/* Sets the clothing visibility state on any character actor and optionally updates the body material with character data hidden face map */
	void SetClothingVisibilityState(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanClothingVisibilityState InState, bool bUpdateMaterialHiddenFaces);

	/* Gets the clothing visibility state on any character actor */
	EMetaHumanClothingVisibilityState GetClothingVisibilityState(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Returns true when the input Character has an outfit applied in the currently active edit sesssion
	 */
	bool IsCharacterOutfitSelected(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Returns the Face Archetype Mesh for the given template type
	 */
	static USkeletalMesh* GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType InTemplateType, UObject* OuterForGeneratedAssets = GetTransientPackageAsObject());

	/**
	 * Returns the Body Archetype Mesh for the given template type
	 */
	static USkeletalMesh* GetBodyArchetypeMesh(EMetaHumanCharacterTemplateType InTemplateType, UObject* OuterForGeneratedAssets = GetTransientPackageAsObject());

	/**
	 * Returns combined face and body mesh for the given character.
	 *
	 * The requirement is that the character has both face and body DNAs.
	 */
	USkeletalMesh* CreateCombinedFaceAndBodyMesh(TNotNull<const UMetaHumanCharacter*> InCharacter, const FString& InAssetPathAndName, const bool bOverwriteExisting = false);

public:
	//
	// Skin Material Editing
	//

	/**
	 * Returns if the subsystem is able synthesize textures
	 */
	bool IsTextureSynthesisEnabled() const;

	/**
	 * Gets the skin tone texture color as rgb value
	 */
	FLinearColor GetSkinTone(const FVector2f& UV) const;

	/**
	 * Get or create the skin tone texture suitable to be used in the skin tone picker UI
	 * The caller is responsible for keeping a reference to the returned texture or it may be GC'ed
	 */
	TWeakObjectPtr<class UTexture2D> GetOrCreateSkinToneTexture();

	/**
	 * Estimates the skin tone UI values from an sRGB colour.
	 * Note that the estimation will be done using the currently loaded texture synthesis model.
	 * @param InSkinTone is assumed to be in sRGB space
	 */
	FVector2f EstimateSkinTone(const FLinearColor& InSkinTone, const int HFIndex) const;

	/**
	 * Get the maximum value for the HF index the model supports
	 */
	int32 GetMaxHighFrequencyIndex() const;

	/**
	 * Updates the face evaluation settings (vertex deltas and vertex geometry delta) of all the actors associated with the given character.
	 */
	void ApplyFaceEvaluationSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings) const;

	/**
	 * Set all the face evaluation settings to the character and apply the changes to all the registered actors.
	 */
	void CommitFaceEvaluationSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings);

	/**
	 * Gets the texture attribute map associated with the face texture synthesizer.
	 */
	const FMetaHumanFaceTextureAttributeMap& GetFaceTextureAttributeMap() const;
	
	/**
	 * Updates the Head Model (Eyelashes) of all the actors associated with the given character.
	 * bIgnoreGrooms flag is used to skip groom toggling when the focus is only geometry and materials.
	 */
	void ApplyHeadModelSettings(
		TNotNull<UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterHeadModelSettings& InHeadModelSettings,
		bool bIgnoreGrooms = false);

	/**
	 * Set all the Head Model settings to the character and apply the changes to all the registered actors.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Model")
	void CommitHeadModelSettings(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterHeadModelSettings& InHeadModelSettings);

	/**
	 * Applies or removes eyelashes grooms according to properties.
	 */
	void ToggleEyelashesGrooms(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties);
	
	/**
	 * Updates the Skin material of all the actors associated with the given character.
	 */
	void ApplySkinSettings(
		TNotNull<UMetaHumanCharacter*> InCharacter,
		const FMetaHumanCharacterSkinSettings& InSkinSettings) const;

	/**
	 * Set all the skin settings to the character and apply the changes to all registered actors.
	 * This will synthesize textures if needed based on skin settings. This will also discard any
	 * high resolution textures currently stored in the Character
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Texture Synthesis")
	void CommitSkinSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterSkinSettings& InSkinSettings);

	
	UE_DEPRECATED(5.7, "Please use RequestTextureSources") 
	void RequestHighResolutionTextures(TNotNull<UMetaHumanCharacter*> InCharacter, ERequestTextureResolution InResolution);

	/**
	 * @brief Request high resolution textures for the given character.
	 *
	 * This function does nothing if there is already a pending request
	 *
	 * @param InCharacter The character to request the textures for
	 * @param InParams Parameters to control the request
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Texture Synthesis")
	void RequestTextureSources(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterTextureRequestParams& InParams = FMetaHumanCharacterTextureRequestParams());

	/**
	 * Returns true if there is pending request for high resolution textures
	 */
	bool IsRequestingHighResolutionTextures(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Update the currently active preview material for the character.
	 *
	 * @param bWritePersistentState  When true (default), the new preview material is written to UMetaHumanCharacter::PreviewMaterialType and the asset package
	 *                               is marked dirty. Pass false for transient/visualization-only.
	 */
	void UpdateCharacterPreviewMaterial(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterial, bool bWritePersistentState = true) const;

	/*
	* Update translucency on matcap material used in Import Tool, this won't work if character doesn't have matcap material applied
	*/
	void UpdateTranslucencyOnActor(TNotNull<UMetaHumanCharacter*> InCharacter, float InTranslucency);

	/*
	* Update color on matcap material used in Import Tool, this won't work if character doesn't have matcap material applied
	*/
	void UpdateMatcapMaterialColorOnActor(TNotNull<UMetaHumanCharacter*> InCharacter, FLinearColor InColor);

	/*
	* Set guide textures on matcap material used in Import Tool, this won't work if character doesn't have matcap material applied
	*/
	void SetGuideTexturesOnActor(TNotNull<UMetaHumanCharacter*> InCharacter, bool bShowGuides);

	/**
	* Compare the face textures for the two supplied characters
	* Does NOT require the characters to have been added for edit
	* Returns true if face textures are identical to within the specified tolerance for each channel of each pixel, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Testing")
	static bool CompareFaceTextures(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, int32 InPixelTolerance);


	/** Callback when downloading textures state changes in editor */
	DECLARE_MULTICAST_DELEGATE_OneParam(FMetaHumanOnDownloadingTexturesStateChanged, TNotNull<const UMetaHumanCharacter*>);
	FMetaHumanOnDownloadingTexturesStateChanged OnDownloadingTexturesStateChanged;

private:

	/**
	 * Stores the synthesized textures in the character asset to be serialized
	 */
	void StoreSynthesizedTextures(TNotNull<UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Update the preview material for the actors corresponding to the character data
	 */
	static void UpdateActorsSkinPreviewMaterial(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, const FMetaHumanCharacterSkinSettings* InSkinSettings = nullptr);

	/** 
	 * Updates the editing state of the Character with the given skin settings.
	 * 
	 * Compares the new skin settings to those in the Character Data to determine whether to
	 * re-synthesize textures, etc.
	 * 
	 * If bInForceUseExistingTextures is true, this function will assume the current textures are
	 * up to date and will not re-synthesize them even if the new skin settings don't match the 
	 * stored settings. 
	 * 
	 * bOutTexturesHaveBeenRegenerated will be set to true if textures were re-synthesized.
	 */
	void ApplySkinSettings(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		const FMetaHumanCharacterSkinSettings& InSkinSettings,
		bool bInForceUseExistingTextures,
		const FMetaHumanCharacterSkinTextureSet& InFinalSkinTextureSet,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InOutSynthesizedFaceTextures,
		bool& bOutTexturesHaveBeenRegenerated) const;

	/**
	 * Synthesizes textures and updates face state with high frequency data.
	 *
	 * This function doesn't compare the new state to the existing state, so only call it if the
	 * textures and HF data need updating.
	 *
	 * @param InCharacterData the character data struct to apply the settings to
	 * @param InSkinProperties parameters for the texture synthesis model
	 * @param InOutSynthesizedFaceTextures synthesized textures to be updated
	 * @param InOutBodyTextures the set of body textures to update the body material with
	 */
	void ApplySkinProperties(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		const FMetaHumanCharacterSkinProperties& InSkinProperties,
		TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InOutSynthesizedFaceTextures,
		const TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& InBodyTextures) const;

	/** Updates material parameters to set textures and skin tone. Needs to be called if new texture objects are being used. */
	void UpdateSkinTextures(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
							const FMetaHumanCharacterSkinProperties& InSkinProperties,
							const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const;

	/**
	 * Handles a high resolution texture response.
	* Stores the new textures in the character and update any live character actors
	*/
	void OnHighResolutionTexturesRequestCompleted(TSharedPtr<UE::MetaHuman::FFaceHighFrequencyData> InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Handles a high resolution texture request failure
	 */
	void OnHighResolutionTexturesRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Updates the progress of the texture download notification
	 */
	void OnHighResolutionTexturesProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Handles a high resolution body texture response.
	* Stores the new textures in the character and update any live character actors
	*/
	void OnHighResolutionBodyTexturesRequestCompleted(TSharedPtr<UE::MetaHuman::FBodyHighFrequencyData> InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Handles a high resolution body texture request failure
	 */
	void OnHighResolutionBodyTexturesRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Updates the progress of the body texture download notification
	 */
	void OnHighResolutionBodyTexturesProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey);

public:
	//
	// Eyes editing
	//

	/**
	 * Updates the editing state of the Character with the given eyes settings
	 */
	void ApplyEyesSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterEyesSettings& InEyesSettings) const;

	/**
	 * @brief Commits the eyes settings to the character and updates the associated actors
	 * 
	 * @param InCharacter The character in which the eyes settings are going to be committed
	 * @param InEyesSettings the eyes settings to commit to the character
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Eyes")
	void CommitEyesSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterEyesSettings& InEyesSettings) const;

private:

	/**
	 * Utility function to apply the eyes settings in the character data and update the eyes material with the eyes settings
	 */
	static void ApplyEyesSettings(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterEyesSettings& InEyesSettings);

public:
	//
	// Makeup editing
	//

	/**
	 * Updates the editing state of the Character with the given makeup settings
	 */
	void ApplyMakeupSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterMakeupSettings& InMakeupSettings) const;

	/**
	 * @brief Commits the makeup settings to the character and updates the associated actors
	 * 
	 * @param InCharacter The character in which the makeup settings are going to be commited
	 * @param InMakeupSettings the makeup settings to commit to the character
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Makeup")
	void CommitMakeupSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterMakeupSettings& InMakeupSettings) const;

private:

	/**
	 * @brief Utility function to apply the makeup settings in the character data and update the face material with the makeup settings
	 * 
	 * @param InCharacterData Character Editor Data containing the materials to be updated
	 * @param InMakeupSettings The makeup settings to apply to the materials
	 */
	static void ApplyMakeupSettings(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterMakeupSettings& InMakeupSettings);

public:
	//
	// Face sculpting and editing
	//

	/**
	 * Applies the given state in the MetaHuman Character Actors registered against the character.
	 */
	void ApplyFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState);

	/**
	 * Provides read-only access to the current face editing state.
	 * 
	 * If edits have been made since the last call to CommitFaceState, this will be different from
	 * Character's stored face state.
	 */
	TSharedRef<const FMetaHumanCharacterIdentity::FState> GetFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
	
	/**
	 * Provides read-only access to the FDNAToSkelMeshMap for the current face editing state.
	 */
	TSharedRef<const FDNAToSkelMeshMap> GetFaceDnaToSkelMeshMap(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Creates a copy of the current face editing state.
	 * 
	 * Same as GetFaceState, but creates a copy owned by the caller for convenience.
	 */
	[[nodiscard]] TSharedRef<FMetaHumanCharacterIdentity::FState> CopyFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Commits the Face State into the Character asset in order to be serialized when the asset is saved.
	 * 
	 * Also updates the face editing state.
	 */
	void CommitFaceState(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState);

	/**
	 * Commits the Face State of the given Character to be serialized. This needs to be done after after using the
	 * sculpting or blending APIs to persist the changes
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Sculpting")
	void CommitFaceState(UMetaHumanCharacter* InCharacter);

	/**
	* Evaluate the face state for each of the supplied characters, and compare the vertices and vertex normals
	* Requires the characters to have been added for edit using TryAddObjectToEdit
	* Returns true if all corresponding vertices and vertex normals are within InTolerance of each other in terms of vector norm
	*/
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Testing")
	bool CompareFaceState(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, float InTolerance) const;


	/** 
	 * Returns a reference to a delegate that fires whenever the face editing state of the given character is modified.
	 * 
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */ 
	FSimpleMulticastDelegate& OnFaceStateChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Adapts the given face DNA to fit the character's current body, returning the corrected DNA. 
	 * Performs CopyBodyJointsToFace and UpdateFaceSkinWeightsFromBodyAndVertexNormals on the input DNA.
	 * 
	 * @param InCharacter Character whose body the DNA is being aligned to
	 * @param InFaceDNAReader Face DNA to align
	 * @param bUpdateDescendentFaceJoints If true, descendent face joints are updated from body joints
	 * @return Aligned DNA, or the input DNA unchanged if alignment cannot be performed (no body or incompatible)
	 */
	TSharedPtr<IDNAReader> AlignFaceDNAWithBody(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader, bool bUpdateDescendentFaceJoints = false);

	/**
	 * Updates the face editing state and SkelMesh from the given DNA. Does NOT modify the DNA.
	 *
	 * @param InCharacter Character to update face DNA for
	 * @param InFaceDNAReader DNA to apply (must be non-null)
	 * @param InLodUpdateOption Chooses which LODs in the SkelMesh are updated
	 * @return true if the face mesh was successfully updated, false otherwise
	 */
	bool ApplyFaceDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InFaceDNAReader, ELodUpdateOption InLodUpdateOption = ELodUpdateOption::All);

	/**
	 * @deprecated Body-aligns and applies the DNA. Use AlignFaceDNAWithBody + ApplyFaceDNA(TSharedRef, …) instead.
	 * The new ApplyFaceDNA does not modify the DNA, callers that need alignment should call AlignFaceDNAWithBody explicitly.
	 */
	UE_DEPRECATED(5.8, "Use AlignFaceDNAWithBody + ApplyFaceDNA(TSharedRef<IDNAReader>, LodOption) instead. The new ApplyFaceDNA does not modify the DNA.")
	TSharedPtr<IDNAReader> ApplyFaceDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader, bool& bOutUpdatedFaceMesh, ELodUpdateOption InLodUpdateOption = ELodUpdateOption::All);

	/**
	 * @deprecated Body-aligns and applies the DNA. Use AlignFaceDNAWithBody + ApplyFaceDNA(TSharedRef, …) instead.
	 * The new ApplyFaceDNA does not modify the DNA, callers that need alignment should call AlignFaceDNAWithBody explicitly.
	 */
	UE_DEPRECATED(5.8, "Use AlignFaceDNAWithBody + ApplyFaceDNA(..., TSharedRef<IDNAReader>, ...) instead. The new ApplyFaceDNA does not modify the DNA.")
	TSharedPtr<IDNAReader> ApplyFaceDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader, ELodUpdateOption InLodUpdateOption = ELodUpdateOption::All);

	/**
	 * Create a face skeletal mesh from the imported DNA.
	 */
	void ImportFaceDNA(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader);

	/**
	 * Enable skeletal post-processing.
	 * This will enable running the face and body rig and correctives.
	 */
	UE_DEPRECATED(5.7, "Enabling animation is now handled internally by the actors and this function is not needed anymore")
	void EnableSkeletalPostProcessing(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Disable skeletal post-processing.
	 * This will enable running the face and body rig and correctives.
	 */
	UE_DEPRECATED(5.7, "Disabling animation is now handled internally by the actors and this function is not needed anymore")
	void DisableSkeletalPostProcessing(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Enable animation.
	 * This will connect the preview character to the invisible driving actor.
	 */
	UE_DEPRECATED(5.7, "Enabling animation is now handled internally by the actors and this function is not needed anymore")
	void EnableAnimation(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Disable animation.
	 * This will disconnect the preview character from the invisible driving actor.
	 */
	UE_DEPRECATED(5.7, "Disabling animation is now handled internally by the actors and this function is not needed anymore")
	void DisableAnimation(TNotNull<const UMetaHumanCharacter*> InCharacter);
	
	/**
	 * Commits the Face DNA into the Character asset in order to be serialized when the asset is saved.
	 */
	void CommitFaceDNA(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InFaceDNAReader);

	/**
	 * Reset character face.
	 */
	void ResetCharacterFace(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Returns the list of Face Gizmo positions from the Character's state
	 */
	[[nodiscard]] TArray<FVector3f> GetFaceGizmos(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Sets the face gizmo to an exact position.
	 * This function updates the character's Face mesh and returns the list of updated gizmo positions
	 */
	[[nodiscard]] TArray<FVector3f> SetFaceGizmoPosition(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InGizmoIndex, const FVector3f& InPosition, bool bInSymmetric, bool bInEnforceBounds);

	/**
	 * Sets the face gizmo to an exact rotation.
	 * This function updates the character's Face mesh and returns the list of updated gizmo positions
	 */
	[[nodiscard]] TArray<FVector3f> SetFaceGizmoRotation(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InGizmoIndex, const FVector3f& InRotation, bool bInSymmetric, bool bInEnforceBounds);

	/**
	* Scales the given gizmo.
	* This function updates the character's Face mesh and returns the list of updated gizmo positions
	*/
	[[nodiscard]] TArray<FVector3f> SetFaceGizmoScale(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InGizmoIndex, float InScale, bool bInSymmetric, bool bInEnforceBounds);

	/**
	 * Returns the list of Face Landmark positions from the Character's state
	 */
	[[nodiscard]] TArray<FVector3f> GetFaceLandmarks(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * @brief Gets the positions of the face landmarks for a given character.
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to obtain the landmarks for
	 * @param OutFaceLandmarks the list with the face landmarks positions
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MetaHuman|Sculpting")
	void GetFaceLandmarks(const UMetaHumanCharacter* InCharacter, TArray<FVector>& OutFaceLandmarks) const;

	/**
	 * Translates the given landmark by a delta.
	 * This function updates the character's Face mesh and returns the list of updated landmark positions
	 */
	[[nodiscard]] TArray<FVector3f> TranslateFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, int32 InLandmarkIndex, const FVector3f& InDelta, bool bInTranslateSymmetrically);

	/**
	 * @brief Translates the face landmarks by the given deltas
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * The number of landmark indices and deltas must match
	 * 
	 * 
	 * @param InCharacter the character to translate the landmarks for
	 * @param InLandmarkIndices The index of each landmark to be translated
	 * @param InDeltas the list of translation deltas to apply to each landmark
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Sculpting")
	void TranslateFaceLandmarks(const UMetaHumanCharacter* InCharacter, const TArray<int32>& InLandmarkIndices, const TArray<FVector>& InDeltas);

	/**
	 * @brief Gets the coefficients of the underlying face model for the given character.
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to obtain the coefficients for
	 * @param OutCoefficients The coefficients array to fill
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MetaHuman|Sculpting")
	void GetFaceModelCoefficients(const UMetaHumanCharacter* InCharacter, TArray<float>& OutCoefficients) const;

	/**
	 * @brief Sets the coefficients of the underlying face model for the given character.
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to update
	 * @param InCoefficients The coefficients to apply
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Sculpting")
	void SetFaceModelCoefficients(const UMetaHumanCharacter* InCharacter, const TArray<float>& InCoefficients);

	/**
	 * Selects a vertex on the face by intersecting the ray with the current face mesh.
	 */
	int32 SelectFaceVertex(TNotNull<const UMetaHumanCharacter*> InCharacter, const FRay& InRay, FVector& OutHitVertex, FVector& OutHitNormal);

	/**
	 * Adds additional custom landmark manipulator on a given mesh surface point.
	 */
	void AddFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InMeshVertexIndex);

	/**
	 * Removes selected landmark manipulator.
	 */
	void RemoveFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InLandmarkIndex);

	/**
	 * Blends Face region though preset states.
	 */
	TArray<FVector3f> BlendFaceRegion(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InRegionIndex, const TSharedPtr<const FMetaHumanCharacterIdentity::FState>& InStartState, const TArray<TSharedPtr<const FMetaHumanCharacterIdentity::FState>>& InPresetStates, TConstArrayView<float> InPresetWeights, EBlendOptions InBlendOptions, bool bInBlendSymmetrically);

	/**
	 * Method which handles calls to AutoRigService.
	 */
	UE_DEPRECATED(5.7, "Please use RequestAutoRigging")
	void AutoRigFace(TNotNull<UMetaHumanCharacter*> InCharacter, const UE::MetaHuman::ERigType InRigType);

	/**
	 * @brief Make a request to auto-rig a character
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to be auto-rigged
	 * @param InParams Parameters to control the auto-rigging process
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Auto-Rigging")
	void RequestAutoRigging(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterAutoRiggingRequestParams& InParams = FMetaHumanCharacterAutoRiggingRequestParams());

	/**
	 * Remove the face rig from InCharacter, reverting the face mesh back to the
	 * archetype DNA and unregistering any morph targets.
	 *
	 * @param InCharacter Character to remove the face rig from. No-op if null.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Auto-Rigging")
	void RemoveFaceRig(UMetaHumanCharacter* InCharacter);

	/**
	 * Remove the body rig from InCharacter
	 */
	void RemoveBodyRig(TNotNull<UMetaHumanCharacter*> InCharacter);

	/**
	 * Utility function that sets the eyelashes variant to the input state based on the eyelashes type property
	 */
	void UpdateEyelashesVariantFromProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties) const;

	/**
	 * Utility function that sets the teeth variant to the input state based on the teeth type property
	 * Also allows the user to turn off the (teeth) expressions at the end of using the tool
	 */
	void UpdateTeethVariantFromProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterTeethProperties& InTeethProperties, bool bInUseExpressions = true) const;

	/**
	 * Utility function that sets the high frequency variant to the input state based on the skin texture property
	 */
	void UpdateHFVariantFromSkinProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterSkinProperties& InSkinProperties) const;

	/**
	 * Returns true if there is an active request to auto rig the face of the given character
	 */
	bool IsAutoRiggingFace(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Get the rigging state for the supplied character
	 */
	EMetaHumanCharacterRigState GetRiggingState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Fit the state to the supplied target vertices, which will use whatever part(s) the user has supplied to fit the model
	 * EHeadFitToTargetMeshes::Head vertices must always be supplied in the InTargetVertices, using the supplied fitting options.
	 * Returns true if successful, false otherwise
	 */
	bool FitStateToTargetVertices(TNotNull<UMetaHumanCharacter*> InCharacter, const TMap<EHeadFitToTargetMeshes, TArray<FVector3f>>& InTargetVertices, const FFitToTargetOptions & InFitToTargetOptions);

	/**
	 * @brief Fit the character to the supplied vertices.
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 *
	 * @param InCharacter The character to be fitted
	 * @param InParams Parameters to control the fitting process as well as the meshes
	 * @return true if successful, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool FitStateToTargetVertices(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterFitToVerticesParams& InParams);

	/**
	 * Fit the state to the supplied face DNA, using the supplied fitting options. Returns true if successful, false otherwise
	 */
	bool FitToFaceDna(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<class IDNAReader> InFaceDna, const FFitToTargetOptions& InFitToTargetOptions);

	/**
	 * Fits the Character face state to the conformed mesh of the input Identity asset
	 */
	EImportErrorCode ImportFromIdentity(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<const class UMetaHumanIdentity*> InMetaHumanIdentity, const FImportFromIdentityParams& InImportParams);

	/**
	 * Fits the Character face state to the conformed mesh of the input Identity asset.
	 * Suitable for use from Python and Blueprints.
	 *
	 * @param InMetaHumanCharacter  The character to import into
	 * @param InMetaHumanIdentity   The MetaHuman Identity asset to import from
	 * @param InImportParams        Options controlling the import
	 * @return EImportErrorCode::Success on success, otherwise an error code describing the failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode ImportFromIdentity(UMetaHumanCharacter* InMetaHumanCharacter, const class UMetaHumanIdentity* InMetaHumanIdentity, const FImportFromIdentityParams& InImportParams);

	/**
	 * Either fits the Character face state to the input face DNA, or imports the DNA as-is, depending on options
	 */
	EImportErrorCode ImportFromFaceDna(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InFaceDna, const FImportFromDNAParams& InImportParams);

	/**
	 * @brief Either fits the Character face state to the input face DNA, or imports the DNA as-is, depending on options
	 *
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * 
	 * @param InCharacter The character to import the DNA to
	 * @param InDNAFilePath Path to the DNA file to be imported
	 * @param InImportParams Options for how to perform the import operation
	 * @return code indicating success or failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode ImportFromFaceDna(UMetaHumanCharacter* InCharacter, const FString& InDNAFilePath, const FImportFromDNAParams& InImportParams);


	/**
	 * @brief Fits the Character face state to the conformed mesh of the input asset, which must be a SkelMesh or Static Mesh which has the correct number of vertices.
	 *
	 * In addition, the user can (optionally) in the case of a StaticMesh pass in up to three additional meshes for left eye, right eye and teeth, which if not
	 * null will be used in the fitting. Note that for the StaticMesh, if the extra meshes are present, they will be used and the flags in the import options will be ignored. 
	 * Eye and Teeth meshes must contain the correct number of vertices for a MetaHuman.
	 *
	 * @param InCharacter The character to import the template mesh(es) to
	 * @param InTemplateMesh The head mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman head mesh
	 * @param InTemplateLeftEyeMesh If not nullptr, must be a StaticMesh for the left eye which much match the topology of a MetaHuman left eye mesh
	 * @param InTemplateRightEyeMesh If not nullptr, must be a StaticMesh for the right eye which much match the topology of a MetaHuman right eye mesh
	 * @param InTemplateTeethMesh If not nullptr, must be a StaticMesh for the teeth which much match the topology of a MetaHuman teeth mesh
	 * @param InImportParams the parameters used during the fitting process
	 * @return code indicating success or failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode ImportFromTemplate(UMetaHumanCharacter* InCharacter, UObject* InTemplateMesh, UObject* InTemplateLeftEyeMesh, UObject* InTemplateRightEyeMesh, UObject* InTemplateTeethMesh, 
		const FImportFromTemplateParams& InImportParams);


	/**
	 * Initializes metahuman character using selected preset character.
	 */
	void InitializeFromPreset(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<const UMetaHumanCharacter*> InPresetCharacter);

private:
	/**
	 * Called when an AutoRigging request completes
	 */
	void OnAutoRigFaceRequestCompleted(const UE::MetaHuman::FAutorigResponse& InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey, const UE::MetaHuman::ERigType InRigType);

	/**
	 * Handles a high resolution texture request failure
	 */
	void OnAutoRigFaceRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Updates the progress of an AutoRigging request
	 */
	void OnAutoRigFaceProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey);

	/**
	 * Sets the given face state on the Character Data.
	 * 
	 * Note that this function takes ownership of InState, unlike the public overload that takes a copy of it.
	 */
	static void ApplyFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TSharedRef<FMetaHumanCharacterIdentity::FState> InState);
	
	/** Updates the face editing state from the given skin properties */
	void ApplySkinPropertiesToFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterSkinProperties& InSkinProperties) const;

	/** Updates the face editing state from the given eyelashes and teeth properties */
	void ApplyEyelashesAndTeethPropertiesToFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties, const FMetaHumanCharacterTeethProperties& InTeethProperties, 
		bool bInUpdateEyelashes, bool bInUpdateTeeth, ELodUpdateOption InUpdateOption) const;

	/**
	 * Updates the Face Mesh of the Character using state stored in the actor and the given vertices and vertex normals
	 * This function does not evaluate the model and purely updates the skeletal mesh. It is the caller
	 * responsibility to call Evaluate and obtain the vertices and normals to pass to this function.
	 */
	static void UpdateFaceMeshInternal(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanRigEvaluatedState& InVerticesAndNormals, ELodUpdateOption InUpdateOption);

	/** Core implementation shared by both FitFaceStateFromBody overloads.
	 *  Drives the head from the body vertices, then fits any supplied teeth/eye
	 *  vertex arrays into the bind frame and calls FitToTarget. */
	void FitFaceStateFromBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter,
		const TArray<FVector3f>& InTeethVertices,
		const TArray<FVector3f>& InLeftEyeVertices,
		const TArray<FVector3f>& InRightEyeVertices);
	
	/**
	 * Get the data for performing import from template mesh
	 */
	EImportErrorCode GetDataForConforming(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<UObject*> InTemplateMesh, UObject* InTemplateLeftEyeMesh, UObject* InTemplateRightEyeMesh, UObject* InTemplateTeethMesh, const FImportFromTemplateParams& InImportParams, TMap<EHeadFitToTargetMeshes, TArray<FVector3f>>& OutVertices);

public:
	//
	// Body Editing
	//

	enum class EBodyMeshUpdateMode : uint8
	{
		/** A fast update to be used while dragging sliders, etc. Only the data needed for immediate rendering is updated. */
		Minimal,
		/** The full update that takes longer. This must be done once the slider drag or other input is complete. */
		Full
	};

	/**
	 * Applies the given custom body state to MetaHuman Character Actors registered against the character.
	 * Evaluates the state and updates the body mesh, updates the character's body mesh state using the state stored in the character
	 * 
	 * The subsystem takes a copy of the passed-in state and uses the copy, so InState will not be modified.
	 */
	void ApplyBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode);

	/**
	* Commits the Body State into the Character asset in order to be serialized when the asset is saved.
	* If there are live Character actors registered against the subsystem, also update their face state.
	*/
	void CommitBodyState(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode = EBodyMeshUpdateMode::Full);

	/**
	* Evaluate the body state for each of the supplied characters, and compare the vertices and vertex normals
	* Requires the characters to have been added for edit using TryAddObjectToEdit
	* Returns true if all corresponding vertices and vertex normals are within InTolerance of each other in terms of vector norm
	*/
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Testing")
	bool CompareBodyState(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, float InTolerance) const;

	/**
	 * Commits the Body State of the given Character to be serialized. This needs to be done after after using the
	 * sculpting or blending APIs to persist the changes
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Sculpting")
	void CommitBodyState(UMetaHumanCharacter* InCharacter);

	/** 
	 * Returns a reference to a delegate that fires whenever the body editing state of the given character is modified.
	 * 
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */ 
	FSimpleMulticastDelegate& OnBodyStateChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Returns a reference to a delegate that fires whenever the target-mesh keypoints collection on
	 * the given character is modified including undo/redo of keypoint changes.
	 *
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */
	FSimpleMulticastDelegate& OnTargetMeshKeyPointsChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);
	
	/**
	 * Provides read-only access to the current body editing state.
	 * 
	 * If edits have been made since the last call to CommitBodyState, this will be different from
	 * Character's stored body state.
	 */
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> GetBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	 * Creates a copy of the current body editing state.
	 * 
	 * Same as GetBodyState, but creates a copy owned by the caller for convenience.
	 */
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> CopyBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
	
	/**
	 * Sets the character state to the character's posed state 
	 * 
	 * Used to restore a character's posed state if one has been stored during import for the target mesh key
	 */
	bool SetToTargetPosedState(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey);



	/**
	 * Sets the body vertex and joint global delta scale
	 */
	void SetBodyGlobalDeltaScale(TNotNull<UMetaHumanCharacter*> InCharacter, float InBodyGlobalDelta) const;

	/**
	 * Gets the body vertex and joint global delta scale
	 */
	float GetBodyGlobalDeltaScale(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	* Updates the body editing state with the given DNA. Optionally importing the mesh from DNA via interchange
	*/
	TSharedPtr<IDNAReader> ApplyBodyDNA(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InBodyDNAReader, bool bImportMesh = true);

	/**
	 * Commits the Body DNA into the Character asset in order to be serialized when the asset is saved,
	 * Sets the Character as having a fixed body type with bFixedBodyType. Fixed types import the mesh from DNA via interchange
	 */
	void CommitBodyDNA(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDNAReader, bool bFixedBodyType = true);

	/**
	*  Fits the Character body state to the fixed body DNA
	*/
	bool ParametricFitToDnaBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter);

	/**
	*  Fits the Character body state to the current fixed compatibility body
	*/
	bool ParametricFitToCompatibilityBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter);

#if WITH_EDITORONLY_DATA
	/**
	* Either fits the Character body state to the input body DNA, or imports the DNA as-is, depending on options
	*/
	UE_DEPRECATED(5.7, "ImportFromBodyDna option has been deprecated. Please use ConformBody, SetBodyJointTranslations, SetBodyMesh or ImportWholeRig.")
	EImportErrorCode ImportFromBodyDna(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDna, const FImportBodyFromDNAParams& InImportOptions);
	
	/**
	 * @brief Fits the Character body state to the conformed mesh of the input asset, which must be a SkelMesh or Static Mesh which has the correct number of vertices.
	 * 	 
	 * @param InMetaHumanCharacter The character to import the body template mesh to
	 * @param InTemplateMesh The mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman body or combined head and body mesh
	 * @param InBodyFitOptions the fitting options used during the fitting process
	 * @return code indicating success or failure
	 */
	UE_DEPRECATED(5.7, "ImportFromBodyTemplate option has been deprecatd. Please use ConformBody, SetBodyJointTranslations, SetBodyMesh or ImportWholeRig.")
	EImportErrorCode ImportFromBodyTemplate(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InTemplateMesh, EMetaHumanCharacterBodyFitOptions InBodyFitOptions);
#endif // WITH_EDITORONLY_DATA

	/**
	 * Get the mesh for performing import from body template 
	 */
	EImportErrorCode GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector3f>& OutVertices);

	/**
	 * @brief Blueprint exposed version of the GetMeshForBodyConforming function
	 *
	 * @param InMetaHumanCharacter The character to import the body template mesh to
	 * @param InBodyTemplateMesh The mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman body or combined head and body mesh	 
	 * @param InHeadTemplateMesh The mesh (either StaticMesh or SkelMesh) which much match the topology of a MetaHuman head mesh
	 * @param bInMatchVerticesByUVs flag which if set to true uses UV matching to match the vertices, otherwise requires exactly matching SkelMesh
	 * @param OutVertices On completion contains the body mesh vertices
	 * @return code indicating success or failure
	 */
	UE_DEPRECATED(5.8, "Use GetMeshForBodyConformingFromTemplate instead.")
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming", meta = (DeprecatedFunction, DeprecationMessage = "Use GetMeshForBodyConformingFromTemplate instead."))
	EImportErrorCode GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector>& OutVertices);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode GetMeshForBodyConformingFromTemplate(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector3f>& OutVertices);

	
	/**
	 * Get the mesh for performing import from body template
	 */
	EImportErrorCode GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, TSharedRef<IDNAReader> BodyDNA, TSharedPtr<IDNAReader> HeadDNA, TArray<FVector3f>& OutVertices);
	
	/**
	 * Get the body mesh vertices for conforming, from DNA file paths.
	 *
	 * @param InMetaHumanCharacter  The character to conform
	 * @param InBodyDnaFilePath     Absolute file path to the body DNA file
	 * @param InHeadDnaFilePath     Absolute file path to the head DNA file (optional, pass empty string to omit)
	 * @param OutVertices           On success, contains the body mesh vertices
	 * @return EImportErrorCode::Success on success, otherwise an error code describing the failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode GetMeshForBodyConformingFromDNA(UMetaHumanCharacter* InMetaHumanCharacter, const FString& InBodyDnaFilePath, const FString& InHeadDnaFilePath, TArray<FVector3f>& OutVertices);

	/**
	 * Get the joints for performing import from body template
	 */
	EImportErrorCode GetJointsForBodyConforming(USkeletalMesh* InTemplateMesh, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations);

	/**
	 * @brief Blueprint exposed version of the GetJointsForBodyConforming function
	 *
	 * @param InTemplateMesh The mesh to be conformed which much match the topology of a MetaHuman body or combined head and body mesh
	 * @param OutJointWorldTranslations On completion contains the body joint world translations
	 * @param OutJointRotations On completion contains the body joint rotations
	 * @return code indicating success or failure
	 */
	UE_DEPRECATED(5.8, "Use GetJointsForBodyConformingFromTemplate instead.")
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming", meta = (DeprecatedFunction, DeprecationMessage = "Use GetJointsForBodyConformingFromTemplate instead."))
	EImportErrorCode GetJointsForBodyConforming(USkeletalMesh* InTemplateMesh, TArray<FVector>& OutJointWorldTranslations, TArray<FVector>& OutJointRotations);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode GetJointsForBodyConformingFromTemplate(USkeletalMesh* InTemplateMesh, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations);


	/**
	 * Get the joints for performing import from body template
	 */
	EImportErrorCode GetJointsForBodyConforming(TSharedRef<IDNAReader> BodyDNA, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations);

	/**
	 * Get the body joint data for conforming, from a DNA file path.
	 * Suitable for use from Python and Blueprints.
	 *
	 * @param InBodyDnaFilePath             Absolute file path to the body DNA file
	 * @param OutJointWorldTranslations     On success, contains the body joint world translations
	 * @param OutJointRotations             On success, contains the body joint rotations
	 * @return EImportErrorCode::Success on success, otherwise an error code describing the failure
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode GetJointsForBodyConformingFromDNA(const FString& InBodyDnaFilePath, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations);
	
	/* Conforms the body mesh to target parameters */
	bool ConformBody(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bTargetIsInAPose, bool bEstimateJointsFromMesh);

	/**
	 * Updates the face state to fit the current body state, and optionally re-fits teeth and/or eyes
	 * using vertices extracted from the supplied template mesh objects. Call this after ConformBody.
	 * @param InMetaHumanCharacter  The character whose face state will be updated.
	 * @param InTeethMesh           Optional static or skeletal mesh in MetaHuman teeth topology. Pass null to skip.
	 * @param InLeftEyeMesh         Optional static or skeletal mesh in MetaHuman left eye topology. Pass null to skip.
	 * @param InRightEyeMesh        Optional static or skeletal mesh in MetaHuman right eye topology. Pass null to skip.
	 * @param bInMatchVerticesByUVs When true, use UV correspondence to map template vertices to the DNA archetype order.
	 * @return true if the face state was successfully updated.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool FitFaceStateFromBodyWithEyesTeethTemplate(UMetaHumanCharacter* InMetaHumanCharacter,
		UObject* InTeethMesh,
		UObject* InLeftEyeMesh,
		UObject* InRightEyeMesh,
		bool bInMatchVerticesByUVs);

	/**
	 * Updates the face state to fit the current body state, then re-fits teeth and eyes using
	 * vertex positions read directly from a face DNA reader. The head shape is driven by the body,
	 * not the DNA — only teeth (mesh index 1), left eye (3), and right eye (4) are taken from the DNA.
	 * Call this after ConformBody.
	 * @param InMetaHumanCharacter  The character whose face state will be updated.
	 * @param InFaceDna             DNA reader to extract teeth and eye vertices from.
	 * @return true if the face state was successfully updated.
	 */
	bool FitFaceStateFromBodyWithEyesTeethDNA(UMetaHumanCharacter* InMetaHumanCharacter, TSharedRef<class IDNAReader> InFaceDna);

	/**
	 * Updates the face state to fit the current body state, then re-fits teeth and eyes using
	 * vertex positions read from a face DNA file. The head shape is driven by the body,
	 * not the DNA — only teeth (mesh index 1), left eye (3), and right eye (4) are taken from the DNA.
	 * Call this after ConformBody.
	 * @param InMetaHumanCharacter  The character whose face state will be updated.
	 * @param InFaceDnaFilePath     Path to the face DNA file to extract teeth and eye vertices from.
	 * @return true if the face state was successfully updated.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool FitFaceStateFromBodyWithEyesTeethDNA(UMetaHumanCharacter* InMetaHumanCharacter, const FString& InFaceDnaFilePath);
	
	/* Conforms the body and head meshes to target meshes */
	bool ConformTargetMeshesAsync(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FConformTargetParams& InConformTargetParams, bool bBlocking = false);

	/** Extract topology vertices and triangle indices from a static or skeletal mesh.
	 * Uses the MeshDescription to return the same data that the interactive conform tool uses internally.
	 * @param InMesh The source mesh (UStaticMesh or USkeletalMesh)
	 * @param OutVertices Topology vertex positions
	 * @param OutTriangleIndices Triangle indices (3 per triangle, referencing OutVertices)
	 * @return true if extraction succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	static bool GetMeshDataForConforming(UObject* InMesh, TArray<FVector3f>& OutVertices, TArray<int32>& OutTriangleIndices);

	/** Synchronously conforms the body and head meshes to target parameters.
	 * Python/Blueprint-friendly wrapper around ConformTargetMeshesAsync that
	 * runs with bBlocking=true and returns the solver result.
	 * @param InMetaHumanCharacter The character to conform. Must be open for editing.
	 * @param InTargetMeshKey Identifies which target mesh slot (body / head / combined) the
	 *                       conform applies to. Used as the per-mesh key for target-pose state
	 *                       storage so successive conforms against different meshes don't
	 *                       overwrite each other.
	 * @param InConformTargetParams Solver inputs: target mesh data, keypoints, face tracking, solver settings.
	 * @return true if the conform solver succeeded; false on invalid input or solve failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool ConformToTargetMeshes(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FConformTargetParams& InConformTargetParams);

	/** Run face landmark detection on an image and return tracking contour curves.
	 * Use with a front-facing portrait of the target mesh — typically a SceneCapture2D
	 * render. The first call after editor start synchronously loads the face contour
	 * tracker NNE models (the default tracker asset), so it can take several seconds;
	 * subsequent calls hit the cached models and are fast.
	 * @param InImageData Raw pixel data (BGRA8); length must equal InWidth * InHeight.
	 * @param InWidth Image width in pixels.
	 * @param InHeight Image height in pixels.
	 * @param OutCurveTrackingPoints Named contour curves (2D points in image space).
	 * @return true if any landmarks were detected; false on invalid input, model load failure, or no face found.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool TrackFaceLandmarksFromImage(const TArray<FColor>& InImageData, int32 InWidth, int32 InHeight, TMap<FString, FTrackingPoints>& OutCurveTrackingPoints);

	/* Rigidly aligns the character state to the target meshes */
	bool AlignToTargetMeshesAsync(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FConformTargetParams& InConformTargetParams, bool bBlocking = false);
	
	/** After conforming body state in pose, finalizes body and face states by evaluating solved body state in MetaHuman A Pose, and updating face state from it.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	void CommitPosedStateAsAPose(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey);

	/** Synchronously aligns tthe body and head meshes to target meshes 
	 * Python/Blueprint-friendly wrapper that calls AlignToTargetMeshesAsync in blocking mode.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool AlignToTargetMeshes(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FConformTargetParams& InConformTargetParams);
	
	/* Returns if Async Conform process is pending */
	bool IsAsyncConformPending(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter) const;

	/* Returns if the body state is currently matching the target posed state for the given target mesh key */
	bool IsBodyStateMatchingTargetPosedState(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey) const;
	
	/** 
	 * Returns a reference to a delegate that fires on each conform async iteration
	 * 
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */ 
	FOnAsyncMeshConformIteration& OnAsyncMeshConformIteration(TNotNull<const UMetaHumanCharacter*> InCharacter);
	
	/** 
	 * Returns a reference to a delegate that fires whenever the conform async is completed.
	 * 
	 * May only be called if the Character is registered using TryAddObjectToEdit.
	 */ 
	FOnAsyncMeshConformCompleted& OnAsyncMeshConformCompleted(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/* Refines vertices to the target mesh */
	bool RefineVerticesToTargeAsync(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FRefinementTargetParams& InRefinementTargetParams);
	
	/* Cancels any async mesh import process */
	void CancelMeshAsyncProcess(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter);

	/* Commit the target mesh keypoints to the character (filters out preset keypoints, only commits Custom) */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	void CommitTargetMeshKeypoints(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FMetaHumanCharacterTargetKeyPoints& InTargetMeshKeyPoints, const TMap<FName, EKeyPointType>& InKeyPointTypes);

	/* Get preset keypoints from MetaHumanCreator API (body or head) without committing them */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	TMap<FName, int32> GetPresetBodyKeyPoints(const UMetaHumanCharacter* InCharacter) const;

	/* Commit the target mesh tracking results to the character */
	void CommitTargetMeshTrackingResults(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, const FMetaHumanCharacterTargetTrackingResults& InTrackingResults);
	
	/**
	 * @brief Blueprint exposed version of the ConformBody functions
	 *
	 * @param InMetaHumanCharacter The character to conform
	 * @param InVertices The body vertices, obtained by calling GetMeshForBodyConforming
	 * @param InJointRotations The body joint rotations, obtained by calling GetJointsForBodyConforming
	 * @param bRepose If set to true, repose the body to the A pose after conformation, if false, leave as-is
	 * @param bEstimateJointsFromMesh If set to true, estimate the hand and feet joints from the supplied mesh vertices; otherwise, leave alone.
	 * @return true if successful, false otherwise
	 */
	UE_DEPRECATED(5.8, "Use ConformBodyToTarget instead.")
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming", meta = (DeprecatedFunction, DeprecationMessage = "Use ConformBodyToTarget instead."))
	bool ConformBody(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector>& InVertices, const TArray<FVector>& InJointRotations, bool bRepose, bool bEstimateJointsFromMesh);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool ConformBodyToTarget(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bTargetIsInAPose, bool bEstimateJointsFromMesh);

	/* Set custom body joints */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool SetBodyJoints(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InComponentJointTranslations, const TArray<FVector3f>& InJointRotations, bool bImportHelperJoints);

	/* Set custom body neutral mesh */ 
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	bool SetBodyMesh(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, bool bRepositionHelperJoints);

	/* Import body as a fully rigged Character  from DNA, sets character to a fixed body type */
	EImportErrorCode ImportBodyWholeRig(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDna, TSharedPtr<IDNAReader> InHeadDna);

	/* Import body as a fully rigged Character from DNA file paths, sets character to a fixed body type */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Conforming")
	EImportErrorCode ImportBodyWholeRig(UMetaHumanCharacter* InMetaHumanCharacter, const FString& InBodyDnaFilePath, const FString& InHeadDnaFilePath);
#if WITH_EDITORONLY_DATA
	/**
	 * Fit the state to the supplied body DNA. Returns true if successful, false otherwise
	 */
	UE_DEPRECATED(5.7, "FitToBodyDna option has been deprecatd. Please use ConformBodyFromDna with FConformBodyParams instead.")
	bool FitToBodyDna(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<class IDNAReader> InBodyDna, EMetaHumanCharacterBodyFitOptions InBodyFitOptions);
#endif // WITH_EDITORONLY_DATA

	/**
	 * @brief Gets the list of body constrains from the underlying parametric body model
	 * 
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * 
	 * @param InCharacter the character to retrieve the body constraints for
	 * @param bScaleMeasurementRangesWithHeight scale the measurement ranges by height to help stay within realistic model proportions
	 * @return the list of body constraints provided by the underlying parametric body model
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Sculpting")
	TArray<FMetaHumanCharacterBodyConstraint> GetBodyConstraints(const UMetaHumanCharacter* InCharacter, bool bScaleMeasurementRangesWithHeight = false) const;

	/**
	 * @brief Set body constraints and evaluate the parametric body
	 * 
	 * Requires the character to be added for edit using TryAddObjectToEdit
	 * 
	 * @param InCharacter The character to update the constraints
	 * @param InBodyContrains The list of body constraints to apply to the character
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Sculpting")
	void SetBodyConstraints(const UMetaHumanCharacter* InCharacter, const TArray<FMetaHumanCharacterBodyConstraint>& InBodyConstraints);

	/**
	* Reset the parametric body
	*/
	void ResetParametricBody(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Sets the MetaHuman body on the body editing state
	 */
	void SetMetaHumanBodyType(TNotNull<const UMetaHumanCharacter*> InCharacter, EMetaHumanBodyType InBodyType, EBodyMeshUpdateMode InUpdateMode);

	/**
	* Is the body a fixed body type, either imported from dna as a whole rig, or a fixed compatibility body
	*/
	bool IsFixedBodyType(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
	
	/**
	 * Selects a vertex on the body by intersecting the ray with the current body mesh.
	 */
	int32 SelectBodyVertex(TNotNull<const UMetaHumanCharacter*> InCharacter, const FRay& InRay, FVector& OutHitVertex, FVector& OutHitNormal) const;

	/**
	 * Returns the list of body region gizmo positions from the character's state
	*/
	[[nodiscard]] TArray<FVector3f> GetBodyGizmos(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/**
	* Blends Face region though preset states.
	*/
	TArray<FVector3f> BlendBodyRegion(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InRegionIndex, EBodyBlendOptions InBodyBlendOptions, const TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>& InStartState, const TArray<TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>>& InPresetStates, TConstArrayView<float> InPresetWeights);
	
	/* Sets to evaluate a pose other than MetaHuman A pose from the state */ 
	void SetEvaluateBodyPose(UMetaHumanCharacter* InMetaHumanCharacter, bool bEvaluateBodyPose);

	/** Returns the face editing mesh for the character. */
	TNotNull<const USkeletalMesh*> GetFaceEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/** Returns the body editing mesh for the character. */
	TNotNull<const USkeletalMesh*> GetBodyEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/** @deprecated Use GetFaceEditMesh instead. */
	UE_DEPRECATED(5.8, "Debug_GetFaceEditMesh has been renamed to GetFaceEditMesh.")
	TNotNull<const USkeletalMesh*> Debug_GetFaceEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

	/** @deprecated Use GetBodyEditMesh instead. */
	UE_DEPRECATED(5.8, "Debug_GetBodyEditMesh has been renamed to GetBodyEditMesh.")
	TNotNull<const USkeletalMesh*> Debug_GetBodyEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const;

private:

	static void UpdateBodyMeshInternal(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
		const FMetaHumanRigEvaluatedState& InVerticesAndNormals, 
		ELodUpdateOption InUpdateOption, 
		bool bInUpdateDnaState);

	static void UpdateFaceFromBodyInternal(
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
		ELodUpdateOption InUpdateOption,
		bool bInUpdateNeutral);
	
	static void ApplyBodyState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode);

	/*
	 * Updates the character's fixed body type, fixed bodies are either imported from dna as a whole rig, or a fixed compatibility body
	 */
	void UpdateCharacterIsFixedBodyType(TNotNull<UMetaHumanCharacter*> InCharacter, bool bIsFixedBody);

	/**
	* Utility function that invokes a callback for each valid MetaHuman Character Editor Actor registered against the given MetaHuman Character
	 */
	static void ForEachCharacterActor(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TFunction<void(TScriptInterface<class IMetaHumanCharacterEditorActorInterface>)> InFunc);

	struct FMetaHumanCharacterIdentityModels
	{
		TSharedPtr<FMetaHumanCharacterIdentity> Face;
		TSharedPtr<FMetaHumanCharacterBodyIdentity> Body;
	};

	/**
	 * Returns the FMetaHumanCharacterIdentity of the given template type.
	 * If the Identity for the template doesn't exist it will be created and cached in CharacterIdentities
	 */
	const FMetaHumanCharacterIdentityModels& GetOrCreateCharacterIdentity(EMetaHumanCharacterTemplateType InTemplateType);

	/**
	 * Returns the path to where the face models for the given template type are stored
	 */
	static FString GetFaceIdentityTemplateModelPath(EMetaHumanCharacterTemplateType InTemplateType);

	/** 
	 * Returns the path to where the body model is stored
	 */
	static FString GetBodyIdentityModelPath();

	/** 
	 * Returns the path to where the legacy bodies are stored
	 */
	static FString GetLegacyBodiesPath();

	/**
	 * Creates the physics asset using body state
	 *
	 * @param InCharacter the character to generate the physics asset for
	 * @param InOuter the object to use as the outer for the new physics asset
	 * @param InBodyState the body state used to generate the physics asset
	 */
	static TObjectPtr<UPhysicsAsset> CreatePhysicsAssetForCharacter(TNotNull<const UMetaHumanCharacter*> InCharacter,
																	TNotNull<UObject*> InOuter,
																	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState);
	/**
	 * Updates a physics asset using body state
	 */
	static void UpdatePhysicsAssetFromBodyState(TNotNull<UPhysicsAsset*> InPhysicsAsset, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState);

	/**
	 * Called on instance updated. Updates head and body hidden face maps and materials
	 */
	void OnCharacterInstanceUpdated(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/**
	 * Updates hidden faces map on body material
	 */
	void UpdateCharacterPreviewMaterialHiddenFacesMask(TNotNull<const UMetaHumanCharacter*> InCharacter) const;
	
	/**
	* Gets measurements from face and body, either from dna or from character state
	*/
	TMap<FString, float> GetFaceAndBodyMeasurements(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, bool bGetMeasuresFromDNA) const;
	
	/* Called on async conform iteration to update character render data in scene */ 
	void OnMeshImportIterationUpdate(float InPercentage, 
		const FMetaHumanRigEvaluatedState& InBodyVerticesAndNormals, 
		const FMetaHumanRigEvaluatedState& InFaceVerticesAndNormals, 
		const TArray<FVector3f>& InJointPositions, 
		const TArray<FRotator3f>& InJointRotations,
		const FText& InSolveMessage,
		TNotNull<UMetaHumanCharacter*> InCharacter);

	/* Called on async conform completion to commit state */ 
	void OnMeshImportComplete(bool bSuccess, bool bWasCancelled,
		const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState, 
		const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState, 
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, 
		TNotNull<UMetaHumanCharacter*> InCharacter);

	
	/* Called on async conform failure cases to set notifications */
	void OnMeshImportFailedNotifications(TNotNull<UMetaHumanCharacter*> InCharacter);
	
	/* Updates override debug bone transforms used to display body bones during conform */
	static void UpdateDebugBoneTransformsFromState(const TSharedRef<FMetaHumanCharacterEditorData>& InCharacterData);

private:

	static ELodUpdateOption GetUpdateOptionForEditing();

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// Map a MetaHuman Character to the data it needs while being edited
	TMap<TObjectKey<UMetaHumanCharacter>, TSharedRef<FMetaHumanCharacterEditorData>> CharacterDataMap;

	// Map all the live cloud requests for a given MetaHuman Character
	TMap<TObjectKey<UMetaHumanCharacter>, FMetaHumanCharacterEditorCloudRequests> CharacterCloudRequests;

	// Map async mesh import contexts for each MetaHuman Character 
	TMap<TObjectKey<UMetaHumanCharacter>, FMetaHumanCharacterEditorMeshImportContext> CharacterMeshImportContexts;
	
	// Map with loaded Character Identity Models
	TSortedMap<EMetaHumanCharacterTemplateType, FMetaHumanCharacterIdentityModels> CharacterIdentities;

	// Face Synthesizer to be shared between all editable objects
	FMetaHumanFaceTextureSynthesizer FaceTextureSynthesizer;

	// Skin Tone Texture created from FaceTextureSynthesizer used in UI skin tone picker
	TWeakObjectPtr<class UTexture2D> SkinToneTexture;

	// Delegate handle for instance update
	FDelegateHandle CharacterInstanceUpdatedDelegateHandle;

	// Cached archetype meshes imported from DNA
	UPROPERTY()
	TObjectPtr<USkeletalMesh> MetaHumanFaceArchetypeMesh;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> MetaHumanBodyArchetypeMesh;

	// True if TryAddObjectToEdit is running
	bool bIsAddingObjectToEdit = false;

public:
	/**
	* Utility function that invokes a callback for each valid MetaHuman Character Editor Actor registered against the given MetaHuman Character
	 */
	void ForEachCharacterActor(TNotNull<const UMetaHumanCharacter*> InCharacter, TFunction<void(TScriptInterface<class IMetaHumanCharacterEditorActorInterface>)> InFunc);

	/**
	 * Destroys an editor actor previously created via CreateMetaHumanCharacterEditorActor and unregisters it
	 * from the character's actor list.
	 *
	 * RemoveObjectToEdit already destroys all actors registered for the character, so callers that paired
	 * Create with TryAddObjectToEdit / RemoveObjectToEdit do not need to invoke this directly. This is only
	 * required when CreateMetaHumanCharacterEditorActor was called without going through TryAddObjectToEdit
	 * (e.g. transient preview/thumbnail use cases) and the caller is responsible for tearing the actor down.
	 */
	void DestroyMetaHumanCharacterEditorActor(TScriptInterface<class IMetaHumanCharacterEditorActorInterface> InCharacterActor);

	//
	// Editing environment changes from toolbar options
	//
	UE_DEPRECATED(5.7, "Please use OnNotifyLightingEnvironmentChanged function for getting a delegate for all level related changes.")
	FOnNotifyLightingEnvironmentChanged& OnLightTonemapperChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);
	UE_DEPRECATED(5.7, "Please use OnNotifyLightingEnvironmentChanged function for getting a delegate for all level related changes.")
	FOnNotifyLightingEnvironmentChanged& OnLightEnvironmentChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);
	FOnNotifyLightingEnvironmentChanged& OnNotifyLightingEnvironmentChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FOnStudioLightRotationChanged& OnLightRotationChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FOnStudioBackgroundColorChanged& OnBackgroundColorChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FSimpleMulticastDelegate& OnPreviewMaterialChanged(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FOnCameraFocusRequested& OnCameraFocusRequested(TNotNull<const UMetaHumanCharacter*> InCharacter);

	FOnViewportToolbarRenderingQualityProfileChange& OnViewportToolbarRenderingQualityProfileChange(TNotNull<const UMetaHumanCharacter*> InCharacter);

	/*
	* !!!!DEPRECATED!!!!
	* USE NotifyLightingEnvironmentChanged
	* Updates the Environment Lighting studio.
	* This function executes an EnvironmentUpdate delegate which has a bound function inside of an EditorToolkit.
	* It is called when change happens inside a tile view which holds lighting studio options in toolbar menu or if tonemapper option changes,
	* or if user decides to use custom lighting environment.
	*/
	UE_DEPRECATED(5.7, "Please use NotifyLightingEnvironmentChanged function for all level related changes.")
	void UpdateLightingEnvironment(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanCharacterEnvironment InLightingEnvironment) const;

	/*
	*  !!!!DEPRECATED!!!!
	* USE NotifyLightingEnvironmentChanged
	* Updates the Environment Lighting studio.
	* This function executes an EnvironmentUpdate delegate which has a bound function inside of an EditorToolkit.
	* It is called when change happens inside a tile view which holds lighting studio options in toolbar menu.
	*/
	UE_DEPRECATED(5.7, "Please use NotifyLightingEnvironmentChanged function for all level related changes.")
	void UpdateTonemapperOption(TNotNull<UMetaHumanCharacter*> InCharacter, bool bInToneMapperEnabled) const;


	/*
	* Used for lighting scenario levels updates, including custom levels and post process levels, signals an update to editor toolkit
	*/
	void NotifyLightingEnvironmentChanged(TNotNull<UMetaHumanCharacter*> InCharacter) const;

	/**
	 *
	 */
	void UpdateLightRotation(TNotNull<UMetaHumanCharacter*> InCharacter, float InRotation) const;

	/**
	 * Updates the background color of the lighting environment
	 */
	void UpdateBackgroundColor(TNotNull<UMetaHumanCharacter*> InCharacter, const FLinearColor& InBackgroundColor) const;

	/*
	* Updates the Character Level of detail shown in Editor.
	*/
	void UpdateCharacterLOD(TNotNull<UMetaHumanCharacter*> InCharacter, const EMetaHumanCharacterLOD NewLODValue) const;

	/**
	 * Updates character actor groom components to always use cards instead of strands.
	 */
	void UpdateAlwaysUseHairCardsOption(TNotNull<UMetaHumanCharacter*> InCharacter, bool bInAlwaysUseHairCards);


	/**
	 * Used for rendering quality profile update from viewport toolbar, signals an update to editor toolkit
	*/
	void NotifyViewportToolbarRenderingQualityProfileChange(TNotNull<UMetaHumanCharacter*> InCharacter, const int32 InIndex) const;
};
