// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyMetaHumanGroom.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * A rule to test if a UObject complies with the MetaHuman Groom standard
 */
UCLASS(MinimalAPI, BlueprintType)
class UVerifyMetaHumanGroom : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()

public:
	// UMetaHumanVerificationRuleBase overrides
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;

	/**
	 * If true, detailed grooming mesh verification is performed. In addition to the basic checks
	 * (source/target mesh presence and spatial bounds overlap), the source skeletal mesh geometry
	 * is compared against the template MetaHuman head topology in UV space. This is expensive,
	 * so disable it when the caller only needs the basic grooming mesh checks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verification")
	bool bDetailedGroomingMeshVerification = true;

	/**
	 * If true, per-group groom-to-mesh alignment is validated. This builds a dynamic mesh AABB tree
	 * from the source skeletal mesh and tests every strand root against it; it is expensive. Other
	 * per-group strands checks (bounds intersect, vertex/curve counts) always run regardless of this flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verification")
	bool bVerifyGroomToMeshAlignment = true;

	/**
	 * If true, per-group hair cards will be checked for alignment against the groom's combined strands
	 * bounding box. This requires iterating every hair group to compute the strands bounds and is
	 * expensive. Other card checks (e.g. UVs) always run regardless of this flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verification")
	bool bVerifyCardsStrandsAlignment = true;

	/**
	 * If true, per-group hair meshes (e.g. helmets) will be checked for alignment against the groom's
	 * combined strands bounding box. This requires iterating every hair group to compute the strands
	 * bounds and is expensive. Other hair mesh checks (e.g. UVs) always run regardless of this flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Verification")
	bool bVerifyMeshesStrandsAlignment = true;
};

#undef UE_API
