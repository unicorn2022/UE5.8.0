// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"

#include "MetaHumanCrowdHeadPipeline.generated.h"

class UDNA;
class UAnimSequenceTransformProviderData;
class UMetaHumanCharacter;
class USkeletalMesh;

USTRUCT()
struct FMetaHumanCrowdHeadBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> HeadMesh;

	UPROPERTY()
	TObjectPtr<UAnimSequenceTransformProviderData> BakedAnimRootProvider;

};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdHeadAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> HeadMesh;

	UPROPERTY()
	TObjectPtr<UAnimSequenceTransformProviderData> BakedAnimRootProvider;

};

UCLASS(Blueprintable, EditInlineNew)
class UMetaHumanCrowdHeadPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdHeadPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;
	
	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TSoftObjectPtr<UMetaHumanCharacter> CompatibleBody;

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanCrowdEditor.MetaHumanCrowdHeadEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
