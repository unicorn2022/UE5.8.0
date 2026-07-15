// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Item/MetaHumanGroomPipeline.h"
#include "Item/MetaHumanSkeletalMeshPipeline.h"
#include "Item/MetaHumanOutfitPipeline.h"
#include "MetaHumanCharacterGeneratedAssets.h"
#include "MetaHumanCollectionPipeline.h"

#include "ChaosOutfitAsset/OutfitAsset.h"
#include "Misc/NotNull.h"
#include "Templates/SubclassOf.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakObjectPtrFwd.h"

#include "MetaHumanDefaultPipelineBase.generated.h"

#define UE_API METAHUMANDEFAULTPIPELINE_API

class AActor;
class ITargetPlatform;
class UMetaHumanInstance;
class UMetaHumanCharacterPalette;

USTRUCT()
struct FMetaHumanCharacterPartOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FMetaHumanCharacterGeneratedAssets GeneratedAssets;
};

USTRUCT(BlueprintType)
struct FMetaHumanDefaultAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class USkeletalMesh> FaceMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TObjectPtr<class USkeletalMesh> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	FMetaHumanGroomPipelineAssemblyOutput Hair;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	FMetaHumanGroomPipelineAssemblyOutput Eyebrows;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	FMetaHumanGroomPipelineAssemblyOutput Beard;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	FMetaHumanGroomPipelineAssemblyOutput Mustache;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	FMetaHumanGroomPipelineAssemblyOutput Eyelashes;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	FMetaHumanGroomPipelineAssemblyOutput Peachfuzz;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TArray<FMetaHumanSkeletalMeshPipelineAssemblyOutput> SkeletalMeshData;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Build")
	TArray<FMetaHumanOutfitPipelineAssemblyOutput> ClothData;
};

/**
 * The common base class for the current and legacy default MetaHuman pipelines.
 * 
 * Contains shared functionality for building simple MetaHumans.
 */
UCLASS(Abstract, MinimalAPI, meta=(MetaHumanCreatorOnly))
class UMetaHumanDefaultPipelineBase : public UMetaHumanCollectionPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanDefaultPipelineBase();

	virtual void AssembleCollection(const FAssembleCollectionParams& Params, const FOnAssemblyComplete& OnComplete) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override
	{
		return Specification;
	}

	virtual const UMetaHumanItemPipeline* GetFallbackItemPipelineForAssetType(const TSoftClassPtr<UObject>& InAssetClass) const override;

	UE_API static const FName OutfitsSlotName;
	UE_API static const FName TopGarmentSlotName;
	UE_API static const FName BottomGarmentSlotName;
	UE_API static const FName SkeletalMeshSlotName;

private:
	/** The specification that this pipeline implements */
	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;

	/** Default item pipeline per asset type. Used if wardrobe item doesn't have a pipeline specified */
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<TSoftClassPtr<UObject>, TSubclassOf<UMetaHumanItemPipeline>> DefaultAssetPipelines;
};

/**
 * Pipeline functionality extender.
 * Extenders can e.g. be registered from external plugins, modifying the blueprint after the base pipeline
 * depending on if the plugin is enabled or not.
 */
class IMetaHumanCharacterPipelineExtender : public IModularFeature
{
public:
	virtual ~IMetaHumanCharacterPipelineExtender() = default;
	static inline const FName FeatureName = TEXT("MetaHumanCharacterPipelineExtender");

	virtual TSubclassOf<AActor> GetOverwriteBlueprint(EMetaHumanQualityLevel QualityLevel, FName AnimationSystemName) const { return {}; }

	/** Additional modifications called after the base pipeline blueprint modification. */
	virtual void ModifyBlueprint(TNotNull<UBlueprint*> InBlueprint) {}
};

#undef UE_API
