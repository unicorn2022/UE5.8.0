// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Mass/EntityElementTypes.h"
#include "Math/NumericLimits.h"
#include "Templates/SubclassOf.h"

#include "MetaHumanMassFragments.generated.h"

class UMetaHumanCrowdAppearanceProvider;

USTRUCT()
struct FMetaHumanAppearanceSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	/**
	 * Pre-baked array of appearance indices, populated from
	 * UMetaHumanMassCrowdVisualizationTrait::CharacterInstances.
	 * 
	 * Empty when the trait is using an AppearanceProviderClass instead.
	 */
	UPROPERTY(Transient)
	TArray<uint32> AssignedAppearanceIndices;

	UPROPERTY(Transient)
	uint32 CurrentIndex = 0;

	/** Optional procedural appearance provider */
	UPROPERTY(Transient)
	TSubclassOf<UMetaHumanCrowdAppearanceProvider> ProviderClass;
};

USTRUCT()
struct FMetaHumanMassIdentityFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Sentinel value for AppearanceIndex meaning "no appearance assigned". */
	static constexpr uint32 InvalidAppearanceIndex = TNumericLimits<uint32>::Max();

	UPROPERTY(Transient)
	uint32 AppearanceIndex = InvalidAppearanceIndex;
};

/**
 * Shared fragment for animation track pool scalability parameters.
 * Set by the visualization trait, consumed by processors and the visualization component.
 * A scalability system could adjust these at runtime to trade quality for performance.
 */
USTRUCT()
struct FMetaHumanMassAnimationScalabilitySharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	/** Number of steady-state (phase-offset) tracks per animation sequence.
	 *  More tracks = more phase variety in looping crowds, but more GPU memory.
	 *  Min 1 = full lockstep. */
	UPROPERTY(EditAnywhere, Category = "Animation|Scalability", meta=(ClampMin="1", ClampMax="20"))
	int32 SteadyStateTracksPerSequence = 3;

	/** Maximum number of temporary blend tracks across all entities for this ISKM.
	 *  When exhausted, entities snap directly to steady-state (no blend). */
	UPROPERTY(EditAnywhere, Category = "Animation|Scalability", meta=(ClampMin="0"))
	int32 MaxBlendTracks = 50;

	/** Duration in seconds of the crossfade blend when an entity changes animation sequence. */
	UPROPERTY(EditAnywhere, Category = "Animation|Scalability", meta=(ClampMin="0.0"))
	float AnimBlendTime = 0.25f;

	bool operator==(const FMetaHumanMassAnimationScalabilitySharedFragment& Other) const
	{
		return SteadyStateTracksPerSequence == Other.SteadyStateTracksPerSequence
			&& MaxBlendTracks == Other.MaxBlendTracks
			&& AnimBlendTime == Other.AnimBlendTime;
	}

	friend uint32 GetTypeHash(const FMetaHumanMassAnimationScalabilitySharedFragment& Fragment)
	{
		return HashCombine(
			HashCombine(GetTypeHash(Fragment.SteadyStateTracksPerSequence), GetTypeHash(Fragment.MaxBlendTracks)),
			GetTypeHash(Fragment.AnimBlendTime));
	}
};

/**
 * This fragment is only used for MH Crowd Perf. This way it avoids
 * any potential issues with traits that might read this data.
 *
 * We should consider using FMassMoveTargetFragment or similar
 * existing fragments.
 */
USTRUCT()
struct FMetaHumanMassTargetLocationFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector TargetLocation = FVector::ZeroVector;

};