// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"

#include "MetaHumanOutfitPipeline.generated.h"

class UChaosClothComponent;
class UChaosOutfitAsset;
class USkeletalMesh;
class USkinnedAsset;

USTRUCT()
struct FMetaHumanOutfitGeneratedAssets
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UChaosOutfitAsset> Outfit;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> OutfitMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> CombinedBodyMesh;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMap;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;

	// The source sizes of the outfit that were available
	UPROPERTY()
	TArray<FName> AvailableSourceSizes;

	// The source size of the outfit that was auto-selected by the resizing graph
	UPROPERTY()
	FName AutoSelectedSourceSize;
};

USTRUCT()
struct FMetaHumanOutfitPipelineBuildOutput
{
	GENERATED_BODY()

public:
	/** 
	 * Map from Character item key to the fitted outfit for that Character.
	 * 
	 * If the outfit can't be fitted, this will just be a reference to the original outfit asset.
	 */
	UPROPERTY()
	TMap<FMetaHumanPaletteItemKey, FMetaHumanOutfitGeneratedAssets> CharacterAssets;
};

USTRUCT()
struct FMetaHumanOutfitPipelineAssemblyInput
{
	GENERATED_BODY()

public:
	
	UPROPERTY()
	FMetaHumanPaletteItemKey SelectedCharacter;
};

USTRUCT(BlueprintType)
struct FMetaHumanOutfitPipelineAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UChaosOutfitAsset> Outfit;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> OutfitMesh;

	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMap;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;
};

UCLASS(Blueprintable, EditInlineNew)
class METAHUMANDEFAULTPIPELINE_API UMetaHumanOutfitPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanOutfitPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;
	
	virtual void SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline")
	static void ApplyOutfitAssemblyOutputToClothComponent(const FMetaHumanOutfitPipelineAssemblyOutput& InOutfitAssemblyOutput, UChaosClothComponent* InClothComponent);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline")
	static void ApplyOutfitAssemblyOutputToMeshComponent(const FMetaHumanOutfitPipelineAssemblyOutput& InOutfitAssemblyOutput, USkeletalMeshComponent* InMeshComponent, bool bUpdateSkelMesh = false);

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TArray<FMetaHumanMaterialParameter> RuntimeMaterialParameters;

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanDefaultEditorPipeline.MetaHumanOutfitEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
