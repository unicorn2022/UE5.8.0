// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanCrowdTypes.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "Item/MetaHumanCrowdOutfitPipeline.h"

#include "MetaHumanCrowdGroomPipeline.generated.h"

class UGroomBindingAsset;
class UMaterialInterface;
class USkeletalMesh;
class UTexture;
class FObjectPreSaveContext;

/**
 * Pre-authored baked groom texture for a single (groom, head) pair, plus the wardrobe slot the
 * groom lives in. The collection pipeline binds this onto the head's face material(s) at the
 * face LODs listed in AppliesAtFaceLODs.
 */
USTRUCT()
struct FMetaHumanCrowdGroomBakedTextureEntry
{
	GENERATED_BODY()

	/** Pre-authored baked groom texture, bound onto the face material's "{WardrobeSlot}AttributeMap" parameter. */
	UPROPERTY()
	TObjectPtr<UTexture> Texture;

	/** Pipeline slot the groom lives in (e.g. "Hair", "Beard"); used as a parameter-name prefix. */
	UPROPERTY()
	FName PipelineSlotName;

	/** Face-LOD indices at which the baked texture is bound. */
	UPROPERTY()
	TArray<int32> AppliesAtFaceLODs;

	/** The effective GroomTextureMinLOD threshold. */
	UPROPERTY()
	int32 GroomTextureMinLOD = INT32_MAX;
};

/**
 * Per-variant material override carried alongside the shared geometry bundle.
 */
USTRUCT()
struct FMetaHumanCrowdGroomMaterialOverride
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSkeletalMaterial> Materials;

	// Per-LOD polygon-group-ID -> Materials-index map.
	TArray<TArray<int32>> LODMaterialMaps;
};

USTRUCT()
struct FMetaHumanCrowdGroomBuildOutput
{
	GENERATED_BODY()

public:
	/**
	 * Per-(groom, head) shared geometry. The bundle's own Materials/LODMaterialMaps describe
	 * the actor variant's MICs (named for actor output-LOD positions, parented to
	 * CardsMaterial or HelmetsMaterial as appropriate). Runtime AssembleItem reads these
	 * directly when generating MIDs for the actor mesh.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle> ItemToGroomGeometryMap;

	/**
	 * Per-key override of the bundle's Materials/LODMaterialMaps for the instanced variant.
	 * Geometry is shared with ItemToGroomGeometryMap; only the material set differs (each MIC
	 * named for instanced output-LOD positions, ensuring no MIC is shared between actor and
	 * instanced skeletal meshes). Keys parallel ItemToGroomGeometryMap, populated in lockstep.
	 *
	 * Note: FMetaHumanCrowdGroomMaterialOverride::LODMaterialMaps is non-UPROPERTY (matches
	 * FMetaHumanCrowdMeshGeometryBundle::LODMaterialMaps); won't survive reflection-based serialization.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomMaterialOverride> InstancedMaterialOverrides;

	/**
	 * Per-(groom, head) baked groom texture entries, populated when the editor groom pipeline
	 * has BakedGroomTexture set and at least one face LOD has no card/helmet geometry. Consumed
	 * by the collection pipeline when constructing face MICs.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdGroomBakedTextureEntry> ItemToBakedGroomTexture;

	/** Source groom asset name, used for friendly mesh naming. */
	UPROPERTY()
	FString GroomAssetName;

	/** Pipeline slot the groom lives in (e.g. "Hair", "Beard"). */
	UPROPERTY()
	FName PipelineSlotName;
};

USTRUCT()
struct FMetaHumanCrowdGroomAssemblyInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemKey TargetItem;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdGroomAssemblyOutput
{
	GENERATED_BODY()

public:
	/** MID overrides for actor mesh components, keyed by material slot name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;

	/** Per-slot ISKM material binding for instanced meshes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TMap<FName, TObjectPtr<UMaterialInterface>> InstancedMaterialData;

	/**
	 * ISKM-wide per-instance custom data float buffer.
	 *
	 * Sized once during assembly to fit every slot's authored offsets. Slots write into this via
	 * absolute CustomDataOffset values that must not overlap across slots on the same ISKM.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TArray<float> InstancedMeshCustomDataFloats;
};

UCLASS()
class UMetaHumanCrowdGroomPipelineMaterialParameters : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (MaterialParamName = "Melanin", Min = 0.0f, UIMin = 0.0f, Max = 1.0f, UIMax = 1.0f))
	float Melanin = 0.16f;

	UPROPERTY(meta = (MaterialParamName = "Redness", Min = 0.0f, UIMin = 0.0f, Max = 1.0f, UIMax = 1.0f))
	float Redness = 0.0f;

	UPROPERTY(meta = (MaterialParamName = "Roughness", Min = 0.0f, UIMin = 0.0f, Max = 1.0f, UIMax = 1.0f))
	float Roughness = 0.25f;

	UPROPERTY(meta = (MaterialParamName = "WhiteAmount", Min = 0.0f, UIMin = 0.0f, Max = 1.0f, UIMax = 1.0f))
	float Whiteness = 0.0f;

	UPROPERTY(meta = (MaterialParamName = "LightAmount", Min = 0.0f, UIMin = 0.0f, Max = 1.0f, UIMax = 1.0f))
	float Lightness = 0.0f;

	UPROPERTY(meta = (MaterialParamName = "DyeColor"))
	FLinearColor DyeColor = FLinearColor::White;
};

UCLASS(Blueprintable, EditInlineNew)
class UMetaHumanCrowdGroomPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdGroomPipeline();
	
	//~ Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	//~ End UObject interface

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;
	
	virtual void SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const override;
	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<const UMetaHumanWardrobeItem> SourceGroomItem;

	// Key is material slot name
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial> InstancedComponentOverrideMaterials;

#if WITH_EDITORONLY_DATA
	/**
	 * When enabled, RuntimeMaterialParameters array can be edited manually. When disabled, the array is
	 * automatically populated from the declared material parameter properties and any manual edits are disabled.
	 */
	UPROPERTY(EditAnywhere, Category = "Material")
	bool bUseCustomRuntimeMaterialParameters = false;
#endif

	UPROPERTY(EditAnywhere, Category = "Material", meta = (EditCondition = "bUseCustomRuntimeMaterialParameters"))
	TArray<FMetaHumanMaterialParameter> RuntimeMaterialParameters;

private:
#if WITH_EDITOR
	void UpdateParameters();
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanCrowdEditor.MetaHumanCrowdGroomEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
