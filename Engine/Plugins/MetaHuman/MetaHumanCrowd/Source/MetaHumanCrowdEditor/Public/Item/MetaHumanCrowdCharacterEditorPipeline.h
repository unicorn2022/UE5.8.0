// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanItemEditorPipeline.h"

#include "MetaHumanCrowdCharacterEditorPipeline.generated.h"

class UMetaHumanCharacter;
class USkeletalMesh;

UENUM()
enum class EMetaHumanCrowdCharacterMeshRequirement : uint8
{
	/** The mesh is not needed and will not be generated. */
	NotNeeded,

	/** The mesh is needed but will not be modified by the caller. Pre-built meshes can be
	  * passed through without duplication. */
	ReadOnly,

	/** The mesh is needed and may be modified by the caller. Pre-built meshes will be
	  * duplicated so the original is not affected. */
	Modifiable
};

USTRUCT()
struct FMetaHumanCrowdCharacterBuildInput
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EMetaHumanCrowdCharacterMeshRequirement FaceMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::Modifiable;

	UPROPERTY()
	EMetaHumanCrowdCharacterMeshRequirement BodyMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::Modifiable;

	UPROPERTY()
	EMetaHumanCrowdCharacterMeshRequirement MergedHeadAndBodyMeshRequirement = EMetaHumanCrowdCharacterMeshRequirement::NotNeeded;

	UPROPERTY()
	bool bGenerateBodyMeasurements = false;
};

UCLASS(EditInlineNew)
class UMetaHumanCrowdCharacterEditorPipeline : public UMetaHumanItemEditorPipeline
{
	GENERATED_BODY()

public:
	UMetaHumanCrowdCharacterEditorPipeline();

	virtual UE::Tasks::TTask<FMetaHumanPaletteBuiltData> BuildItem(const FBuildItemParams& Params) const override;

	virtual TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> GetSpecification() const override;
	
	// Set these to meshes produced by the "Cinematic" assembly option in the MetaHuman Character
	// editor if you want to provide pre-built meshes instead of generating them at build time.
	// When set, the pipeline will use these meshes directly instead of calling
	// TryGenerateCharacterAssets. Leave unset (nullptr) to generate meshes on demand.

	// Pre-built face mesh from the MetaHuman Character editor's Cinematic assembly.
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<USkeletalMesh> FaceMesh;

	// Pre-built body mesh from the MetaHuman Character editor's Cinematic assembly.
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<USkeletalMesh> BodyMesh;

	// Pre-built merged head and body mesh from the MetaHuman Character editor's Cinematic assembly.
	// Body measurements will be read from the UChaosOutfitAssetBodyUserData attached to this mesh
	// if available.
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<USkeletalMesh> MergedHeadAndBodyMesh;

	/** 
	 * If this Character has been conformed to the body of another Character, assign the other
	 * Character to this property.
	 * 
	 * This lets the Collection pipeline know that the neck seams of these two Characters are 
	 * identical, and so the heads can safely be swapped.
	 */
	UPROPERTY(EditAnywhere, Category = "Pipeline")
	TObjectPtr<UMetaHumanCharacter> CompatibleBody;

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorPipelineSpecification> Specification;
};
