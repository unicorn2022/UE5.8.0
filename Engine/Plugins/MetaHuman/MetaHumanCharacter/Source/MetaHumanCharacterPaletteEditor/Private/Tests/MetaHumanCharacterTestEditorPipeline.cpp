// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterTestEditorPipeline.h"

#include "MetaHumanCharacterEditorPipelineSpecification.h"
#include "MetaHumanWardrobeItem.h"
#include "Tests/MetaHumanCharacterTestPipeline.h"

#include "AssetRegistry/AssetData.h"

void UMetaHumanCharacterTestEditorPipeline::SetSpecification(UMetaHumanCharacterEditorPipelineSpecification* InSpecification)
{
	Specification = InSpecification;
}

void UMetaHumanCharacterTestEditorPipeline::BuildCollection(const FBuildCollectionParams& Params, const FOnBuildComplete& OnComplete) const
{
	// This function and some of the ones below are not used by tests yet, and so don't need an
	// implementation yet. They will be implemented if needed for testing.
	unimplemented();
}

bool UMetaHumanCharacterTestEditorPipeline::CanBuild() const
{
	return true;
}

bool UMetaHumanCharacterTestEditorPipeline::TryUnpackInstanceAssets(
	TNotNull<UMetaHumanInstance*> Instance,
	FInstancedStruct& AssemblyOutput,
	TArray<FMetaHumanGeneratedAssetMetadata>& AssemblyAssetMetadata,
	const FString& TargetFolder) const
{
	unimplemented();
	return false;
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanCharacterTestEditorPipeline::GetSpecification() const
{
	return Specification ? Specification.Get() : GetDefault<UMetaHumanCharacterEditorPipelineSpecification>();
}

TSubclassOf<AActor> UMetaHumanCharacterTestEditorPipeline::GetEditorActorClass() const
{
	unimplemented();
	return nullptr;
}
