// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCollectionEditorPipeline.h"

#include "MetaHumanCharacterTestEditorPipeline.generated.h"

class UMetaHumanCharacterTestPipeline;

/**
 * A minimal pipeline used for automated tests, similar to a mock.
 */
UCLASS()
class UMetaHumanCharacterTestEditorPipeline : public UMetaHumanCollectionEditorPipeline
{
	GENERATED_BODY()

public:
	void SetSpecification(UMetaHumanCharacterEditorPipelineSpecification* InSpecification);

	// Begin UMetaHumanCharacterEditorPipeline interface
	virtual void BuildCollection(const FBuildCollectionParams& Params, const FOnBuildComplete& OnComplete) const override;

	virtual bool CanBuild() const override;

	virtual bool TryUnpackInstanceAssets(
		TNotNull<UMetaHumanInstance*> Instance,
		FInstancedStruct& AssemblyOutput,
		TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
		const FString& TargetFolder) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;

	virtual TSubclassOf<AActor> GetEditorActorClass() const override;
	// End UMetaHumanCharacterEditorPipeline interface

private:
	UPROPERTY(Transient)
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
