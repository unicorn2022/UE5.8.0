// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCollectionEditorPipeline.h"
#include "Item/MetaHumanCrowdOutfitPipeline.h"
#include "PerQualityLevelProperties.h"
#include "UObject/PerPlatformProperties.h"

#include "MetaHumanCrowdEditorPipeline.generated.h"

class USkeleton;
class UMaterialInterface;
class UMetaHumanCrowdAnimationConfig;
class UDataflow;
class UMetaHumanCrowdGroomPipeline;
class UMetaHumanCrowdSkeletalClothingPipeline;

/**
 * Per-slot face material override for the Crowd pipeline. When the slot name on the face
 * skeletal mesh matches SlotName and Material is non-null, the build replaces that slot's
 * material with a fresh UMaterialInstanceConstant parented to Material; scalar/vector/texture
 * parameters from the existing slot MIC are copied across so per-character textures (e.g.
 * synthesised face basecolor/normal/cavity) survive the swap.
 *
 * Slots without an entry, or whose entry has Material == nullptr, keep their source MetaHuman
 * Character material untouched.
 */
USTRUCT()
struct FMetaHumanCrowdFaceMaterialOverride
{
	GENERATED_BODY()

	/** Slot name on the face skeletal mesh. */
	UPROPERTY(EditAnywhere, Category = "Face")
	FName SlotName;

	/**
	 * Override parent material for the actor face variant. Reads color from MIC scalar
	 * parameters (driven by per-actor MIDs at runtime). None = leave the slot alone.
	 */
	UPROPERTY(EditAnywhere, Category = "Face")
	TSoftObjectPtr<UMaterialInterface> ActorMaterial;

	/**
	 * Override parent material for the instanced (ISKM) face variant. Reads color from
	 * per-instance custom data (written into the face ISKM's custom-data buffer at
	 * AssembleCollection time). None = leave the slot alone.
	 */
	UPROPERTY(EditAnywhere, Category = "Face")
	TSoftObjectPtr<UMaterialInterface> InstancedMaterial;

	/**
	 * Per-instance custom-data offsets for parameters that drive this face slot's material.
	 */
	UPROPERTY(EditAnywhere, Category = "Face")
	TMap<FName, FMetaHumanCrowdOutfitCustomDataFormat> InstanceParameterNameToCustomDataFormat;
};

USTRUCT()
struct FMetaHumanCrowdActorLODSettings
{
	GENERATED_BODY()

	/** The index of the source LOD on the original MetaHuman mesh to pull this LOD's geometry from	 */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	int32 SourceLOD = 0;
};

USTRUCT()
struct FMetaHumanCrowdInstancedLODSettings
{
	GENERATED_BODY()

	/** The index of the source LOD on the original MetaHuman mesh to pull this LOD's geometry from */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	int32 SourceLOD = 0;

	/**
	 * Screen size at which this LOD becomes active when Nanite is not enabled.
	 * 
	 * Nanite always uses the top LOD and scales down procedurally from there.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	FPerPlatformFloat ScreenSize = FPerPlatformFloat(1.0f);
};

UCLASS(EditInlineNew)
class UMetaHumanCrowdEditorPipeline : public UMetaHumanCollectionEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdEditorPipeline();

	virtual void BuildCollection(const FBuildCollectionParams& Params, const FOnBuildComplete& OnComplete) const override;

	virtual bool CanBuild() const override;

	virtual bool TryUnpackInstanceAssets(
		TNotNull<UMetaHumanInstance*> Instance,
		FInstancedStruct& AssemblyOutput,
		TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
		const FString& TargetFolder) const override;

	virtual TSubclassOf<AActor> GetEditorActorClass() const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;

	virtual EMetaHumanWardrobeItemCompatibility TestWardrobeItemCompatibilityWithSlot(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem) const override;

	virtual bool TryCreateItemForImport(
		TNotNull<UMetaHumanCollection*> Collection,
		FName SlotName,
		TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem,
		FMetaHumanCharacterPaletteItem& OutItem) override;

	virtual bool ValidateCollection(TNotNull<UMetaHumanCollection*> Collection) override;

	/** LOD settings for actor face and groom meshes. */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	TArray<FMetaHumanCrowdActorLODSettings> ActorFaceLODs;

	/** LOD settings for actor body and clothing meshes. */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	TArray<FMetaHumanCrowdActorLODSettings> ActorBodyLODs;

	/** Per-platform MinLOD applied to every actor skeletal mesh produced */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	FPerPlatformInt ActorMeshMinLOD;

	/**
	 * Per-quality-level MinLOD applied to every actor skeletal mesh produced.
	 *
	 * Note that this is only used if the Use Skeletal Mesh Min LOD Per Quality Levels project setting is enabled.
	 */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	FPerQualityLevelInt ActorMeshQualityLevelMinLOD;

	/**
	 * Recompute tangents will be enabled using the skin cache on actor face LODs whose index is 
	 * less than or equal to this threshold. 
	 * 
	 * In other words, this is the index of the worst LOD where it will be enabled.
	 * 
	 * Note that this is a LOD index on the generated actor mesh, regardless of which source LODs
	 * it was generated from.
	 * 
	 * Set to a negative value to disable on all LODs.
	 */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	int32 FaceRecomputeTangentsLODThreshold = 1;

	/**
	 * The post-process anim blueprint that animates the face will be enabled on actor face LODs 
	 * whose index is less than or equal to this threshold. 
	 * 
	 * In other words, this is the index of the worst LOD where it will be enabled.
	 * 
	 * Note that this is a LOD index on the generated actor mesh, regardless of which source LODs
	 * it was generated from.
	 * 
	 * Set to a negative value to disable on all LODs.
	 */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	int32 FacialAnimationLODThreshold = 1;

	/**
	 * The post-process anim blueprint that applies correctives to the body, improving volume 
	 * preservation for example, will be enabled on actor body LODs whose index is less than or 
	 * equal to this threshold. 
	 * 
	 * In other words, this is the index of the worst LOD where it will be enabled.
	 * 
	 * Note that this is a LOD index on the generated actor mesh, regardless of which source LODs
	 * it was generated from.
	 * 
	 * Set to a negative value to disable on all LODs.
	 */
	UPROPERTY(EditAnywhere, Category = "Actor Meshes")
	int32 BodyCorrectivesLODThreshold = 0;

	/** LOD settings for instanced face meshes */
	UPROPERTY(EditAnywhere, Category = "Instanced Meshes")
	TArray<FMetaHumanCrowdInstancedLODSettings> InstancedFaceLODs;

	/** LOD settings for instanced body meshes */
	UPROPERTY(EditAnywhere, Category = "Instanced Meshes")
	TArray<FMetaHumanCrowdInstancedLODSettings> InstancedBodyLODs;

	/** Per-platform MinLOD applied to every instanced skeletal mesh produced. */
	UPROPERTY(EditAnywhere, Category = "Instanced Meshes")
	FPerPlatformInt InstancedMeshMinLOD;

	/**
	 * Per-quality-level MinLOD applied to every instanced skeletal mesh produced.
	 *
	 * Note that this is only used if the Use Skeletal Mesh Min LOD Per Quality Levels project setting is enabled.
	 */
	UPROPERTY(EditAnywhere, Category = "Instanced Meshes")
	FPerQualityLevelInt InstancedMeshQualityLevelMinLOD;

	/** Whether to include the teeth mesh section in instanced meshes. */
	UPROPERTY(EditAnywhere, Category = "Instanced Meshes")
	bool bKeepTeethInInstancedMeshes = false;

	/** Whether to remove hidden body geometry according to the hidden face maps of the clothing items */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	bool bApplyHiddenFaceMaps = true;

	UPROPERTY(EditAnywhere, Category = "Crowd")
	bool bEnableNaniteOnInstancedMeshes = true;

	/** The dataflow asset used to resize outfits to fit different body proportions */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	TObjectPtr<UDataflow> OutfitResizeDataflowAsset;

	/**
	 * The skeleton to bind all meshes to.
	 *
	 * The Instanced Skinned Mesh Component requires meshes to use the same skeleton as the animation 
	 * playing on them.
	 *
	 * This skeleton should be in the project or a project plugin, because some unrelated actions, such
	 * as importing a new animation, can modify the skeleton and it's good practise to avoid modifying
 	 * assets provided with the engine.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	TObjectPtr<USkeleton> TargetSkeleton;

	/**
	 * The skeleton that TargetSkeleton will be copied from during ValidateCollection, if the user
	 * chooses to create a new skeleton.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crowd")
	TSoftObjectPtr<USkeleton> TargetSkeletonTemplate;

	/**
	 * Animation configuration asset defining which animations to bake and merge for this crowd collection.
	 * Each entry produces one merged UAnimSequence per head character, on the shared TargetSkeleton.
	 * Two input modes are supported: a face + body pair that gets merged here (face curves are baked
	 * through the head mesh post-process ABP, then merged with body bone tracks), or a pre-merged
	 * sequence that is duplicated and rebound to TargetSkeleton.
	 * If not set, no animation baking is performed during BuildCollection.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	TObjectPtr<UMetaHumanCrowdAnimationConfig> AnimationConfig;

	/**
	 * Bone compression settings for the procedurally-baked animations produced during
	 * BuildCollection. If null, the engine's animation-recorder preset is used.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crowd", meta = (ForceShowEngineContent))
	TObjectPtr<class UAnimBoneCompressionSettings> BoneCompressionSettingsOverride;

	/**
	 * Bones that should remain in the reference skeleton of body, outfit, and groom meshes
	 * even when no geometry is weighted to them. Keeps the bone hierarchy consistent across
	 * every variant the pipeline produces (instanced and actor) so ISKM playback (which
	 * skips runtime retargeting) lines up with ABP playback.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crowd")
	TArray<FName> BodyForceKeepBones = { TEXT("neck_01"), TEXT("neck_02"), TEXT("head") };

	/**
	 * Bone used to align the head and body skeletons. Set it to the common bone where
	 * the two skeletons should align (typically 'head' on standard MetaHumans).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Crowd")
	FName HeadAlignmentBoneName = TEXT("head");

	/**
	 * Per-slot overrides for face skeletal mesh materials.
	 * 
	 * Use this to swap in crowd-specific shading parents (e.g. baked-groom face material, lower
	 * cost teeth/eye materials) while preserving per-character parameters from the source MICs.
	 */
	UPROPERTY(EditAnywhere, Category = "Face")
	TArray<FMetaHumanCrowdFaceMaterialOverride> FaceMaterialOverrides;

	/**
	 * Material applied to groom skeletal-mesh slots at face LODs >= the groom's GroomTextureMinLOD.
	 * Renders nothing (transparent/masked) so the baked-groom texture on the face material can
	 * take over without competing geometry. Required when any groom item has a BakedGroomTexture
	 * set; if null, no LOD-based masking happens and cards keep rendering at high LODs.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Face")
	TSoftObjectPtr<UMaterialInterface> InvisibleMaterial;

	/** Pipeline class used for imported groom wardrobe items. */
	UPROPERTY(EditDefaultsOnly, Category = "Item Pipelines")
	TSubclassOf<UMetaHumanCrowdGroomPipeline> GroomPipelineClass;

	/** Pipeline class used for imported outfit wardrobe items. */
	UPROPERTY(EditDefaultsOnly, Category = "Item Pipelines")
	TSubclassOf<UMetaHumanCrowdOutfitPipeline> OutfitPipelineClass;

	/** Pipeline class used for imported skeletal mesh clothing wardrobe items. */
	UPROPERTY(EditDefaultsOnly, Category = "Item Pipelines")
	TSubclassOf<UMetaHumanCrowdSkeletalClothingPipeline> SkeletalClothingPipelineClass;


private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
