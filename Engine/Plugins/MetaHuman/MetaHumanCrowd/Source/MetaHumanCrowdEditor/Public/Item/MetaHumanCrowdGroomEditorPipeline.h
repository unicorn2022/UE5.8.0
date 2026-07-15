// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanCrowdGroomEditorPipeline.generated.h"

class UMetaHumanCharacter;
class USkeletalMesh;
class USkeleton;
class UMaterialInterface;
class UTexture;

USTRUCT()
struct FMetaHumanCrowdGroomFitTarget
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UMetaHumanCharacter> OptionalCharacter;
	
	UPROPERTY()
	TObjectPtr<USkeletalMesh> TargetMesh;
};

USTRUCT()
struct FMetaHumanCrowdGroomBuildInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomFitTarget> FitTargets;

	UPROPERTY()
	TArray<int32> ActorFaceLODs;

	UPROPERTY()
	TArray<int32> InstancedFaceLODs;

	/**
	 * Pipeline slot the groom item lives in (e.g. "Hair", "Eyebrows", "Beard", "Mustache").
	 * Used as a parameter-name prefix when binding the groom's BakedGroomTexture and runtime
	 * colour parameters to the face material (e.g. "HairAttributeMap", "BeardMelanin").
	 */
	UPROPERTY()
	FName PipelineSlotName;
};

/**
 * Method for copying skin weights from source to target mesh.
 *
 * Note: this setting only applies fully to hair-card LODs. Helmet LODs (sourced from
 * FHairGroupsMeshesSourceDescription) are solid hair caps rather than strands, so the
 * pipeline only honours None vs. SingleBone for them: if you pick None, helmets get no
 * skin weights; otherwise (PerVertex / StrandBased / SingleBone) helmets are forced to
 * SingleBone (every helmet vertex parented to TargetBoneName).
 */
UENUM(BlueprintType)
enum class EMetaHumanCrowdGroomSkinWeightCopyMethod : uint8
{
	/** Do not copy any weights */
	None,

	/**
	 * Each vertex finds closest point on source surface, interpolates weights
	 * (smooth, independent per-vertex). When TargetBoneName is specified, only source
	 * triangles influenced by that bone or its descendants are considered (same filtering
	 * as StrandBased; prevents long hair from picking up shoulder/spine weights).
	 */
	PerVertex,

	/**
	 * Identify strands via connected components, copy anchor weights to entire strand.
	 * When TargetBoneName is specified, only source triangles influenced by that bone
	 * or its descendants are considered for anchor matching (prevents long hair from
	 * picking up shoulder/spine weights). */
	StrandBased,

	/** All vertices assigned to a single target bone with weight 1.0 */
	SingleBone
};

/** Parameters for converting groom hair cards to skinned skeletal meshes. */
USTRUCT()
struct FGroomCardConversionParams
{
	GENERATED_BODY()

	/** Method used to copy skin weights from source mesh to converted cards. */
	UPROPERTY(EditAnywhere, Category = "Groom")
	EMetaHumanCrowdGroomSkinWeightCopyMethod SkinWeightMethod = EMetaHumanCrowdGroomSkinWeightCopyMethod::StrandBased;

	/** Minimum weight threshold for bone influences. Weights below this value are discarded. */
	UPROPERTY(EditAnywhere, Category = "Groom")
	float BlendWeightsThreshold = 0.002f;

	/**
	 * Material slot names to exclude from source mesh when finding closest triangles.
	 * Useful for excluding geometry like eyelashes that would cause incorrect weight transfer.
	 */
	UPROPERTY(EditAnywhere, Category = "Groom")
	TArray<FName> ExcludedMaterialSlotNames;

	/**
	 * Target bone used by multiple skin weight methods:
	 *  - SingleBone: All vertices are assigned to this bone with weight 1.0
	 *  - StrandBased / PerVertex: Only source triangles influenced by this bone or its
	 *    descendants are considered for closest-point matching (prevents long hair from
	 *    picking up body weights).
	 */
	UPROPERTY(EditAnywhere, Category = "Groom")
	FName TargetBoneName;
};

UCLASS(EditInlineNew)
class UMetaHumanCrowdGroomEditorPipeline : public UMetaHumanItemEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdGroomEditorPipeline();

	virtual UE::Tasks::TTask<FMetaHumanPaletteBuiltData> BuildItem(const FBuildItemParams& Params) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;

	UPROPERTY(EditAnywhere, Category = "Groom")
	TArray<TSoftObjectPtr<UMetaHumanCharacter>> CompatibleHeads;

	/** Parent material used for hair cards. Card textures will be applied to this material's parameters. */
	UPROPERTY(EditAnywhere, Category = "Groom")
	TSoftObjectPtr<UMaterialInterface> CardsMaterial;

	/**
	 * Parent material used for helmet (solid hair cap) LODs. Same texture parameters as
	 * CardsMaterial, but the difference is that the helmet variant authoring sets up the shading
	 * graph for solid-cap rendering rather than strand sampling.
	 */
	UPROPERTY(EditAnywhere, Category = "Groom")
	TSoftObjectPtr<UMaterialInterface> HelmetsMaterial;

	/** Parameters controlling how groom hair cards are converted to skinned skeletal meshes. */
	UPROPERTY(EditAnywhere, Category = "Groom")
	FGroomCardConversionParams CardConversionParams;

	/**
	 * Pre-authored "baked groom" texture used as a face-material overlay on LODs that have no
	 * card or helmet geometry. Bound to the face MIC's "{PipelineSlotName}AttributeMap"
	 * parameter (e.g. "HairAttributeMap" for grooms in the Hair slot). Lets strands-only
	 * grooms (buzzcut, eyebrows, etc.) still render on the face when the crowd pipeline
	 * doesn't carry strand renderers.
	 *
	 * If unset, falls back to the value from the source groom's UMetaHumanDefaultGroomPipeline
	 * (the runtime pipeline's SourceGroomItem) when one is configured, so existing groom WIs
	 * work without per-asset duplication.
	 */
	UPROPERTY(EditAnywhere, Category = "Groom")
	TSoftObjectPtr<UTexture> BakedGroomTexture;

	/**
	 * First face LOD at which the baked-groom texture is bound to the face material. Cards/helmets
	 * cover lower LODs (< GroomTextureMinLOD); the baked texture takes over from GroomTextureMinLOD
	 * upward. Cards/helmets at masked-out LODs render with a transparent material so they don't
	 * compete with the baked texture.
	 *
	 * `-1` (default) falls back to the value from the source groom's UMetaHumanDefaultGroomPipeline
	 * (the runtime pipeline's SourceGroomItem) when one is configured; otherwise the threshold
	 * becomes `0` (baked covers every face LOD).
	 *
	 * `N` (any non-negative value) reserves face LODs `0..N-1` for cards/helmet rendering and
	 * switches to the baked texture for face LODs `>= N`.
	 */
	UPROPERTY(EditAnywhere, Category = "Groom", meta = (ClampMin = -1))
	int32 GroomTextureMinLOD = -1;

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
