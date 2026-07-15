// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassVisualizationTrait.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "MetaHumanMassCrowdVisualizationTrait.generated.h"

#define UE_API METAHUMANCROWD_API

class UMetaHumanInstance;
class UMetaHumanCrowdAppearanceProvider;

UCLASS(MinimalAPI, BlueprintType, EditInlineNew, CollapseCategories, meta=(DisplayName="MetaHuman Crowd Visualization"))
class UMetaHumanMassCrowdVisualizationTrait : public UMassVisualizationTrait
{
	GENERATED_BODY()
public:
	/** MetaHuman Instance descriptions to use. Entities will be assigned an instance to use for their appearance */
	UPROPERTY(EditAnywhere, Category = "Mass|Visual")
	TArray<TObjectPtr<UMetaHumanInstance>> CharacterInstances;

	/**
	 * Optional procedural appearance provider class.
	 * 
	 * This allows you to generate MetaHuman Instances procedurally at runtime.
	 * 
	 * If this is set, AcquireAppearance will be called on it before spawning each entity.
	 * 
	 * If CharacterInstances contains any Instances, they will be pre-registered so 
	 * AcquireAppearance can return handles to them without needing to register them first.
	 */
	UPROPERTY(EditAnywhere, Category = "Mass|Visual")
	TSubclassOf<UMetaHumanCrowdAppearanceProvider> AppearanceProviderClass;

	/** Number of steady-state (phase-offset) tracks per animation sequence.
	 *  More tracks = more phase variety in looping crowds, but more GPU memory.
	 *  Min 1 = full lockstep. */
	UPROPERTY(EditAnywhere, Category = "Mass|Animation Scalability", meta=(ClampMin="1", ClampMax="20"))
	int32 SteadyStateTracksPerSequence = 3;

	/** Maximum number of temporary blend tracks across all entities for this ISKM.
	 *  When exhausted, entities snap directly to steady-state (no blend). */
	UPROPERTY(EditAnywhere, Category = "Mass|Animation Scalability", meta=(ClampMin="0", UIMin="0", UIMax="100"))
	int32 MaxBlendTracks = 50;

	/** Duration in seconds of the crossfade blend when an entity changes animation sequence. */
	UPROPERTY(EditAnywhere, Category = "Mass|Animation Scalability", meta=(ClampMin="0.0", UIMin="0.0", UIMax="2.0"))
	float AnimBlendTime = 0.25f;

	UE_API UMetaHumanMassCrowdVisualizationTrait();

	UE_API virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	UE_API virtual void SanitizeMeshParams(FMassRepresentationParameters& InOutParams, const bool bStaticMeshDeterminedInvalid = false, const bool bSkinnedMeshDeterminedInvalid = false) const override;
};

#undef UE_API
