// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemPipeline.h"
#include "MetaHumanPaletteItemKey.h"

#include "MetaHumanCrowdCharacterPipeline.generated.h"

class UMetaHumanCharacter;
class USkeletalMesh;

USTRUCT()
struct FMetaHumanCrowdCharacterBuildOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> MergedHeadAndBodyMesh;

	UPROPERTY()
	TMap<FString, float> BodyMeasurements;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacter> CompatibleBody;
};

USTRUCT(BlueprintType)
struct FMetaHumanCrowdCharacterAssemblyOutput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<USkeletalMesh> FaceMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> MergedHeadAndBodyMesh;

	UPROPERTY()
	TMap<FString, float> BodyMeasurements;
};

UCLASS(Blueprintable, EditInlineNew)
class UMetaHumanCrowdCharacterPipeline : public UMetaHumanItemPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdCharacterPipeline();

#if WITH_EDITOR
	virtual void SetDefaultEditorPipeline() override;

	virtual const UMetaHumanItemEditorPipeline* GetEditorPipeline() const override;
#endif

	virtual void AssembleItem(const FAssembleItemParams& Params, const FOnAssemblyComplete& OnComplete) const override;

	virtual TNotNull<const UMetaHumanCharacterPipelineSpecification*> GetSpecification() const override;

private:
#if WITH_EDITOR
	TSubclassOf<UMetaHumanItemEditorPipeline> GetEditorPipelineClass() const;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, NoClear, Instanced, Category = "Pipeline", meta = (FullyExpand, AllowedClasses = "/Script/MetaHumanCrowdEditor.MetaHumanCrowdCharacterEditorPipeline"))
	TObjectPtr<UMetaHumanItemEditorPipeline> EditorPipeline;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterPipelineSpecification> Specification;
};
