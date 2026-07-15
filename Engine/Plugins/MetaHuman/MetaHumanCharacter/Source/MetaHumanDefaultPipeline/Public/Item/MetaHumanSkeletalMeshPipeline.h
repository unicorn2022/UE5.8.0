// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanGeometryRemovalTypes.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"

#include "Item/MetaHumanMaterialPipelineCommon.h"

#include "MetaHumanSkeletalMeshPipeline.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMesh;
class UAnimBlueprint;

USTRUCT()
struct FMetaHumanSkeletalMeshPipelineBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMap;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;
};

USTRUCT(BlueprintType)
struct METAHUMANDEFAULTPIPELINE_API FMetaHumanSkeletalMeshPipelineAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> SkelMesh;

	UPROPERTY()
	TSoftObjectPtr<UAnimBlueprint> AnimBlueprintToUse;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture HeadHiddenFaceMap;

	UPROPERTY()
	UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture BodyHiddenFaceMap;

	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;
};

UCLASS(Blueprintable, EditInlineNew)
class METAHUMANDEFAULTPIPELINE_API UMetaHumanSkeletalMeshPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanSkeletalMeshPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;

	virtual void SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline")
	static void ApplySkeletalMeshAssemblyOutputToSkeletalMeshComponent(
		const FMetaHumanSkeletalMeshPipelineAssemblyOutput& InAssemblyOutput,
		USkeletalMeshComponent* InComponent,
		USkeletalMeshComponent* InLeaderComponent);

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TArray<FMetaHumanMaterialParameter> RuntimeMaterialParameters;

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TSoftObjectPtr<UAnimBlueprint> AnimBlueprintToUse;

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanDefaultEditorPipeline.MetaHumanSkeletalMeshEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
