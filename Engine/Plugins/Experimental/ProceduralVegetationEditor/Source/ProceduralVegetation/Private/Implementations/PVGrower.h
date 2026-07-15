// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataTypes/PVGrowerParams.h"
#include "CoreMinimal.h"
#include "PVGrowerData.h"
#include "PVLightDetection.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Nodes/PVObjectInteractionSettings.h"

struct FPVColliderMeshData;
struct FPVSeedPoint;
struct FPVPointGravityAttribute;

struct FPVGrower
{
	static void Grow(const FPVSeedPoint& SeedPoint, const FPVGrowerParams& InGrowerParams, FManagedArrayCollection& InCollection, bool bUseSplitPoints = true);

	static void Grow(const FManagedArrayCollection& InCollection, const FPVGrowerParams& InGrowerParams, FManagedArrayCollection& Collection, bool bUseSplitPoints = true);

	static void FillCollection(FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, const FPVGrowerParams& GrowerParams);

	static void FillFromCollection(const FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, bool bAgeReset, bool bUseSplitPoints = true);

	static void FillPointsFromCollection(const FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, bool bAgeReset);

	static void FillBranchesFromCollection(const FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, bool bUseSplitPoints = true);

	static float RandomValue(uint32 Seed);

	static void InitializeGrowth(const FPVGrowerParams& InGrowerParams, const FPVSeedPoint& SeedPoint, UPVGrowerData* Skeleton);
	
	static void InitializeGrowth(const FManagedArrayCollection& InCollection, UPVGrowerData* Skeleton, bool bAgeReset, bool bUseSplitPoints = true);

	static void IterateGrowthCycles(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton);

	static void PrintPoints(UPVGrowerData* Skeleton);

	static void ConvertPointsToCentimeters(UPVGrowerData* Skeleton);
	
	static void SplitBranchSourcePoints(UPVGrowerData* Skeleton);

	static UPVGrowerPoint* InitializePointFromSeed(const FPVSeedPoint& SeedPoint, UPVGrowerData* Skeleton, const FPVGrowerParams& InGrowerParams);

	static UPVGrowerPrimitive* InitializePrimitiveFromSeed(const FPVSeedPoint& SeedPoint, UPVGrowerData* Skeleton);

	static void ExecuteGrowthCycle(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, const int InCycle, const FPVColliderMeshData& ColliderMeshData);
	
	static void UpdateAge(UPVGrowerData* Skeleton);
	
	/** Generates leaf spawn transforms for all branches in the current skeleton state.
	 *  Translates the Houdini generateLeaves() logic using ethylene levels, phyllotaxy,
	 *  and axil angle to produce one FTransform per leaf (position in cm, scale = pscale). */
	static void GenerateLeaves(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, int32 Cycle, TArray<FPVLeafTransform>& OutLeafTransforms);

	static void DetectLight(UPVGrowerData* Skeleton, const FPVColliderMeshData& StaticColliderData,
		const FPVLeafMeshGeometry& LeafGeometry, const TArray<FPVLeafTransform>& LeafTransforms, int32 InCycle);
	
	static void ProcessPointLightData(UPVGrowerData* Skeleton, const TArray<FPVPointLightVectorData>& PointLightData);

	static void CreateSprout(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, const int InCycle);

	static TArray<UPVGrowerPoint*> FindSproutCandidates(UPVGrowerData* Skeleton);

	static TArray<UPVGrowerPoint*> FindSprouts(const FPVGrowerParams& InGrowerParams, TArray<UPVGrowerPoint*> SproutCandidates, const int Cycle);

	static void SetNumberOfTriggered(TArray<UPVGrowerPoint*> Sprouts, const FPVGrowerParams& InGrowerParams);
	
	static uint32 GetPhyllotaxyMin(const FPVGrowerPhyllotaxyParams& InPhyllotaxy);

	static uint32 GetPhyllotaxyMax(const FPVGrowerPhyllotaxyParams& InPhyllotaxy);

	static void SetAxialElongation(TArray<UPVGrowerPoint*> Sprouts, const FPVTrunkGrowthParams& InAxialParams, const int InRandomSeed);

	static void ApicalGrowthCycle(const FPVGrowerParams& InGrowerParams, TArray<UPVGrowerPoint*> Sprouts, UPVGrowerData* Skeleton);

	static FVector SampleDirectionCone(const FVector& Direction, float AngleInRedians, float RandomX, float RandomY);

	static void AxillaryGrowthCycle(const FPVGrowerParams& InGrowerParams, TArray<UPVGrowerPoint*> Sprouts, UPVGrowerData* Skeleton);

	static void CodominantGrowth(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Sprout, UPVGrowerData* Skeleton);

	static void SetApicalDirectionForCodominantSprout(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Sprout);
	
	static void CodominantSprouting(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Sprout, UPVGrowerData* Skeleton);

	static void PostGrowth(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton);
	
	static void ApplyGravity(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton);
	
	static void ApplyDavinciWeight(UPVGrowerData* Skeleton, const FPVGrowerGravityParams& InGravityParams, const FPVTrunkGrowthParams& InLateralParams);
	
	static void ApplyGravityWeight(UPVGrowerData* Skeleton, const FPVGrowerGravityParams& InGravityParams, const FPVFoliageParams& InFoliage, const FPVTrunkGrowthParams& InAxialParams, const FPVAgeSenescenceParams& InAgeSenescence);

	static void AttributeAccumulation(UPVGrowerData* Skeleton, TArray<FPVPointGravityAttribute>& GravityAttributes, const FPVGrowerGravityParams& InGravityParams, const FPVTrunkGrowthParams& InAxialParams);
	
	static void RealignVectors(UPVGrowerData* Skeleton);
	
	static void GravityBreakRoot(UPVGrowerData* Skeleton, TArray<int32>& BrokenPrimitives, const FPVGrowerGravityParams& InGravityParams);

	static void RemoveBrokenBranches(UPVGrowerData* Skeleton,const TArray<int32>& BrokenPrimitives);
	
	static void BranchBreak(UPVGrowerData* Skeleton, const FPVAgeSenescenceParams& InAgeSenescence, const FPVGrowerGravityParams& InGravityParams);

	static void RemoveBuds(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Primitive, TArray<int32> BrokenBuds, TArray<UPVGrowerPrimitive*>& PrimitivesToRemove);

	static void FindParentsToClean(UPVGrowerPrimitive* Primitive, TArray<int32>& CleanParents);

	static void GetBranchesToRemoveForBrokenBud(UPVGrowerData* Skeleton, UPVGrowerPoint* Point, TArray<UPVGrowerPrimitive*>& RemoveBranches);

	static void CleanParentsForBrokenBuds(UPVGrowerData* Skeleton, TArray<UPVGrowerPrimitive*>& RemoveBranches, TArray<int32>& CleanParents);

	static void SetBrokenTip(UPVGrowerData* Skeleton, TArray<int32> UnbrokenPoints);

	static TArray<int> ExtractBroken(const TArray<int32>& Points, const TArray<int32>& UnBrokenPoints);

	static TArray<int32> FindNonBrokenBuds(UPVGrowerData* Skeleton, TArray<int32> PrimPoints, const FPVAgeSenescenceParams& InAgeSenescence, const FPVGrowerGravityParams& InGravityParams);

	static void FindAdopters(UPVGrowerData* Skeleton, TArray<UPVGrowerPrimitive*>& Adopters);
	
	static void AdoptOrphans(UPVGrowerData* Skeleton, TArray<UPVGrowerPrimitive*>& Adopters);

	static void CleanHierarchyForAdoption(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Adopter,const TArray<UPVGrowerPrimitive*>& OrphanBranches);

	static void AdoptOrphanPoints(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Adopter, TArray<int32>& OrphanPoints);
	
	static void ExtractOrphans(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Adopter,const TArray<UPVGrowerPrimitive*>& Adopters, TArray<UPVGrowerPrimitive*>& OrphanBranches, TArray<int32>& OrphanPoints);

	static void GroundHitBehaviour(UPVGrowerData* Skeleton, const FPVGrowerGravityParams& InGravityParams);

	static void RemovePrimitiveFromParent(UPVGrowerData* Skeleton, TObjectPtr<UPVGrowerPrimitive> Primitive);

	static void DeactivateBrokenBuds(UPVGrowerData* Skeleton, const FPVAgeSenescenceParams& InAgeSenescence);

	static TArray<UPVGrowerPrimitive*> GetGenerationalSortedPrimitives(UPVGrowerData* Skeleton);

	static bool GetActiveInteractions(UPVGrowerData* Skeleton, UPVGrowerPoint* Point);

	static void ComputeGravity(UPVGrowerData* Skeleton, TArray<FPVPointGravityAttribute>& GravityAttributes, const FPVGrowerGravityParams& InGravityParams);
	
	static FQuat CreateGravityMatrix(const FPVPointGravityAttribute& Attribute, const FPVGrowerGravityParams& InGravityParams);

	static void RotateChildPoints(UPVGrowerData* Skeleton, TArray<FPVPointGravityAttribute>& GravityAttributes, FPVPointGravityAttribute& Attribute,const FQuat& Quat);

	static void UpdatePositions(UPVGrowerData* Skeleton,const TArray<FPVPointGravityAttribute>& GravityAttributes);
	
	static void OnGrowthCycleEnd(UPVGrowerData* Skeleton);

	static float GetLengthFromTrunk(const UPVGrowerPoint* InPoint, float NextElongation);

	static void AbscissionSenescence(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, int32 Cycle);
	
	static bool LightSenescence(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, FPVBud& Bud, UPVGrowerPrimitive* Primitive);

	static void DeActivateSenescenceChildren(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Primitive);
	
	static void SetDegradationAmount(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, UPVGrowerPrimitive* Primitive, int32 Cycle);
	
	static void SetSenescence(UPVGrowerData* Skeleton, UPVGrowerPoint* Point, bool bDeactivate);
	
	static bool ApplyAgeSenescence(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Point);
};
