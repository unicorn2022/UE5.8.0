// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"

#include "Item/MetaHumanMaterialPipelineCommon.h"

#include "MetaHumanGroomPipeline.generated.h"

struct FHairGroupsMaterial;
class UGroomBindingAsset;
class UGroomComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMesh;

USTRUCT()
struct FMetaHumanGroomPipelineBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<TObjectPtr<UGroomBindingAsset>> Bindings;

	UPROPERTY()
	bool bRequiresBinding = true;
};

USTRUCT()
struct FMetaHumanGroomPipelineAssemblyInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const USkeletalMesh> TargetMesh;
};

USTRUCT(BlueprintType)
struct METAHUMANDEFAULTPIPELINE_API FMetaHumanGroomPipelineAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Groom")
	TObjectPtr<UGroomBindingAsset> Binding;

	// Key is slot name
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Groom")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;

	// Key is slot name, face mesh skin MIDs for baked groom color param updates
	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> FaceMeshOverrideMaterials;
};

UCLASS(Blueprintable, EditInlineNew)
class METAHUMANDEFAULTPIPELINE_API UMetaHumanGroomPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanGroomPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;

	virtual void SetPostAssemblyParameters(const FSetPostAssemblyParametersParams& Params, FInstancedStruct& InOutItemAssemblyOutput) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Pipeline")
	static void ApplyGroomAssemblyOutputToGroomComponent(const FMetaHumanGroomPipelineAssemblyOutput& GroomAssemblyOutput, UGroomComponent* GroomComponent);

	// Internal functions to reduce implementation boilerplate
	static UE::MetaHuman::MaterialUtils::FFetchSlotNameDelegate MakeFetchSlotNameDelegate(TConstArrayView<FHairGroupsMaterial> HairMaterials);
	static UE::MetaHuman::MaterialUtils::FFetchSlotMaterialDelegate MakeFetchSlotMaterialDelegate(TConstArrayView<FHairGroupsMaterial> HairMaterials);

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TMap<FName, TObjectPtr<UMaterialInterface>> OverrideMaterials;

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TArray<FMetaHumanMaterialParameter> RuntimeMaterialParameters;

protected:
	// Allows pipeline to override default material values before they're initialized
	virtual void OverrideInitialMaterialValues(TNotNull<UMaterialInstanceDynamic*> InMID, FName InSlotName) const {}

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanDefaultEditorPipeline.MetaHumanGroomEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
