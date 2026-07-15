// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanCrowdTypes.h"

#include "Item/MetaHumanMaterialPipelineCommon.h"

#include "MetaHumanCrowdOutfitPipeline.generated.h"

struct FPrimitiveInstanceId;
class USkeletalMesh;

USTRUCT()
struct FMetaHumanCrowdOutfitBuildOutput
{
	GENERATED_BODY()

public:
	// This data is to be consumed by the Collection pipeline and not stored in the Collection built data

	/** Keyed by body item key -> geometry bundle for the fitted outfit. */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanCrowdMeshGeometryBundle> BodyToOutfitGeometryMap;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdOutfitAssemblyInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanPaletteItemKey BodyItem;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdOutfitCustomDataFormat
{
	GENERATED_BODY()

public:
	/**
	 * Offset into the ISKM's per-instance custom-data float buffer.
	 *
	 * The custom-data buffer is shared across every material slot on a single ISKM, so the
	 * offsets authored here must not overlap each other across slots on the same mesh.
	 */
	UPROPERTY(EditAnywhere, Category = "Materials")
	int32 CustomDataOffset = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = "Materials")
	uint8 bUseChannelR : 1 = true;
	
	UPROPERTY(EditAnywhere, Category = "Materials")
	uint8 bUseChannelG : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Materials")
	uint8 bUseChannelB : 1 = true;

	UPROPERTY(EditAnywhere, Category = "Materials")
	uint8 bUseChannelA : 1 = true;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdOutfitInstancedMaterial
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Materials")
	TObjectPtr<UMaterialInterface> InstancedComponentMaterial;

	UPROPERTY(EditAnywhere, Category = "Materials")
	TMap<FName, FMetaHumanCrowdOutfitCustomDataFormat> InstanceParameterNameToCustomDataFormat;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdOutfitAssemblyOutput
{
	GENERATED_BODY()

public:
	/** MID overrides for actor mesh components, keyed by material slot name. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInterface>> MeshComponentOverrideMaterials;

	/** Per-slot ISKM material binding for instanced meshes. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInterface>> InstancedMaterialData;

	/**
	 * ISKM-wide per-instance custom data float buffer.
	 *
	 * Sized once during assembly to fit every slot's authored offsets. Slots write into this via
	 * absolute CustomDataOffset values that must not overlap across slots on the same ISKM.
	 */
	UPROPERTY()
	TArray<float> InstancedMeshCustomDataFloats;
};

UCLASS(Blueprintable, EditInlineNew)
class UMetaHumanCrowdOutfitPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdOutfitPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;
	
	virtual void SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;
	
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<const UMetaHumanWardrobeItem> SourceOutfitItem;

	// Key is material slot name
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<FName, FMetaHumanCrowdOutfitInstancedMaterial> InstancedComponentOverrideMaterials;

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanCrowdEditor.MetaHumanCrowdOutfitEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
