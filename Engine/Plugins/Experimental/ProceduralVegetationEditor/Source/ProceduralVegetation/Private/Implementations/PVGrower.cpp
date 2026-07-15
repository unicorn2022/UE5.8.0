// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVGrower.h"

#include <random>
#include "ProceduralVegetationModule.h"
#include "Algo/Accumulate.h"
#include "Algo/Reverse.h"
#include "Facades/PVAttributesNames.h"
#include "Utils/PVAttributes.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVMetaInfoFacade.h"
#include "Facades/PVPointFacade.h"
#include "Helpers/PVAttributesHelper.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Implementations/PVSeedGenerator.h"
#include "Implementations/PVLightDetection.h"
#include "UObject/Package.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "Grower"

struct FPVPointGravityAttribute
{
	float Weight = 0;
	float Radius = 0;
	float GravityOverride = 0;
	int Generation = 0;
	int BudAge = 0;
	float LeafWeight = 0;
	float Length = 0;
	FVector Position = FVector::ZeroVector;
	FVector Direction = FVector::UpVector;
	FVector Up = FVector::UpVector;
	TArray<int> ChildPoints;
};

void FPVGrowerParams::GetPhyllotaxy(FPVGrowerPhyllotaxyParams& OutPhyllotaxy, int Generation) const
{
	if (Generation > 1 && !bBranchPhyllotaxySameAsTrunk)
	{
		OutPhyllotaxy = BranchPhyllotaxy;
		return;
	}

	OutPhyllotaxy = Phyllotaxy;
}

float FPVGrowerParams::GetPhyllotaxyAngle(const FPVGrowerPhyllotaxyParams& InPhyllotaxy) const
{
	return InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Spiral ? FMath::DegreesToRadians((float)InPhyllotaxy.Formation + InPhyllotaxy.AdditionalAngle)  : FMath::DegreesToRadians((float)PhyllotaxyTypeAngles[(int)InPhyllotaxy.Type] + InPhyllotaxy.AdditionalAngle);
}

void FPVGrowerParams::GetAuxin(FPVAuxinParams& OutAuxin, int Generation) const
{
	if (Generation > 1 && !bBranchAuxinConditionSameAsTrunk)
	{
		OutAuxin = BranchAuxin;
		return;
	}

	OutAuxin = Auxin;
}

void FPVGrowerParams::GetPhototropism(FPVPhototropismParams& OutPhototropism, int Generation) const
{
	if (Generation > 1 && !bBranchPhototropismSameAsTrunk)
	{
		OutPhototropism = BranchPhototropism;
		return;
	}

	OutPhototropism = Phototropism;
}

void FPVGrowerParams::GetDirectionalParams(FPVDirectionalParams& OutDirectional, int Generation) const
{
	if (Generation > 1 && !bBranchDirectionalSameAsTrunk)
	{
		OutDirectional = BranchDirectional;
		return;
	}

	OutDirectional = Directional;
}

void FPVGrowerParams::GetBifurcation(FPVGrowerBifurcationParams& OutBifurcation, int Generation) const
{
	if (Generation > 1 /*&& !bBranchPhyllotaxySameAsTrunk*/)
	{
		OutBifurcation = BranchBifurcation;
		return;
	}

	OutBifurcation = Bifurcation;
}

void FPVGrower::Grow(const FPVSeedPoint& SeedPoint, const FPVGrowerParams& InGrowerParams, FManagedArrayCollection& InCollection, bool bUseSplitPoints)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::Grow);
	TObjectPtr<UPVGrowerData> Skeleton = NewObject<UPVGrowerData>(GetTransientPackage(), UPVGrowerData::StaticClass(),FName("Skeleton"), RF_Public | RF_Transactional);
	InitializeGrowth(InGrowerParams, SeedPoint, Skeleton);
	IterateGrowthCycles(InGrowerParams, Skeleton);
	ConvertPointsToCentimeters(Skeleton);
	if (bUseSplitPoints)
	{
		SplitBranchSourcePoints(Skeleton);
	}
	FillCollection(InCollection, Skeleton, InGrowerParams);
	//PrintPoints();
}

void FPVGrower::Grow(const FManagedArrayCollection& InCollection, const FPVGrowerParams& InGrowerParams, FManagedArrayCollection& Collection, bool bUseSplitPoints)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::Grow);
	TObjectPtr<UPVGrowerData> Skeleton = NewObject<UPVGrowerData>(GetTransientPackage(), UPVGrowerData::StaticClass(),FName("Skeleton"), RF_Public | RF_Transactional);
	InitializeGrowth(InCollection, Skeleton, InGrowerParams.AgeSenescence.bResetOnResumeGrowth, bUseSplitPoints);
	IterateGrowthCycles(InGrowerParams, Skeleton);
	ConvertPointsToCentimeters(Skeleton);
	if (bUseSplitPoints)
	{
		SplitBranchSourcePoints(Skeleton);
	}
	FillCollection(Collection, Skeleton, InGrowerParams);
	//PrintPoints();
}

void FPVGrower::FillCollection(FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, const FPVGrowerParams& GrowerParams)
{
	PV::Facades::FPointFacade PointFacade = PV::Facades::FPointFacade(Collection);
	PV::Facades::FBranchFacade BranchFacade = PV::Facades::FBranchFacade(Collection);

	TArray<FVector3f> Points;
	TArray<float> LengthFromRoots;
	TArray<float> PointScales;
	TArray<float> SeedPScales;
	TArray<float> SeedPScaleRatios;
	TArray<TArray<FVector3f>> BudDirections;
	TArray<TArray<float>> BudHormoneLevels;
	TArray<float> LengthFromSeeds;
	TArray<int32> BudNumbers;
	TArray<TArray<float>> BudLightDetected;
	TArray<TArray<int32>> BudDevelopment;
	TArray<TArray<int32>> BudStatus;
	TArray<TArray<float>> BudLateralMeristem;
	TArray<float> NjordPixelIndices;
	
	int PointIndex = 0;
	for (auto Point : Skeleton->Points)
	{ 
		Points.Add(FVector3f(Point->Position));//FVector3f(-Point->Position.X , Point->Position.Z, Point->Position.Y));
		LengthFromRoots.Add(Point->LengthFromRoot);
		PointScales.Add(Point->PScale);
		SeedPScales.Add(Point->SeedInfo.PScale);
		SeedPScaleRatios.Add(Point->SeedInfo.PScaleRatio);
		LengthFromSeeds.Add(Point->LengthFromRoot);
		BudNumbers.Add(Point->Bud.BudNumber);
		
		BudDirections.Add(Point->Bud.Direction.GetDataArray());
		BudHormoneLevels.Add(Point->Bud.HormoneLevels.GetDataArray());
		BudDevelopment.Add(Point->Bud.Development.GetDataArray());
		BudLightDetected.Add(Point->Bud.LightDetected.GetDataArray());
		BudStatus.Add(Point->Bud.Status.GetDataArray());
		BudLateralMeristem.Add(Point->Bud.LateralMeristem.GetDataArray());
		NjordPixelIndices.Add(PointIndex+1);
		PointIndex++;
	}

	PointFacade.AddElements(Points.Num());
	PointFacade.FillPositions(Points);
	PointFacade.FillLFR(LengthFromRoots);
	PointFacade.FillPointScales(PointScales);
	PointFacade.FillBudDirections(BudDirections);
	PointFacade.FillBudStatus(BudStatus);
	PointFacade.FillBudLateralMeristem(BudLateralMeristem);
	PointFacade.FillBudHormoneLevels(BudHormoneLevels);
	PointFacade.FillLengthFromSeeds(LengthFromSeeds);
	PointFacade.FillBudNumbers(BudNumbers);
	PointFacade.FillBudLightDetected(BudLightDetected);
	PointFacade.FillBudDevelopment(BudDevelopment);
	PointFacade.FillSeedPScale(SeedPScales);
	PointFacade.FillSeedPScaleRatio(SeedPScaleRatios);

	TArray<TArray<int32>> AllParents;
	TArray<TArray<int32>> AllChildren;
	TArray<TArray<int32>> AllPoints;
	TArray<int32> BranchNumbers;
	TArray<int32> BranchSourceBudNumbers;
	TArray<int32> BranchHierarchyNumbers;
	TArray<int32> BranchGenerationNumbers;
	TArray<int32> BranchParentNumbers;
	TArray<int32> PlantNumbers;

	for (auto Branch : Skeleton->Primitives)
	{
		TArray<int32> BranchPoints;
		for (auto Bud : Branch->BranchBuds)
		{
			const int BudIndex = Skeleton->GetPointIndex(Bud);
			if (BudIndex != INDEX_NONE)
			{
				BranchPoints.Add(BudIndex);
			}
			else
			{
				UE_LOGF(LogProceduralVegetation, Warning, "Invalid Bud found for branch number %i", Branch->BranchNumber);
			}
		}

		if (BranchPoints.Num() < 2)
		{
			UE_LOGF(LogProceduralVegetation, Warning, "Bud count for Branch number %i is < 2, excluding the branch from collection", Branch->BranchNumber);
			continue;
		}
		AllPoints.Add(BranchPoints);

		
		AllChildren.Add(Branch->Children);
		AllParents.Add(Branch->Parents);
		BranchNumbers.Add(Branch->BranchNumber);
		BranchSourceBudNumbers.Add(Branch->BranchSourceBudNumber);
		BranchGenerationNumbers.Add(Branch->Parents.Num());
		BranchHierarchyNumbers.Add(Branch->Parents.Num());
		BranchParentNumbers.Add(Branch->BranchParentNumber);
		PlantNumbers.Add(Branch->PlantNumber);
	}

	BranchFacade.AddElements(AllParents.Num());
	BranchFacade.FillParents(AllParents);
	BranchFacade.FillChildren(AllChildren);
	BranchFacade.FillBranchPoints(AllPoints);
	BranchFacade.FillBranchNumbers(BranchNumbers);
	BranchFacade.FillBranchSourceBudNumber(BranchSourceBudNumbers);
	BranchFacade.FillBranchHierarchyNumber(BranchHierarchyNumbers);
	//BranchFacade.FillBranchGenerationNumber(BranchGenerationNumbers);
	BranchFacade.FillBranchParentNumbers(BranchParentNumbers);
	BranchFacade.FillPlantNumbers(PlantNumbers);

	check(PointFacade.IsValid());
	check(BranchFacade.IsValid());

	PV::Facades::FMetaInfoFacade MetaInfoFacade(Collection);

	const TArray<float> LeafGrowthData = {
		GrowerParams.Foliage.EthyleneThreshold,
		GrowerParams.Foliage.StartScale,
		GrowerParams.Foliage.EndScale,
		static_cast<float>(GrowerParams.Foliage.Density),
		GrowerParams.Foliage.AbscisicAcid,
		GrowerParams.Foliage.EthyleneThreshold,
		GrowerParams.Foliage.AuxinRetention
	};
	MetaInfoFacade.SetLeafGrowth(LeafGrowthData);

	const TArray<float> AbscissionSenescenseData = {
		GrowerParams.GravityParams.BranchWeightBreakThreshold,
		static_cast<float>(GrowerParams.AgeSenescence.SenescenceAge),
		static_cast<float>(GrowerParams.AgeSenescence.SenescenceMin),
		static_cast<float>(GrowerParams.AgeSenescence.SenescenceMax),
		GrowerParams.AgeSenescence.RetentionRadius,
		GrowerParams.LightSenescence.SenescenceThreshold,
		static_cast<float>(GrowerParams.LightSenescence.SenescenceMin),
		static_cast<float>(GrowerParams.LightSenescence.SenescenceMax),
		GrowerParams.LightSenescence.RetentionRadius
	};
	MetaInfoFacade.SetAbscissionSenescense(AbscissionSenescenseData);

	const TArray<float> LateralElongationData = {
		GrowerParams.TrunkGrowth.IncrementalRadius,
		GrowerParams.TrunkGrowth.SeedScaleEffect,
		GrowerParams.TrunkGrowth.WhorledRadiusImpact,
		GrowerParams.GravityParams.CellDensity.GetLowerBoundValue(),
		GrowerParams.GravityParams.CellDensity.GetUpperBoundValue(),
		GrowerParams.GravityParams.TrunkReinforcement,
		GrowerParams.GravityParams.CellDevelopmentTime,
		GrowerParams.GravityParams.FoliageWeight,
		GrowerParams.GravityParams.InputReinforcement
	};
	MetaInfoFacade.SetLateralElongation(LateralElongationData);

	// Embed the final-cycle leaf spawn data so the viewport visualizer can render
	// leaf instances without needing access to FPVGrowerParams.
	if (GrowerParams.FoliageMesh && Skeleton->LastLeafTransforms.Num() > 0)
	{
		const int32 NumLeaves = Skeleton->LastLeafTransforms.Num();

		// Single-element group carrying the leaf mesh soft-object path.
		auto MeshPaths = PV::FLeafMeshPathAttribute::AddAttribute(Collection);
		Collection.AddElements(1, PV::GroupNames::LeafMetaGroup);
		MeshPaths[0] = GrowerParams.FoliageMesh->GetPathName();

		// Per-leaf transform data (position cm, rotation as XYZW float4, uniform scale).
		auto Positions = PV::FLeafPositionAttribute::AddAttribute(Collection);
		auto Rotations = PV::FLeafRotationAttribute::AddAttribute(Collection);
		auto Scales    = PV::FLeafScaleAttribute::AddAttribute(Collection);
		Collection.AddElements(NumLeaves, PV::GroupNames::LeavesGroup);

		for (int32 i = 0; i < NumLeaves; ++i)
		{
			const FTransform& T = Skeleton->LastLeafTransforms[i].Transform;
			const FQuat Q = T.GetRotation();
			Positions[i] = FVector3f(T.GetLocation());
			Rotations[i] = FVector4f(Q.X, Q.Y, Q.Z, Q.W);
			Scales[i]    = static_cast<float>(T.GetScale3D().X);
		}
	}
}

void FPVGrower::FillFromCollection(const FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, bool bAgeReset, bool bUseSplitPoints)
{
	FillPointsFromCollection(Collection, Skeleton, bAgeReset);
	FillBranchesFromCollection(Collection, Skeleton, bUseSplitPoints);
}

void FPVGrower::FillPointsFromCollection(const FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, bool bAgeReset)
{
	auto PointPositionAttribute        = PV::FPointPositionAttribute::FindAttribute(Collection);
	auto BudNumberAttribute            = PV::FPointBudNumberAttribute::FindAttribute(Collection);;
	auto BudStatusAttribute            = PV::FBudStatusAttribute::FindAttribute(Collection);
	auto BudDevelopmentAttribute       = PV::FBudDevelopmentAttribute::FindAttribute(Collection);
	auto BudHormoneLevelsAttribute     = PV::FBudHormoneLevelsAttribute::FindAttribute(Collection);
	auto BudDirectionAttribute         = PV::FBudDirectionAttribute::FindAttribute(Collection);
	auto BudLightDetectedAttribute     = PV::FBudLightDetectedAttribute::FindAttribute(Collection);
	auto BudLateralMeristemAttribute   = PV::FBudLateralMeristemAttribute::FindAttribute(Collection);
	auto PointLengthFromRootAttribute  = PV::FPointLengthFromRootAttribute::FindAttribute(Collection);
	auto PointScaleAttribute           = PV::FPointScaleAttribute::FindAttribute(Collection);
	auto PointSeedPScaleAttribute      = PV::FPointSeedPScaleAttribute::FindAttribute(Collection);
	auto PointSeedPScaleRatioAttribute = PV::FPointSeedPScaleRatioAttribute::FindAttribute(Collection);

	auto BranchPointsAttribute       = PV::FBranchPointsAttribute::FindAttribute(Collection);
	auto BranchParentNumberAttribute = PV::FBranchParentNumberAttribute::FindAttribute(Collection);
	auto BranchNumberAttribute       = PV::FBranchNumberAttribute::FindAttribute(Collection);
	auto BranchChildrenAttribute     = PV::FBranchChildrenAttribute::FindAttribute(Collection);

	if (!PV::ValidateAttributeCollection(
		PointPositionAttribute,
		BudNumberAttribute,
		BudStatusAttribute,
		BudDevelopmentAttribute,
		BudHormoneLevelsAttribute,
		BudDirectionAttribute,
		BudLightDetectedAttribute,
		BudLateralMeristemAttribute,
		PointLengthFromRootAttribute,
		PointScaleAttribute,
		PointSeedPScaleAttribute,
		PointSeedPScaleRatioAttribute,
		BranchPointsAttribute,
		BranchParentNumberAttribute,
		BranchNumberAttribute,
		BranchChildrenAttribute
	))
	{
		return;
	}

	const int32 NumPoints = PointPositionAttribute.Num();

	TArray<float> LengthFromTunk;
	LengthFromTunk.SetNumZeroed(NumPoints);
	PV::AttributesHelper::ComputeLengthFromTrunk(
		{
			PointLengthFromRootAttribute,
			BudDevelopmentAttribute,
			BranchPointsAttribute,
			BranchParentNumberAttribute,
			BranchNumberAttribute,
			BranchChildrenAttribute,
		},
		LengthFromTunk
	);

	Skeleton->Points.Reserve(NumPoints);

	for (int32 i = 0; i < NumPoints; i++)
	{
		const int32 BudNumber = BudNumberAttribute[i];
		
		if (Skeleton->GetPointIndex(BudNumber) != INDEX_NONE) // Ignore the Fused points
		{
			continue;
		}

		UPVGrowerPoint* Point = NewObject<UPVGrowerPoint>(Skeleton,UPVGrowerPoint::StaticClass(),NAME_None, RF_Transactional);

		FPVBud Bud;
		Bud.BudNumber = BudNumber;

		Skeleton->MaxBudNumber = FMath::Max(BudNumber, Skeleton->MaxBudNumber);
		
		Bud.Development.SetData(BudDevelopmentAttribute[i].Array);
		if (bAgeReset)
		{
			Bud.Development.ResetAge();
		}
		Bud.Status.SetData(BudStatusAttribute[i].Array);
		Bud.HormoneLevels.SetData(BudHormoneLevelsAttribute[i].Array);
		Bud.Direction.SetData(BudDirectionAttribute[i].Array);
		Bud.LightDetected.SetData(BudLightDetectedAttribute[i].Array);
		Bud.LateralMeristem.SetData(BudLateralMeristemAttribute[i].Array);

		Point->Position = FVector(PointPositionAttribute[i]) * 0.01f;
		Point->LengthFromRoot = PointLengthFromRootAttribute[i];
		Point->LengthFromTrunk = LengthFromTunk[i];
		Point->PScale = PointScaleAttribute[i] * 0.01f;
		
		Point->Bud = Bud;
		Point->bInput = true;

		Point->SeedInfo.PScale = PointSeedPScaleAttribute[i];
		Point->SeedInfo.PScaleRatio = PointSeedPScaleRatioAttribute[i];
		Point->IgnoreGravityBreak = 1.0;

		Skeleton->AddPoint(Point, Bud.BudNumber);
	}
}

void FPVGrower::FillBranchesFromCollection(const FManagedArrayCollection& Collection, UPVGrowerData* Skeleton, bool bUseSplitPoints /*= true*/)
{
	PV::Facades::FBranchFacade BranchFacade = PV::Facades::FBranchFacade(Collection);

	const int32 NumBranches = BranchFacade.GetElementCount();
	Skeleton->Primitives.Reserve(NumBranches);

	TArray<UPVGrowerPoint*> SplitPoints;

	auto SwapSplitPointWithFusedPoint = [&SplitPoints, Skeleton](TArray<int32>& BranchPoints, int Index, int SourceBudNumber)
	{
		if (Index == 0)
		{
			UPVGrowerPoint* Point = Skeleton->GetPointFromIndex(BranchPoints[Index]);
			if (Point && Point->Bud.Status.Seed == 0)
			{
				SplitPoints.Add(Point);
				BranchPoints[Index] = Skeleton->GetPointIndex(SourceBudNumber);
			}
		}
	};
	
	for (int i=0; i< NumBranches; i++)
	{
		UPVGrowerPrimitive* Primitive = NewObject<UPVGrowerPrimitive>(Skeleton,UPVGrowerPrimitive::StaticClass(),NAME_None, RF_Transactional);

		TArray<int32> BranchPoints;
		BranchFacade.GetBranchPoints(i, BranchPoints);

		int32 BranchSourceBudNumber;
		BranchFacade.GetBranchSourceBudNumber(i, BranchSourceBudNumber);
		Primitive->BranchSourceBudNumber = BranchSourceBudNumber;

		for (int j=0; j< BranchPoints.Num(); j++)
		{
			if (bUseSplitPoints)
			{
				SwapSplitPointWithFusedPoint(BranchPoints, j, BranchSourceBudNumber);
			}
			
			UPVGrowerPoint* Point = Skeleton->GetPointFromIndex(BranchPoints[j]);
			if (Point)
			{
				Point->Primitive = Primitive;
				Primitive->AddBranchBud(Point);

				if (BranchPoints.IsValidIndex(j + 1))
				{
					UPVGrowerPoint* NextPoint = Skeleton->GetPointFromIndex(BranchPoints[j + 1]);
					if (NextPoint)
					{
						Point->Neighbors.AddUnique(NextPoint);
						NextPoint->Neighbors.AddUnique(Point);
					}
				}
			}
			else
			{
				UE_LOGF(LogProceduralVegetation, Warning, "Point Not found for index %i", BranchPoints[j]);
			}
		}

		TArray<int32> BranchParents;
		BranchFacade.GetParents(i, BranchParents);
		Primitive->Parents = BranchParents;

		TArray<int32> BranchChildren;
		BranchFacade.GetChildren(i, BranchChildren);
		Primitive->Children = BranchChildren;

		int32 BranchNumber = BranchFacade.GetBranchNumber(i);
		Primitive->BranchNumber = BranchNumber;
		Skeleton->MaxBranchNumber = FMath::Max(BranchNumber, Skeleton->MaxBranchNumber);

		int32 BranchParentNumber;
		BranchFacade.GetBranchParentNumber(i, BranchParentNumber);
		Primitive->BranchParentNumber = BranchParentNumber;

		int32 BranchHierarchyNumber;
		BranchFacade.GetBranchHierarchyNumber(i, BranchHierarchyNumber);

		int32 BranchPlantNumber;
		BranchFacade.GetPlantNumber(i, BranchPlantNumber);
		Primitive->PlantNumber = BranchPlantNumber;

		Skeleton->AddPrimitive(Primitive, BranchNumber);
	}

	if (bUseSplitPoints)
	{
		for (UPVGrowerPoint* Point : SplitPoints)
		{
			if (Point)
			{
				Skeleton->RemovePoint(Point);
			}
		}
	}
}

float FPVGrower::RandomValue(uint32 Seed)
{
	std::mt19937 RandomSeedEngine(Seed);
	return (RandomSeedEngine() >> 8) * (1.0f / 16777216.0f);
}

void FPVGrower::InitializeGrowth(const FPVGrowerParams& InGrowerParams, const FPVSeedPoint& SeedPoint, UPVGrowerData* Skeleton)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::InitializeGrowth);
	check(Skeleton);
	Skeleton->Points.Empty();
	Skeleton->Primitives.Empty();
	
	Skeleton->MaxBranchNumber = 0;
	Skeleton->MaxBudNumber = 0;
	
	UPVGrowerPoint* Point = std::move(InitializePointFromSeed(SeedPoint, Skeleton, InGrowerParams));
	UPVGrowerPrimitive* Primitive = std::move(InitializePrimitiveFromSeed(SeedPoint, Skeleton));
	//Skeleton->Primitives.Add(Primitive);
	Skeleton->AddPrimitive(Primitive);
	Skeleton->AddPoint(Point, Primitive);
	Skeleton->MinLengthFromRoot = SeedPoint.LengthFromRoot;

	//Calculate MinRootDist from seed points
}

void FPVGrower::InitializeGrowth(const FManagedArrayCollection& InCollection, UPVGrowerData* Skeleton, bool bAgeReset, bool bUseSplitPoints)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::InitializeGrowth);
	check(Skeleton);

	// Capture max bud age from the collection before FillFromCollection potentially resets
	// ages (bAgeReset).  This represents the foliage maturity already reached by the
	// upstream grower and is used as a cycle offset in GenerateLeaves.
	const int32 MaxBudAge = PV::AttributesHelper::GetMaxBudAge(PV::FBudDevelopmentAttribute::FindAttribute(InCollection));
	if (MaxBudAge > 0)
	{
		Skeleton->InputCycleOffset = MaxBudAge;
	}

	FillFromCollection(InCollection, Skeleton, bAgeReset, bUseSplitPoints);

	// Restore the upstream grower's final-cycle leaf transforms into LastLeafTransforms so
	// that DetectLight on cycle 0 uses them as occluders.  Without this the array is empty
	// and the first growth cycle runs without any leaf occlusion, regardless of what the
	// upstream grower produced.
	const auto Positions = PV::FLeafPositionAttribute::FindAttribute(InCollection);
	const auto Rotations = PV::FLeafRotationAttribute::FindAttribute(InCollection);
	const auto Scales    = PV::FLeafScaleAttribute::FindAttribute(InCollection);

	if (PV::ValidateAttributeCollection(Positions, Rotations, Scales))
	{
		const int32 NumLeaves = Positions.Num();
		if (NumLeaves > 0 && Rotations.Num() == NumLeaves && Scales.Num() == NumLeaves)
		{
			Skeleton->LastLeafTransforms.Reserve(NumLeaves);
			for (int32 i = 0; i < NumLeaves; ++i)
			{
				const FVector4f R = Rotations[i];
				const float     S = Scales[i];
				FPVLeafTransform& Leaf = Skeleton->LastLeafTransforms.AddDefaulted_GetRef();
				Leaf.Transform = FTransform(
					FQuat(R.X, R.Y, R.Z, R.W),
					FVector(Positions[i]),
					FVector(S, S, S));
			}
		}
	}
}

void FPVGrower::IterateGrowthCycles(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton)
{
	FPVColliderMeshData ColliderMeshData;
	FPVLightDetection::BuildPVCollisionData(InGrowerParams.ColliderSettings, ColliderMeshData);

	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::IterateGrowthCycles);

	for (uint32 Cycle = 0; Cycle < InGrowerParams.GrowthCycles; Cycle++)
	{
		ExecuteGrowthCycle(InGrowerParams, Skeleton, Cycle, ColliderMeshData);
	}
}

void FPVGrower::PrintPoints( UPVGrowerData* Skeleton)
{
	for (auto Point : Skeleton->Points)
	{
		UE_LOGF(LogTemp, Log, "Position %ls", *Point->Position.ToString());
	}
}

void FPVGrower::ConvertPointsToCentimeters(UPVGrowerData* Skeleton)
{
	for (auto Point : Skeleton->Points)
	{
		Point->Position *= 100;
		Point->PScale = Point->Bud.LateralMeristem.LateralMeristem * 100;
	}
}

void FPVGrower::SplitBranchSourcePoints(UPVGrowerData* Skeleton)
{
	for (auto Primitive : Skeleton->Primitives)
	{
		if (Primitive->BranchBuds.IsValidIndex(0))
		{
			UPVGrowerPoint* Point = Skeleton->GetPoint(Primitive->BranchBuds[0]);
			if (Point)
			{
				if (Point->Bud.Status.Seed != 1)
				{
					UPVGrowerPoint* SplitPoint = DuplicateObject<UPVGrowerPoint>(Point, Skeleton);
					SplitPoint->Bud = Point->Bud;
					Skeleton->AddPoint(SplitPoint, Primitive, false);
					Primitive->BranchBuds[0] = Skeleton->MaxBudNumber;
				}
			}
		}
	}
}

UPVGrowerPoint* FPVGrower::InitializePointFromSeed(const FPVSeedPoint& SeedPoint, UPVGrowerData* Skeleton, const FPVGrowerParams& InGrowerParams)
{
	UPVGrowerPoint* Point = NewObject<UPVGrowerPoint>(Skeleton,UPVGrowerPoint::StaticClass(),NAME_None, RF_Transactional);
	FPVBud& Bud = Point->Bud;
	Bud.Direction =
	{
		SeedPoint.ApicalDirection,
		SeedPoint.AxillaryDirection,
		SeedPoint.ApicalDirection,
		SeedPoint.ApicalDirection,
		SeedPoint.ApicalDirection,
		SeedPoint.UpVector
	};

	Bud.LateralMeristem = {0,1,0,0,0,SeedPoint.LengthFromRoot,0};
	Bud.BudNumber = 1; // One For now
	
	Bud.Status = FPVBudStatus{1,0,0,1,1,0,0,0,0,0};
	Bud.HormoneLevels = FPVBudHormoneLevels{1,1, InGrowerParams.Auxin.RadicalAuxin,0,0 };
	Bud.LightDetected = {0,0,0,0};
	Bud.Development = { 1,0,0,0,0,1};
	
	FPVSeedInfo SeedInfo
	{
		SeedPoint.SeedPScale,
		SeedPoint.SeedPScaleRatio,
		SeedPoint.SeedType,
		SeedPoint.SeedGeneration,
		SeedPoint.PlantNumber,
		SeedPoint.SeedBranchParentNumber,
		SeedPoint.SeedMaxBranchNumber,
		SeedPoint.SeedBranchSourceBudNumber,
		SeedPoint.SeedMaxBudNumber,
	};
	Point->SeedInfo = SeedInfo;

	//Apply Offset
	FPVGrowerPhyllotaxyParams PhyllotaxyLocal;
	InGrowerParams.GetPhyllotaxy(PhyllotaxyLocal, 1);
	
	FMatrix RotationMatrix = FQuat(Bud.Direction.Apical.GetSafeNormal(),FMath::DegreesToRadians(PhyllotaxyLocal.Offset)).ToMatrix();
	Bud.Direction.Axillary = RotationMatrix.TransformVector(Bud.Direction.Axillary);
	Bud.Direction.UpVector = RotationMatrix.TransformVector(Bud.Direction.UpVector);

	return Point;
}

UPVGrowerPrimitive* FPVGrower::InitializePrimitiveFromSeed(const FPVSeedPoint& SeedPoint, UPVGrowerData* Skeleton)
{
	UPVGrowerPrimitive* Primitive = NewObject<UPVGrowerPrimitive>(Skeleton,UPVGrowerPrimitive::StaticClass(),NAME_None, RF_Transactional);
	Primitive->BranchNumber = SeedPoint.BranchNumber;
	Primitive->PlantNumber = SeedPoint.PlantNumber;
	Primitive->BranchParentNumber = 0;
	Primitive->BranchSourceBudNumber = 1;
	Primitive->Parents = {0};

	return Primitive;
}

void FPVGrower::ExecuteGrowthCycle(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, const int InCycle, const FPVColliderMeshData& InColliderMeshData)
{
	const FString GrowthCycleScopeName = FString::Printf(TEXT("PV::Grower::ExecuteGrowthCycle [%d]"), InCycle);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*GrowthCycleScopeName);
	UpdateAge(Skeleton);

	// Use leaf transforms cached at the end of the previous cycle for light occlusion.
	// On the first cycle with no input pin connected LastLeafTransforms is empty — correct,
	// no leaves exist yet.  When loaded from an upstream grower the array is pre-populated
	// in InitializeGrowth so cycle 0 already benefits from the upstream leaves.
	DetectLight(Skeleton, InColliderMeshData, InGrowerParams.CachedLeafMeshGeometry, Skeleton->LastLeafTransforms, InCycle);

	if (InGrowerParams.bSenescence)
	{
		AbscissionSenescence(InGrowerParams, Skeleton, InCycle);
	}
	CreateSprout(InGrowerParams, Skeleton, InCycle);

	// Cache leaf transforms from the surviving branches at the end of this cycle.
	// Used by the next cycle's light detection pass and by FillCollection for visualization.
	if (InGrowerParams.CachedLeafMeshGeometry.IsValid())
	{
		GenerateLeaves(InGrowerParams, Skeleton, InCycle, Skeleton->LastLeafTransforms);
	}
}

void FPVGrower::UpdateAge(UPVGrowerData* Skeleton)
{
	for(auto Point : Skeleton->Points)
	{
		const FPVBudStatus& Status = Point->Bud.Status;
		Point->Bud.Development.BudAge = Point->Bud.Development.BudAge + 1;
		Point->Bud.Development.BranchAge = Point->Bud.Development.BranchAge + 1;
	}
}

void FPVGrower::DetectLight(UPVGrowerData* Skeleton, const FPVColliderMeshData& StaticColliderData,
	const FPVLeafMeshGeometry& LeafGeometry, const TArray<FPVLeafTransform>& LeafTransforms, int32 InCycle)
{
	const FString DetectLightScopeName = FString::Printf(TEXT("PV::Grower::DetectLight [%d]"), InCycle);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*DetectLightScopeName);
	// Build a per-cycle combined data set: static external colliders + this cycle's leaf instances.
	// Leaf instances go into a separate buffer (CombinedData.LeafInstances) so the shader can
	// apply self-branch exclusion — a leaf on the same branch as the ray origin is not an occluder.
	FPVColliderMeshData CombinedData = StaticColliderData;
	FPVLightDetection::BuildPVLeafInstanceData(LeafGeometry, LeafTransforms, CombinedData);

	TArray<FPVPointLightVectorData> PointLightData = FPVLightDetection::ExecuteLightDetection(
		FPVLightDetection::BuildPVCollisionDataSkeleton(Skeleton),
		FPVLightDetection::BuildPVRayOriginData(Skeleton), CombinedData);

	if (!PointLightData.IsEmpty())
	{
		ProcessPointLightData(Skeleton, PointLightData);
	}
}

void FPVGrower::GenerateLeaves(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, int32 Cycle, TArray<FPVLeafTransform>& OutLeafTransforms)
{
	OutLeafTransforms.Empty();
	
	const FPVFoliageParams& LeafGrowth = InGrowerParams.Foliage;

	// Sapling age: ramps from 0 to 1 as cycles progress (controls leaf scale).
	// When loaded from an upstream grower, InputCycleOffset is the max bud age accumulated
	// by that grower.  Adding it to the local cycle gives an effective cycle that reflects
	// the plant's true cumulative age, so foliage maturity continues from where the
	// upstream grower left off rather than restarting from zero.
	const float MaxAge        = 100.0f * LeafGrowth.DevelopmentTime + 1.0f;
	const float EffectiveCycle = (float)Cycle + (float)Skeleton->InputCycleOffset;
	const float SaplingAge    = FMath::Clamp(FMath::GetRangePct(0.0f, MaxAge, EffectiveCycle), 0.0f, 1.0f);

	for (UPVGrowerPrimitive* Primitive : Skeleton->Primitives)
	{
		if (Primitive->BranchBuds.Num() < 2) continue;

		UPVGrowerPoint* SourcePoint = Skeleton->GetPoint(Primitive->BranchBuds[0]);

		int Generation = 1;

		if (SourcePoint)
		{
			Generation = SourcePoint->Bud.Development.Generation;
		}

		FPVGrowerPhyllotaxyParams LeafPhyllotaxy = InGrowerParams.LeafPhyllotaxy;

		if (InGrowerParams.bLeafPhyllotaxySameAsBranch)
		{
			if (InGrowerParams.bBranchPhyllotaxySameAsTrunk)
			{
				LeafPhyllotaxy = InGrowerParams.Phyllotaxy;
			}
			else
			{
				LeafPhyllotaxy = InGrowerParams.BranchPhyllotaxy;
			}
		}

		// GetPhyllotaxyAngle already returns radians — do NOT wrap in DegreesToRadians again.
		const float PhyllotaxyAngleRad = InGrowerParams.GetPhyllotaxyAngle(LeafPhyllotaxy);
		const float AxilAngleRad       = FMath::DegreesToRadians(LeafPhyllotaxy.AxilAngle);
		const float OffsetRad          = FMath::DegreesToRadians(LeafPhyllotaxy.Offset);
		// Flatten is stored as a normalised [0,1] value;
		const float FlattenRad         = FMath::DegreesToRadians(LeafPhyllotaxy.Flatten * 90.0f);
		const uint32 MinBuds           = GetPhyllotaxyMin(LeafPhyllotaxy);
		const uint32 MaxBuds           = GetPhyllotaxyMax(LeafPhyllotaxy);

		const int32 BranchNumber = Primitive->BranchNumber;

		// Collect live points for this branch
		TArray<UPVGrowerPoint*> BranchPoints;
		BranchPoints.Reserve(Primitive->BranchBuds.Num());
		for (const int32 BudNum : Primitive->BranchBuds)
		{
			if (UPVGrowerPoint* Pt = Skeleton->GetPoint(BudNum))
				BranchPoints.Add(Pt);
		}
		if (BranchPoints.Num() < 2) continue;

		// ---- getPointArrays: iterate tip-to-root, filter by ethylene, collect data ----
		struct FLeafPointData
		{
			float   Ethylene;
			float   PScale;
			bool    bTriggered;
			FVector Apical;
			FVector Axillary;
			FVector Up;
			FVector Pos;   // cm (light-detection space)
		};
		TArray<FLeafPointData> PointData;
		PointData.Reserve(BranchPoints.Num());

		for (int32 i = BranchPoints.Num() - 1; i >= 0; --i)
		{
			const UPVGrowerPoint* Pt = BranchPoints[i];
			float Ethylene       = Pt->Bud.HormoneLevels.Ethylene;
			const float Auxin    = Pt->Bud.HormoneLevels.Apical;
			const float LeafRet  = FMath::Lerp(Ethylene, 0.0f, Auxin);
			Ethylene             = FMath::Lerp(Ethylene, LeafRet, LeafGrowth.AuxinRetention);

			// Break when two consecutive tip-side points both exceed the threshold
			if (PointData.Num() > 0
				&& Ethylene > LeafGrowth.EthyleneThreshold
				&& PointData.Last().Ethylene > LeafGrowth.EthyleneThreshold)
			{
				break;
			}

			const float T      = FMath::Clamp(FMath::GetRangePct(0.0f, LeafGrowth.EthyleneThreshold, Ethylene), 0.0f, 1.0f);
			const float PScale = FMath::Lerp(LeafGrowth.StartScale * SaplingAge, LeafGrowth.EndScale * SaplingAge, T);

			FLeafPointData Data;
			Data.Ethylene   = Ethylene;
			Data.PScale     = PScale;
			Data.bTriggered = (Pt->Bud.Status.Triggered > 0);
			Data.Apical     = Pt->Bud.Direction.Apical;
			Data.Axillary   = Pt->Bud.Direction.Axillary;
			Data.Up         = Pt->Bud.Direction.UpVector;
			Data.Pos        = Pt->Position * 100.0;  // grower meters -> cm for collision system
			PointData.Add(Data);
		}

		// Restore root-to-tip order
		Algo::Reverse(PointData);
		if (PointData.Num() < 2) continue;

		// ---- interpolateValues: resample segment pairs by leaf density ----
		TArray<FLeafPointData> Resampled;
		const int32 Density = FMath::Max(LeafGrowth.Density, 1);

		if (Density == 1)
		{
			for (int32 i = 1; i < PointData.Num(); ++i)
			{
				if (!PointData[i].bTriggered)
					Resampled.Add(PointData[i]);
			}
		}
		else
		{
			for (int32 i = 1; i < PointData.Num(); ++i)
			{
				for (int32 j = 0; j < Density; ++j)
				{
					const float Blend = (float)j / (float)(Density - 1);
					const float BlendEth = FMath::Lerp(PointData[i-1].Ethylene, PointData[i].Ethylene, Blend);
					if (BlendEth > LeafGrowth.EthyleneThreshold) continue;

					const float BlendTriggered = FMath::Lerp(
						PointData[i-1].bTriggered ? 1.f : 0.f,
						PointData[i].bTriggered   ? 1.f : 0.f, Blend);
					if (BlendTriggered > 0.8f) continue;

					FLeafPointData R;
					R.Ethylene   = BlendEth;
					R.PScale     = FMath::Lerp(PointData[i-1].PScale,   PointData[i].PScale,   Blend);
					R.bTriggered = false;
					R.Apical     = FMath::Lerp(PointData[i-1].Apical,   PointData[i].Apical,   Blend).GetSafeNormal();
					R.Axillary   = FMath::Lerp(PointData[i-1].Axillary, PointData[i].Axillary, Blend).GetSafeNormal();
					// Matches Houdini line 86: lerp(axillary[i-1], up[i], blend)
					R.Up         = FMath::Lerp(PointData[i-1].Axillary, PointData[i].Up,        Blend).GetSafeNormal();
					R.Pos        = FMath::Lerp(PointData[i-1].Pos,      PointData[i].Pos,       Blend);
					Resampled.Add(R);
				}
			}
		}
		if (Resampled.Num() == 0) continue;

		// ---- initializePhyllotaxy: accumulate phyllotaxy angle per resampled point ----
		{
			const FVector InitApical = Resampled[0].Apical.GetSafeNormal();
			const FVector InitUp     = Resampled[0].Up.GetSafeNormal();

			FVector CurrAxillary = LeafPhyllotaxy.bReset
				? FVector::CrossProduct(InitApical, InitUp).GetSafeNormal()
				: Resampled[0].Axillary.GetSafeNormal();

			if (FMath::Abs(OffsetRad) > 0.01f)
				CurrAxillary = FQuat(InitApical, OffsetRad).RotateVector(CurrAxillary);

			for (int32 i = 0; i < Resampled.Num(); ++i)
			{
				const FVector Apical = Resampled[i].Apical.GetSafeNormal();

				// Phyllotaxy rotation accumulates around the initial apical axis
				FVector AxillaryCurr = FQuat(InitApical, PhyllotaxyAngleRad * i).RotateVector(CurrAxillary);

				// Dihedral: align from initial apical frame to the current point's apical frame
				AxillaryCurr = FQuat::FindBetweenNormals(InitApical, Apical).RotateVector(AxillaryCurr);
				AxillaryCurr.Normalize();

				Resampled[i].Axillary = AxillaryCurr;
			}
		}

		// ---- generateSpawnPoints: emit one FTransform per leaf ----
		int32 LeafIdx = 0;

		for (int32 i = 0; i < Resampled.Num(); ++i)
		{
			const FLeafPointData& Data = Resampled[i];
			if (Data.PScale <= SMALL_NUMBER) continue;

			const FVector Apical   = Data.Apical.GetSafeNormal();
			const FVector Axillary = Data.Axillary.GetSafeNormal();
			const FVector Up       = Data.Up.GetSafeNormal();
			const FVector CrossVec = FVector::CrossProduct(Apical, Up);

			// Number of leaves at this position (whorled count or 1-2 for alternate/opposite)
			// Hash-mix loop vars so adjacent (BranchNumber, i) positions cross RoundToInt buckets instead of clumping.
			const uint32 LeafSeedHash = HashCombine(HashCombine(GetTypeHash(BranchNumber), GetTypeHash(i)), GetTypeHash(InGrowerParams.RandomSeed));
			const int32 NumLeaves = FMath::Max(
				FMath::RoundToInt(FMath::Lerp((float)MinBuds, (float)MaxBuds, RandomValue(LeafSeedHash))), (int32)MinBuds);
			const float AxillaryDegRad = FMath::DegreesToRadians(360.0f / (float)NumLeaves);

			for (int32 j = 0; j < NumLeaves; ++j)
			{
				// Abscisic acid randomly suppresses individual leaf buds
				if (RandomValue((uint32)(LeafIdx + BranchNumber + InGrowerParams.RandomSeed + 276)) < LeafGrowth.AbscisicAcid)
				{
					++LeafIdx;
					continue;
				}
				++LeafIdx;

				// Rotate axillary around apical for each whorl position
				FVector AxillaryCurr = FQuat(Apical, AxillaryDegRad * j).RotateVector(Axillary).GetSafeNormal();

				// Axil axis: perpendicular to axillary and apical (controls the tilt direction)
				const FVector AxilAxis = FVector::CrossProduct(AxillaryCurr, Apical).GetSafeNormal();

				// Tilt leaf outward towards the axillary direction (negative angle = towards axillary)
				FVector ApicalCurr = FQuat(AxilAxis, -AxilAngleRad).RotateVector(Apical).GetSafeNormal();

				// Leaf up/normal: derived from axil axis for up_type=0 (cross product),
				// or from the branch up vector for up_type=1 (non-zero flatten)
				FVector UpCurr = (LeafPhyllotaxy.Flatten < 0.001f)
					? FVector::CrossProduct(AxilAxis, ApicalCurr).GetSafeNormal()
					: Up;

				// Flatten: bias the leaf toward the branch's up plane
				if (FlattenRad > 0.001f)
				{
					float FlattenDot = FVector::DotProduct(AxillaryCurr, Up);
					if (FVector::DotProduct(AxillaryCurr, CrossVec) < 0.f) FlattenDot = -FlattenDot;
					const FQuat FlattenQuat(Apical, FlattenRad * FlattenDot);
					ApicalCurr = FlattenQuat.RotateVector(ApicalCurr).GetSafeNormal();
					UpCurr     = FlattenQuat.RotateVector(UpCurr).GetSafeNormal();
				}

				// Build rotation from X=leaf-forward (ApicalCurr) and Z=leaf-normal (UpCurr)
				// MakeFromXZ also handles re-orthogonalization internally
				const FQuat LeafRot = FRotationMatrix::MakeFromXZ(ApicalCurr, UpCurr).ToQuat();
				OutLeafTransforms.Add({ FTransform(LeafRot, Data.Pos, FVector(Data.PScale)), BranchNumber });
			}
		}
	}
}

void FPVGrower::ProcessPointLightData(UPVGrowerData* Skeleton, const TArray<FPVPointLightVectorData>& PointLightData)
{
	for (const FPVPointLightVectorData& Data : PointLightData)
	{
		UPVGrowerPoint* Point = Skeleton->GetPoint(Data.PointNumber);
		check(Point);
		if(Point)
		{
			Point->Bud.LightDetected.Available = Data.LightAvailable;
			Point->Bud.Direction.LightOptimal = FVector(Data.LightOptimalDirection);
			Point->Bud.Direction.LightSubOptimal = FVector(Data.LightSubOptimalDirection);
		}
		//Figure out what bud light collision is and set it
		//Point->Bud.LightDetected.Collision = ?
	}

	TMap<int, float> BranchLights;
	TArray<int> BranchPrims;

	// Get Initial Branch Light
	for (auto Primitive : Skeleton->Primitives)
	{
	    BranchPrims.Add(Primitive->BranchNumber);
	    float LightAvailable = 0.0;
		int NumTriggeredPoints = 0;
	    for (auto BudNumber: Primitive->BranchBuds)
	    {
	    	UPVGrowerPoint* Point = Skeleton->GetPoint(BudNumber);
	    	check(Point);
	    	
	        if (Point && Point->Bud.Status.Triggered == 0)
	        {
	        	LightAvailable += Point->Bud.LightDetected.Available;
	        	NumTriggeredPoints++;
	        }
	    }
	    float BranchLight = NumTriggeredPoints == 0 ? LightAvailable : LightAvailable / NumTriggeredPoints;
	   BranchLights.Add(Primitive->BranchNumber, BranchLight);
	}

	// Append Child Light
	Algo::Reverse(BranchPrims);
	for (int Prim: BranchPrims)
	{
		UPVGrowerPrimitive* Primitive = Skeleton->GetPrimitive(Prim);
		check(Primitive);
		
	    float BranchLight = BranchLights[Prim];
	    TArray<float> ChildBranchLightArray;
		ChildBranchLightArray.Add(BranchLight);
	    
	    if (Primitive->Children.Num() > 1)
	    {
	        for(int32 Child: Primitive->Children)
	    	{
	        	float* ChildLight = BranchLights.Find(Child);
	        	if (ChildLight)
	        	{
	        		ChildBranchLightArray.Add(*ChildLight);
	        	}
	        	else
	        	{
	        		UE_LOGF(LogProceduralVegetation, Log, "Child Light not found for Child Branch %i", Child);
	        	}
	        }
	        
	        Algo::Sort(ChildBranchLightArray);
	    	Algo::Reverse(ChildBranchLightArray);
	    	ChildBranchLightArray.RemoveAt(ChildBranchLightArray.Num()-1);
	    }
	    
	    BranchLight = ChildBranchLightArray.Num() > 0 ? Algo::Accumulate(ChildBranchLightArray, 0.0) / ChildBranchLightArray.Num() : 0;
	    BranchLights[Prim] = BranchLight;
	}

	// Set Attributes
	for (int Prim: BranchPrims)
	{
		UPVGrowerPrimitive* Primitive = Skeleton->GetPrimitive(Prim);
		check(Primitive);
		
	    float BranchLight = BranchLights[Prim];
	    
	    for(int PointIndex :  Primitive->BranchBuds)
		{
	    	UPVGrowerPoint* Point = Skeleton->GetPoint(PointIndex);
	    	check(Point);
	    	Point->Bud.LightDetected.Branch = BranchLight;
	    }
	    
		//Primitive->BranchLight = BranchLight;
	}
}

void FPVGrower::CreateSprout(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, const int InCycle)
{
	auto SproutCandidates = FindSproutCandidates(Skeleton);
	auto Sprouts = FindSprouts(InGrowerParams, SproutCandidates, InCycle);
	SetNumberOfTriggered(Sprouts, InGrowerParams);
	SetAxialElongation(Sprouts, InGrowerParams.TrunkGrowth, InGrowerParams.RandomSeed);
	
	ApicalGrowthCycle(InGrowerParams, Sprouts, Skeleton);
	AxillaryGrowthCycle(InGrowerParams, Sprouts, Skeleton);
	
	PostGrowth(InGrowerParams, Skeleton);
	ApplyGravity(InGrowerParams, Skeleton);

	OnGrowthCycleEnd(Skeleton);
}

TArray<UPVGrowerPoint*> FPVGrower::FindSproutCandidates(UPVGrowerData* Skeleton)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::FindSproutCandidates);
	TArray<UPVGrowerPoint*> SproutCandidates;
	for(auto Point : Skeleton->Points)
	{
		const FPVBudStatus& Status = Point->Bud.Status;
		/*Point->Bud.Development.BudAge = Point->Bud.Development.BudAge + 1;
		Point->Bud.Development.BranchAge = Point->Bud.Development.BranchAge + 1;*/
		/*UE_LOGF(LogTemp, Log, "SproutCandidates:: Point Position %ls , Dormant %i Triggered %i Inactive %i",
			*Point->Position.ToString(), Status.Dormant, Status.Inactive, Status.Triggered);*/
		if (Status.Dormant == 1  && Status.Triggered == 0 && Status.Inactive == 0)
		{
			if (Point->VisAgeSenescen == 0)
			{
				SproutCandidates.Add(Point);
				/*UE_LOGF(LogTemp, Log, "SproutCandidate Position %ls", *Point->Position.ToString());*/
			}
		}
	}

	return SproutCandidates;
}

TArray<UPVGrowerPoint*> FPVGrower::FindSprouts(const FPVGrowerParams& InGrowerParams, TArray<UPVGrowerPoint*> SproutCandidates, const int InCycle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::FindSprouts);
	TArray<UPVGrowerPoint*> Sprouts;

	auto SafeDivideLambda = [](float Numerator, float Denominator)->float
	{
		if (FMath::Abs(Denominator) < KINDA_SMALL_NUMBER)
		{
			return 0.0;
		}

		return Numerator / Denominator;
	};
	
	for(auto Point : SproutCandidates)
	{
		const float RootDistance = Point->LengthFromRoot;
		const float TrunkDistance = Point->LengthFromTrunk;

		const float TargetLength = InGrowerParams.TrunkGrowth.PlantTargetLength;
		const float TargetBranchLength = InGrowerParams.TrunkGrowth.BranchTargetLength;
		//LFR -> Length from root
		float RelativeLFR = FMath::Clamp(SafeDivideLambda(RootDistance, TargetLength), 0.0f, 1.0f);
		//LFT -> Length from trunk
		const float RelativeLFT = FMath::Clamp(SafeDivideLambda(TrunkDistance, TargetBranchLength), 0.0f, 1.0f);

		const float RelativePosZ = FMath::Clamp(SafeDivideLambda(Point->Position.Z, TargetLength), 0.0f, 1.0f);
		RelativeLFR = FMath::Lerp(RelativeLFR,RelativePosZ,InGrowerParams.TrunkGrowth.Corymb);
		
		FRandomStream RandomStream(Point->Bud.BudNumber + InGrowerParams.RandomSeed + 18385);
		const float CytokininRandom = RandomStream.FRand();
		const float ApicalParentPriorityMultiplier = InGrowerParams.TrunkGrowth.ApicalParentGrowth;
		const float AxillaryParentPriorityMultiplier = InGrowerParams.TrunkGrowth.AxillaryParentGrowth;
		const float ApicalChildPriorityMultiplier = InGrowerParams.TrunkGrowth.ApicalChildGrowth;
		const float AxillaryChildPriorityMultiplier = InGrowerParams.TrunkGrowth.AxillaryChildGrowth;
		
		float ApicalPriorityMultiplier = ApicalParentPriorityMultiplier;
		float AxillaryPriorityMultiplier = AxillaryParentPriorityMultiplier;
		const FRichCurve* ApicalGradientCurve = InGrowerParams.TrunkGrowth.ApicalPriorityGradient.GetRichCurveConst();
		const FRichCurve* ApicalGradientChildCurve = InGrowerParams.TrunkGrowth.ApicalPriorityChildGradient.GetRichCurveConst();
		const FRichCurve* AxillaryGradientCurve = InGrowerParams.TrunkGrowth.AxillaryPriorityGradient.GetRichCurveConst();
		const FRichCurve* AxillaryGradientChildCurve = InGrowerParams.TrunkGrowth.AxillaryPriorityChildGradient.GetRichCurveConst();

		check(ApicalGradientCurve);
		check(ApicalGradientChildCurve);
		check(AxillaryGradientCurve);
		check(AxillaryGradientChildCurve);

		const int Generation = Point->Bud.Development.Generation;
		
		float ApicalPriorityGradient = ApicalGradientCurve->Eval(RelativeLFR);//FMath::Lerp(0.0,1.0,RelativeLFR);
		float AxillaryPriorityGradient =  1 - AxillaryGradientCurve->Eval(RelativeLFR);//FMath::Lerp(0.0,1.0,RelativeLFR);

		FPVGrowerPhyllotaxyParams PhyllotaxyLocal;
		InGrowerParams.GetPhyllotaxy(PhyllotaxyLocal, Generation);

		FPVAuxinParams AuxinLocal;
		InGrowerParams.GetAuxin(AuxinLocal, Generation);

		FPVPhototropismParams PhototropismLocal;
		InGrowerParams.GetPhototropism(PhototropismLocal, Generation);

		FPVGrowerBifurcationParams BifurcationLocal;
		InGrowerParams.GetBifurcation(BifurcationLocal, Generation);
		
		bool bIsSprout = false;
		
		if (Generation > 1)
		{
			ApicalPriorityMultiplier = ApicalChildPriorityMultiplier;
			AxillaryPriorityMultiplier = AxillaryChildPriorityMultiplier;

			EPVRampBasis ApicalRampBasis = InGrowerParams.TrunkGrowth.ApicalRampBasis;
			if (InGrowerParams.TrunkGrowth.bApicalUseChildGradient)
			{
				ApicalRampBasis = InGrowerParams.TrunkGrowth.ApicalChildRampBasis;
			}
			
			if (!InGrowerParams.TrunkGrowth.bApicalUseChildGradient )
			{
				if (ApicalRampBasis == EPVRampBasis::PlantTargetLength)
				{
					ApicalPriorityGradient = ApicalGradientCurve->Eval(RelativeLFR);
				}
				else
				{
					ApicalPriorityGradient = ApicalGradientCurve->Eval(RelativeLFT);
				}
			}
			else
			{
				if (ApicalRampBasis == EPVRampBasis::PlantTargetLength)
				{
					ApicalPriorityGradient = ApicalGradientChildCurve->Eval(RelativeLFR);
				}

				else
				{
					ApicalPriorityGradient = ApicalGradientChildCurve->Eval(RelativeLFT);
				}
			}
			

			if (!InGrowerParams.TrunkGrowth.bAxillaryUseChildGradient )
			{
				AxillaryPriorityGradient = 1 - AxillaryGradientCurve->Eval(RelativeLFR);
			}

			EPVRampBasis AxillaryRampBasis = InGrowerParams.TrunkGrowth.AxillaryRampBasis;
			if (InGrowerParams.TrunkGrowth.bAxillaryUseChildGradient)
			{
				AxillaryRampBasis = InGrowerParams.TrunkGrowth.AxillaryChildRampBasis;
			}
			
			else if (AxillaryRampBasis == EPVRampBasis::PlantTargetLength)
			{
				AxillaryPriorityGradient = 1 - AxillaryGradientChildCurve->Eval(RelativeLFR);//1 - FMath::Lerp(0,1,RelativeLFT);
			}

			else
			{
				AxillaryPriorityGradient = 1 - AxillaryGradientChildCurve->Eval(RelativeLFT);
			}
		}

		if (Point->Bud.Status.ApicalMeristem == 1)
		{
			//FRandomStream ApicalPriorityRandomStream(Point->Bud.BudNumber + Cycle + 220);
			//float ApicalPriorityRandom = 1 - ApicalPriorityRandomStream.FRand();

			float ApicalPriorityRandom = FMath::Clamp(RandomValue(Point->Bud.BudNumber + InCycle + 1 + InGrowerParams.RandomSeed + 122), 0.0, 1.0);
			float ApicalPriority = ApicalPriorityRandom * ApicalPriorityMultiplier;
			
			if (ApicalPriority > (1.0 - ApicalPriorityGradient))
			{
				bIsSprout = true;
			}

			/*UE_LOGF(LogTemp, Log, "ApicalPriorityRandom %f ApicalPriorityMultiplier %f ApicalPriority %f ApicalPriorityGradient %f Point %ls bSprout %i BudNumber %i RelativeLFR %f",
				ApicalPriorityRandom, ApicalPriorityMultiplier, ApicalPriority, ApicalPriorityGradient, *Point->Position.ToString(), (int)bIsSprout, Point->Bud.BudNumber, RelativeLFR);*/

			float CodominantRandom = RandomStream.FRand();
			if (bIsSprout && BifurcationLocal.bEnableBifurcation && Point->Bud.HormoneLevels.Cytokinin > 0.99 && CodominantRandom > (1.0 - BifurcationLocal.SplitThreshold))
			{
				Point->Bud.Status.ApicalMeristem = 0;
				Point->Bud.Status.Codominant = 1;
			}
		}
		else if (Point->Bud.Status.Axillary == 1)
		{
			//FRandomStream AxillaryRandomStream(Point->Bud.BudNumber + 1);
			//float AxillaryRandom = 1 - AxillaryRandomStream.FRand();

			float AxillaryRandom = RandomValue(Point->Bud.BudNumber + InGrowerParams.RandomSeed + 136);

			if (InGrowerParams.TrunkGrowth.bAxillaryRetry)
			{
				//FRandomStream AxillaryRetryRandomStream(Point->Bud.BudNumber + Cycle + 1);
				//float AxillaryRetryRandom = 1 - AxillaryRandomStream.FRand();
				float AxillaryRetryRandom = RandomValue(Point->Bud.BudNumber + InCycle + 1 + InGrowerParams.RandomSeed + 136);
				AxillaryRandom = AxillaryRetryRandom;
			}

			float AxillaryPriority = AxillaryRandom * AxillaryPriorityMultiplier;
			if (AxillaryPriority > AxillaryPriorityGradient)
			{
				bIsSprout = true;
			}

			if (Generation >= InGrowerParams.TrunkGrowth.MaxGeneration)
			{
				bIsSprout = false;
			}
			
			float AuxinPriority = 1 - AuxinLocal.ApicalDominance;
			if ( Point->Bud.HormoneLevels.Apical > AuxinPriority  || Point->Bud.HormoneLevels.Radical > 0.05)
			{
				bIsSprout = false;
			}

			if (Point->Bud.LightDetected.Available < PhototropismLocal.LightRequirement)
			{
				bIsSprout = false;
			}

			float MinGravityDot = InGrowerParams.GravityParams.MinGravitationalDot;
			MinGravityDot -= 0.5;
			MinGravityDot *= 2.0;
			FVector Axillary = Point->Bud.Direction.Axillary;
			float GravityDot = Axillary.Dot(FVector{0,0,1});
			if (GravityDot < MinGravityDot)
			{
				bIsSprout = false;
			}

			/*UE_LOGF(LogTemp, Log, "AxillaryRandom %f AxillaryPriorityMultiplier %f AxillaryPriority %f AxillaryPriorityGradient %f Point %ls bSprout %i BudNumber %i RelativeLFR %f",
				AxillaryRandom, AxillaryPriorityMultiplier, AxillaryPriority, AxillaryPriorityGradient, *Point->Position.ToString(), (int)bIsSprout, Point->Bud.BudNumber, RelativeLFR);
				*/

		}

		if (bIsSprout)
		{
			Sprouts.Add(Point);
			//UE_LOGF(LogTemp, Log, "Sprout Position %ls", *Point->Position.ToString());
		}
	}

	return Sprouts;
}

void FPVGrower::SetNumberOfTriggered(TArray<UPVGrowerPoint*> Sprouts, const FPVGrowerParams& InGrowerParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::SetNumberOfTriggered);
	for (auto Sprout : Sprouts)
	{
		const int Generation = Sprout->Bud.Development.Generation;
		
		FPVGrowerPhyllotaxyParams PhyllotaxyLocal;
		InGrowerParams.GetPhyllotaxy(PhyllotaxyLocal, Generation);

		FPVGrowerBifurcationParams BifurcationLocal;
		InGrowerParams.GetBifurcation(BifurcationLocal, Generation);
		
		int NumTriggered = 0;
		FRandomStream RandomStream(Sprout->Bud.BudNumber + InGrowerParams.RandomSeed + 9001);
		float Random = RandomStream.FRand();
			
		if (Sprout->Bud.Status.Codominant == 1)
		{
			FVector2f NewRange = FVector2f(BifurcationLocal.SplitMin, BifurcationLocal.SplitMax);
			float RandomBud = FMath::GetMappedRangeValueClamped( FVector2f(0.0,1.0),NewRange ,Random);//FMath::Lerp(PhyllotaxyLocal.CodominantBudMin, PhyllotaxyLocal.CodominantBudMax ,Random);
			NumTriggered = FMath::RoundToInt(RandomBud);
			Sprout->Bud.Status.Triggered = 1;
			Sprout->Bud.Status.NumTriggered = NumTriggered;
		}
		if (Sprout->Bud.Status.Axillary == 1)
		{
			//Replace Axillary with bud min
			FVector2f NewRange = FVector2f(GetPhyllotaxyMin(PhyllotaxyLocal), GetPhyllotaxyMax(PhyllotaxyLocal));
			float RandomBud = FMath::GetMappedRangeValueClamped( FVector2f(0.0,1.0),NewRange ,Random);//FMath::Lerp(PhyllotaxyLocal.CodominantBudMin, PhyllotaxyLocal.CodominantBudMax ,Random);
			
			//float RandomBud = FMath::Lerp(GetPhyllotaxyMin(PhyllotaxyLocal), GetPhyllotaxyMax(PhyllotaxyLocal) ,Random);
			NumTriggered = FMath::RoundToInt(RandomBud);
			Sprout->Bud.Status.Triggered = 1;
			Sprout->Bud.Status.NumTriggered = NumTriggered;
		}
	}
}

uint32 FPVGrower::GetPhyllotaxyMin(const FPVGrowerPhyllotaxyParams& InPhyllotaxy)
{
	if (InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Whorled ) return InPhyllotaxy.Min;
	else if (InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Alternate) return 1;
	else if (InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Spiral) return 1;
	return 2;
}

uint32 FPVGrower::GetPhyllotaxyMax(const FPVGrowerPhyllotaxyParams& InPhyllotaxy)
{
	if (InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Whorled ) return InPhyllotaxy.Max;
	else if (InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Alternate) return 1;
	else if (InPhyllotaxy.Type == EPVGrowthPhyllotaxyType::Spiral) return 1;
	return 2;
}

void FPVGrower::SetAxialElongation(TArray<UPVGrowerPoint*> Sprouts, const FPVTrunkGrowthParams& InAxialParams, const int InRandomSeed)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::SetAxialElongation);
	for (auto Sprout : Sprouts)
	{
		int Generation = Sprout->Bud.Development.Generation;
		uint32 NumTriggered = Sprout->Bud.Status.NumTriggered;
		FPVTrunkGrowthParams AxialElongationLocal = InAxialParams;

		float Elongation = AxialElongationLocal.SegmentLength * AxialElongationLocal.BranchScale;

		if (Sprout->Bud.Status.ApicalMeristem == 1 || Sprout->Bud.Status.Codominant == 1)
		{
			if (Generation == 1)
			{
				Elongation = AxialElongationLocal.SegmentLength;
			}
		}

		if (Sprout->Bud.Status.ApicalMeristem == 1)
		{
			NumTriggered = 1;
		}

		float LightAdjustedElongation = 0;
		if (AxialElongationLocal.LengthLightImpact > 0)
		{
			LightAdjustedElongation = Elongation + (Elongation * (1 - Sprout->Bud.LightDetected.Available));
		}
		else
		{
			LightAdjustedElongation = Elongation * Sprout->Bud.LightDetected.Available;
		}

		LightAdjustedElongation = FMath::Lerp(Elongation, LightAdjustedElongation, FMath::Abs(AxialElongationLocal.LengthLightImpact));

		float AxillaryAuxinInhibition = Sprout->Bud.HormoneLevels.AxillaryInhibition;
		AxillaryAuxinInhibition = 1 - AxillaryAuxinInhibition;

		float AuxinAdjustedAxialElongation = FMath::Lerp(0, LightAdjustedElongation, AxillaryAuxinInhibition);
		AuxinAdjustedAxialElongation = FMath::Lerp(LightAdjustedElongation,AuxinAdjustedAxialElongation,AxialElongationLocal.AuxinImpact);

		float SeedPScaleRatio = FMath::Lerp(1, Sprout->SeedInfo.PScaleRatio, AxialElongationLocal.SeedScaleEffect);

		Sprout->NextAxialElongations.Empty();
		
		for (uint32 i=0; i < NumTriggered; i++)
		{
			int32 RandomSeedLocal = InRandomSeed + 2997 + Sprout->Bud.BudNumber + i;
			FRandomStream RandomStream(RandomSeedLocal);
			float Random = RandomStream.FRand();
			float RandomAxialMultiplier = FMath::Lerp((1 - AxialElongationLocal.LightRandomness) , (1 + AxialElongationLocal.LightRandomness),Random);
			float RandomAdjustedAxialElongation = AuxinAdjustedAxialElongation * RandomAxialMultiplier;

			Sprout->NextAxialElongations.Add(FMath::Lerp(RandomAdjustedAxialElongation, InAxialParams.SegmentLength, AxialElongationLocal.LengthBias) * SeedPScaleRatio);
		}
	}
}

void FPVGrower::ApicalGrowthCycle(const FPVGrowerParams& InGrowerParams, TArray<UPVGrowerPoint*> Sprouts, UPVGrowerData* Skeleton)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::ApicalGrowthCycle);
	for (auto Sprout : Sprouts)
	{
		if (Sprout->Bud.Status.ApicalMeristem == 1)
		{
			int Generation = Sprout->Bud.Development.Generation;

			FPVPhototropismParams PhototropismLocal;
			InGrowerParams.GetPhototropism(PhototropismLocal, Generation);

			FPVDirectionalParams DirectionalParamsLocal;
			InGrowerParams.GetDirectionalParams(DirectionalParamsLocal, Generation);

			FPVGrowerPhyllotaxyParams PhyllotaxyLocal;
			InGrowerParams.GetPhyllotaxy(PhyllotaxyLocal, Generation);

			FPVAuxinParams AuxinLocal;
			InGrowerParams.GetAuxin(AuxinLocal, Generation);

			FPVGrowerBifurcationParams BifurcationLocal;
			InGrowerParams.GetBifurcation(BifurcationLocal, Generation);
			
			//Set Axillary Direction
			float AxillaryRotation = InGrowerParams.GetPhyllotaxyAngle(PhyllotaxyLocal);
			FQuat Quaternion = FQuat(Sprout->Bud.Direction.Apical.GetSafeNormal(), AxillaryRotation);
			FVector NextAxillaryDirection = Quaternion.RotateVector(Sprout->Bud.Direction.Axillary);
			
			Sprout->NextAxillaryDirection = NextAxillaryDirection;

			float ApicalPhototropism = PhototropismLocal.Apical;
			float ApicalPhototropismBias = InGrowerParams.Phototropism.ApicalBias;

			FVector BudLightOptimalDirection = Sprout->Bud.Direction.LightOptimal;
			FVector BudLightSubOptimalDirection = Sprout->Bud.Direction.LightSubOptimal;
			
			//Stagger apical
			FVector NextApicalDirection = Sprout->Bud.Direction.Apical;
			
			if (Sprout->Bud.Status.Seed != 1 && PhyllotaxyLocal.Stagger > 0.001f)
			{
				float AxilDegree = PhyllotaxyLocal.AxilAngle;
				FVector CrossVec = FVector::CrossProduct(NextApicalDirection,NextAxillaryDirection);
				//UE_LOGF(LogProceduralVegetation, Log, "CrossVec : %ls", *CrossVec.ToString());
				//CrossVec = !bInvert ? CrossVec :-CrossVec;
				float StaggerAmount = FMath::DegreesToRadians((90.0 - AxilDegree) * PhyllotaxyLocal.Stagger);
				FQuat Rotation = FQuat(CrossVec,StaggerAmount);
				NextApicalDirection = Rotation.RotateVector(NextApicalDirection);
				//UE_LOGF(LogProceduralVegetation, Log, "Stagger Amount : %f , NextApicalDirection : %ls , Sprout->Bud.Direction.Axillary : %ls , NextAxillaryDirection : %ls" , StaggerAmount, *NextApicalDirection.ToString(), *Sprout->Bud.Direction.Axillary.ToString(), *NextAxillaryDirection.ToString());
				//UE_LOGF(LogProceduralVegetation, Log, "Stagger Amount : %f , NextApicalDirection : %ls , Sprout->Bud.Direction.Apical : %ls , NextAxillaryDirection : %ls" , StaggerAmount, *NextApicalDirection.ToString(), *Sprout->Bud.Direction.Apical.ToString(), *NextAxillaryDirection.ToString());
			}

			//Apical Randomness
			float RandomApicalAngle = DirectionalParamsLocal.ApicalRandomAngle;
			RandomApicalAngle = RandomApicalAngle * 0.017453;
			float RandomSeedLocal = Sprout->Bud.BudNumber + InGrowerParams.RandomSeed + 28776;
			FRandomStream RandomStream(RandomSeedLocal);
			float RandomX = RandomStream.FRand();
			float RandomY = RandomStream.FRand();
			NextApicalDirection = SampleDirectionCone(NextApicalDirection, RandomApicalAngle, RandomX, RandomY);
			

			//Apical Phototropism
			if (Sprout->Bud.LightDetected.Collision == 1)
			{
				ApicalPhototropism = 1;
				ApicalPhototropismBias = 0;
			}

			if (Sprout->Bud.Status.Seed == 1)
			{
				ApicalPhototropism = 0;
			}
			
			FVector LightDirection = FMath::Lerp(BudLightOptimalDirection,BudLightSubOptimalDirection,ApicalPhototropismBias);
			NextApicalDirection = FMath::Lerp(NextApicalDirection,LightDirection,ApicalPhototropism);
			NextApicalDirection.Normalize();
			
			//Set Guide Direction
			/*if(foundGuide==1 && useGuide==1){
				NextApicalDirection = FVector::Normalize(FVector::SlerpNormals(NextApicalDirection,GuideDirection,GuideFollowStrength));
			}*/
    
			Sprout->NextApicalDirection = NextApicalDirection;


			//Set Next UpVector
			FQuat RotationDihedral = FQuat::FindBetweenVectors(Sprout->Bud.Direction.Apical,NextApicalDirection);
			FVector NextUpVector = RotationDihedral.RotateVector(Sprout->Bud.Direction.UpVector);
			
			Sprout->NextUpVector = NextUpVector;

			//UE_LOGF(LogProceduralVegetation, Log, "NextAxillaryDirection Before : %ls", *NextAxillaryDirection.ToString());
			NextAxillaryDirection = RotationDihedral.RotateVector(NextAxillaryDirection);
			//UE_LOGF(LogProceduralVegetation, Log, "NextAxillaryDirection After : %ls", *NextAxillaryDirection.ToString());
			
			Sprout->NextAxillaryDirection = NextAxillaryDirection;

			//Create New Sprout

			float CytokininBuildup = BifurcationLocal.CytokininBuildup;
			float CytokininRandom = BifurcationLocal.CytokininRandomness;
    
			// Calculate Cytokinin
			FRandomStream CytokininRandomStream(Sprout->Bud.BudNumber + InGrowerParams.RandomSeed + 277);
			float CytokininRandomValue = CytokininRandomStream.FRand() * CytokininBuildup;
			float NewCytokininValue = Sprout->Bud.HormoneLevels.Cytokinin + FMath::Lerp(CytokininBuildup,CytokininRandomValue,CytokininRandom);
			NewCytokininValue = FMath::Clamp(NewCytokininValue,0,1);

			check(Sprout->NextAxialElongations.IsValidIndex(0));
				
			// Create New Shoot
			FVector OldPos = Sprout->Position;
			FVector NewPos = OldPos + (NextApicalDirection * Sprout->NextAxialElongations[0]);

			UPVGrowerPoint* NewPoint = NewObject<UPVGrowerPoint>(Skeleton, UPVGrowerPoint::StaticClass(), NAME_None, RF_Transactional);
			//int NewMeristem = addpoint(0,newPos);
			//int SourceInternodePrim = pointprims(0,@ptnum)[0];
			//addvertex(0,sourceInternodePrim,newMeristem);
			NewPoint->bNewBud = true;
			NewPoint->Position = NewPos;
			// Set New Shoot Attributes
			NewPoint->Bud.Status = FPVBudStatus{1,0,0,0,1,0,0,0,0,0};
			NewPoint->Bud.HormoneLevels = FPVBudHormoneLevels{1,1,Sprout->Bud.HormoneLevels.AxillaryInhibition,Sprout->Bud.HormoneLevels.Radical/2,0,NewCytokininValue};
			NewPoint->Bud.Development = FPVBudDevelopment{Sprout->Bud.Development.Generation,0,Sprout->Bud.Development.BranchAge,0,0,0};
			NewPoint->Bud.Direction = FPVBudDirection{NextApicalDirection, NextAxillaryDirection, NextApicalDirection, NextApicalDirection, NextApicalDirection, NextUpVector};
			NewPoint->Bud.LateralMeristem = FPVBudLateralMeristem(0,Sprout->Bud.LateralMeristem.Multiplier,0,0,0,(Sprout->LengthFromRoot + Sprout->NextAxialElongations[0]),0);
			NewPoint->SeedInfo = Sprout->SeedInfo;
			NewPoint->LengthFromRoot = NewPoint->Bud.LateralMeristem.RootDistance;
			NewPoint->Bud.LightDetected = Sprout->Bud.LightDetected;
			
			if(Sprout->Bud.Development.Generation > 1)
			{
				NewPoint->LengthFromTrunk = GetLengthFromTrunk(Sprout, Sprout->NextAxialElongations[0]);
			}
			// Set Old Shoot Attributes
			Sprout->Bud.Status.ApicalMeristem = 0;
			Sprout->Bud.Status.Axillary = 1;
			Sprout->Neighbors.Add(NewPoint);
			NewPoint->Neighbors.Add(Sprout);
			Skeleton->AddPoint(NewPoint, Sprout->Primitive);

			//UE_LOGF(LogTemp, Log, "NewPoint Position %ls", *NewPoint->Position.ToString());
		}
	}
}

FVector FPVGrower::SampleDirectionCone(const FVector &Direction, float AngleInRadians, float RandomX, float RandomY)
{
	FVector NormalizeDirection = Direction.GetSafeNormal();

	float CosAngle = FMath::Cos(AngleInRadians);
	float Z = FMath::Lerp(1.0, CosAngle, RandomX);

	float Phi = RandomY * 2.0f * PI;
	float SinAngle = FMath::Sqrt(1.0f - Z  * Z);

	FVector Dir(SinAngle * FMath::Cos(Phi), SinAngle * FMath::Sin(Phi), Z);
	//Dir = FVector(-Dir.X, Dir.Z, Dir)

	FVector Up = NormalizeDirection;
	FVector Forward, Right;
	Up.FindBestAxisVectors(Forward, Right);

	FVector Result = Dir.X * Forward + Dir.Y * Right + Dir.Z * Up;
	return Result.GetSafeNormal();
}

void FPVGrower::AxillaryGrowthCycle(const FPVGrowerParams& InGrowerParams, TArray<UPVGrowerPoint*> Sprouts, UPVGrowerData* Skeleton)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::AxillaryGrowthCycle);
	for (auto Sprout : Sprouts)
	{
		if (Sprout->Bud.Status.Axillary == 1)
		{
			int Generation = Sprout->Bud.Development.Generation;
			FPVPhototropismParams PhototropismLocal;
			InGrowerParams.GetPhototropism(PhototropismLocal, Generation);
			FPVDirectionalParams DirectionalParamsLocal;
			InGrowerParams.GetDirectionalParams(DirectionalParamsLocal, Generation);
			FPVGrowerPhyllotaxyParams PhyllotaxyLocal;
			InGrowerParams.GetPhyllotaxy(PhyllotaxyLocal, Generation);
			FPVAuxinParams AuxinLocal;
			InGrowerParams.GetAuxin(AuxinLocal, Generation);
			
			bool bReset = InGrowerParams.BranchPhyllotaxy.bReset && !InGrowerParams.bBranchPhyllotaxySameAsTrunk;
			float Offset = PhyllotaxyLocal.Offset;
			
			if (Generation > 1)
			{
				bReset = false;
				//Offset = 0.0;
			}

			FPVBud& Bud = Sprout->Bud;

			int32 NumTriggered = Bud.Status.NumTriggered;
			float MultiBranchDegree = NumTriggered == 0 ? 0 : 360/float(NumTriggered);

			TArray<FVector> NextApicalDirectionArray, NextAxillaryDirectionArray, NextUpVectorArray;
			FVector RotationAxis, RotatedDirection, NoAxilApicalDirection;
			float RotationAngle = 0.0f;
			FQuat RotationDihedral;

			// ------------------- Start Vector Modification -------------- //
			FVector PhototropicDirection = FMath::Lerp(Bud.Direction.LightOptimal, Bud.Direction.LightSubOptimal, PhototropismLocal.AxillaryBias);
			PhototropicDirection.Normalize();

			check(Sprout->Primitive);
			int32 BranchNumber = Sprout->Primitive->BranchNumber;
			TArray<int32> Parents = Sprout->Primitive->Parents;
			Parents.Add(BranchNumber);
			int32 RandomSeedLocal = InGrowerParams.RandomSeed + 2997;
			
			for (int32 i=0; i < NumTriggered; i++)
			{
				FVector BaseAxillaryDirection = FVector::CrossProduct(Bud.Direction.Apical,Bud.Direction.Axillary);
				FVector BaseApicalDirection = Bud.Direction.Apical;
				FVector BaseUpVector = BaseApicalDirection;

				// Set Base Axillary Direction
				if (!bReset)
				{
					RotationAxis = BaseApicalDirection;
					RotationAngle = InGrowerParams.GetPhyllotaxyAngle(PhyllotaxyLocal);
					FQuat Rotation = FQuat(RotationAxis,RotationAngle);
					RotatedDirection = Rotation.RotateVector(Bud.Direction.Axillary);
					RotationDihedral = FQuat::FindBetweenVectors(BaseApicalDirection,Bud.Direction.Axillary);
					BaseAxillaryDirection = RotationDihedral.RotateVector(RotatedDirection);
				}
        
				// Set Next Generation Up Vector
				if (Generation > 1)
				{
					RotationDihedral = FQuat::FindBetweenVectors(BaseApicalDirection,Bud.Direction.Axillary);
					BaseUpVector = RotationDihedral.RotateVector(Bud.Direction.UpVector);
				}
				// Offset Axillary Offset
				else
				{
					RotationAxis = Bud.Direction.Axillary;//BaseApicalDirection;
					RotationAngle = FMath::DegreesToRadians((Offset/*PhyllotaxyLocal.Offset*/));
					FQuat Rotation = FQuat(RotationAxis,RotationAngle);
					BaseAxillaryDirection = Rotation.RotateVector(BaseAxillaryDirection);
					BaseUpVector = Rotation.RotateVector(BaseUpVector);
				}

				// Set Axil Angle
				RotationAxis = FVector::CrossProduct(Bud.Direction.Apical,Bud.Direction.Axillary);
				RotationAngle = FMath::DegreesToRadians(static_cast<float>(-PhyllotaxyLocal.AxilAngle));
				FQuat Rotation = FQuat(RotationAxis,RotationAngle);
				BaseApicalDirection = Rotation.RotateVector(Bud.Direction.Axillary);
				BaseAxillaryDirection = Rotation.RotateVector(BaseAxillaryDirection);
				BaseUpVector = Rotation.RotateVector(BaseUpVector);
				
				// Rotate Multi Branch
				RotationAxis = Bud.Direction.Apical;
				RotationAngle = FMath::DegreesToRadians((MultiBranchDegree * i));
				Rotation = FQuat(RotationAxis,RotationAngle);
				BaseApicalDirection = Rotation.RotateVector(BaseApicalDirection);
				BaseAxillaryDirection = Rotation.RotateVector(BaseAxillaryDirection);
				BaseUpVector = Rotation.RotateVector(BaseUpVector);
				NoAxilApicalDirection = Rotation.RotateVector(Bud.Direction.Axillary);

				//Stagger
				
				if (Sprout->Bud.Status.Seed != 1 && PhyllotaxyLocal.Stagger > 0.001f)
				{
					float AxilDegree = PhyllotaxyLocal.AxilAngle;
					FVector CrossVec = FVector::CrossProduct(Bud.Direction.Apical, BaseApicalDirection);
					//CrossVec = !bInvert ? CrossVec :-CrossVec;
					float StaggerAmount = FMath::DegreesToRadians((90.0 - AxilDegree) * 0.5f * PhyllotaxyLocal.Stagger);
					Rotation = FQuat(CrossVec,StaggerAmount);
					BaseApicalDirection = Rotation.RotateVector(BaseApicalDirection);
				}
				
				// Flatten Phyllotaxy
				if (PhyllotaxyLocal.Flatten > 0.001f)
				{
					FVector CrossVector = FVector::CrossProduct(Bud.Direction.Apical,Bud.Direction.UpVector);
					float FlattenDot = FVector::DotProduct(NoAxilApicalDirection,Bud.Direction.UpVector);
					float CrossDot = FVector::DotProduct(NoAxilApicalDirection,CrossVector);
					if (CrossDot < 0)
					{
						FlattenDot *= -1;
					}
					RotationAxis = Bud.Direction.Apical;
					RotationAngle = FMath::DegreesToRadians((PhyllotaxyLocal.Flatten * 90) * FlattenDot);
					Rotation = FQuat(RotationAxis,RotationAngle);
					BaseApicalDirection = Rotation.RotateVector(BaseApicalDirection);
					BaseAxillaryDirection = Rotation.RotateVector(BaseAxillaryDirection);
					BaseUpVector = Rotation.RotateVector(BaseUpVector);
				}

				//Adjust Directions for phototropism
				FRandomStream RandomStream(Sprout->Bud.BudNumber + i + InGrowerParams.RandomSeed + 298);
				float RandomX = RandomStream.FRand();
				float RandomY = RandomStream.FRand();
				RotationAngle = FMath::DegreesToRadians(DirectionalParamsLocal.AxillaryRandomAngle);
				FVector AdjustedDirection = SampleDirectionCone(BaseApicalDirection,RotationAngle,RandomX, RandomY);
				AdjustedDirection = FMath::Lerp(AdjustedDirection,PhototropicDirection,PhototropismLocal.Axillary);
				AdjustedDirection.Normalize();

				RotationDihedral = FQuat::FindBetweenVectors(BaseApicalDirection,AdjustedDirection);

				BaseApicalDirection = RotationDihedral.RotateVector(BaseApicalDirection);
				BaseAxillaryDirection = RotationDihedral.RotateVector(BaseAxillaryDirection);
				BaseUpVector = RotationDihedral.RotateVector(BaseUpVector);

				check(Sprout->NextAxialElongations.IsValidIndex(i));
				
				//Create New Sprouts
				FVector OldPosition = Sprout->Position;
				FVector NewPosition = OldPosition + (BaseApicalDirection * Sprout->NextAxialElongations[i]);

				float ParentDot = FVector::DotProduct(BaseApicalDirection, Bud.Direction.Apical);
				float AdjustedLateralMultiplier = Bud.LateralMeristem.Multiplier * (1/float(NumTriggered));
				float LateralMultiplier = FMath::Lerp(Bud.LateralMeristem.Multiplier,AdjustedLateralMultiplier,InGrowerParams.TrunkGrowth.WhorledRadiusImpact);
				// ------------- Set New Attributes --------------//
				UPVGrowerPoint* NewPoint = NewObject<UPVGrowerPoint>(Skeleton, UPVGrowerPoint::StaticClass(), NAME_None, RF_Transactional);
				//int NewMeristem = addpoint(0,newPos);
				//int SourceInternodePrim = pointprims(0,@ptnum)[0];
				//addvertex(0,sourceInternodePrim,newMeristem);
				NewPoint->bNewBud = true;
				NewPoint->Position = NewPosition;
				// Set New Shoot Attributes
				NewPoint->Bud.Status = FPVBudStatus{1,0,0,0,1,0,0,0,0,0};
				NewPoint->Bud.HormoneLevels = FPVBudHormoneLevels{1,1,Sprout->Bud.HormoneLevels.Ethylene,AuxinLocal.RadicalAuxin,0,0};
				NewPoint->Bud.Development = FPVBudDevelopment{Sprout->Bud.Development.Generation + 1,0,0,0,0,0};
				NewPoint->Bud.Direction = FPVBudDirection{BaseApicalDirection, BaseAxillaryDirection, BaseApicalDirection, BaseApicalDirection, BaseApicalDirection, BaseUpVector};
				NewPoint->Bud.LateralMeristem = FPVBudLateralMeristem(0,LateralMultiplier,0,0,ParentDot,(Sprout->LengthFromRoot + Sprout->NextAxialElongations[i]),0);
				NewPoint->LengthFromRoot = NewPoint->Bud.LateralMeristem.RootDistance;
				NewPoint->Bud.LightDetected = Sprout->Bud.LightDetected;

				if(Sprout->Bud.Development.Generation > 1)
				{
					NewPoint->LengthFromTrunk = GetLengthFromTrunk(Sprout, Sprout->NextAxialElongations[i]);
				}
				
				NewPoint->SeedInfo = Sprout->SeedInfo;
				NewPoint->Neighbors.Add(Sprout);
				Sprout->Neighbors.Add(NewPoint);

				check(Sprout->Primitive);
				UPVGrowerPrimitive* Primitive = NewObject<UPVGrowerPrimitive>(Skeleton, UPVGrowerPrimitive::StaticClass(), NAME_None, RF_Transactional);
				Primitive->BranchParentNumber = BranchNumber;
				Primitive->BranchSourceBudNumber = Bud.BudNumber;

				//UE_LOGF(LogProceduralVegetation, Log, "Source Bud Number : %i Apical : %ls NewBud Number : %i Apical : %ls", Bud.BudNumber, *Bud.Direction.Apical.ToString(), NewPoint->Bud.BudNumber, *NewPoint->Bud.Direction.Apical.ToString());
				Primitive->PlantNumber = Sprout->Primitive->BranchNumber;
				Primitive->Parents = Parents;
				Primitive->PlantNumber=  Sprout->Primitive->PlantNumber;
				Primitive->bNewBranch = true;
				Skeleton->AddPoint(NewPoint, Primitive);
				//Skeleton->Primitives.Add(Primitive);
				Skeleton->AddPrimitive(Primitive);

				Bud.Status.Dormant = 0;
				//i[]@budStatus[4] = 0;
				// Set Vector Arrays
				//NextApicalDirectionArray[i] = BaseApicalDirection;
				//NextAxillaryDirectionArray[i] = BaseAxillaryDirection;
				//NextUpVectorArray[i] = BaseUpVector;
			}
		}

		if (Sprout->Bud.Status.Codominant == 1)
		{
			CodominantGrowth(InGrowerParams, Sprout, Skeleton);
		}
	}
}

void FPVGrower::CodominantGrowth(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Sprout, UPVGrowerData* Skeleton)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::CodominantGrowth);
	
	SetApicalDirectionForCodominantSprout(InGrowerParams, Sprout);
	CodominantSprouting(InGrowerParams, Sprout, Skeleton);
}

void FPVGrower::SetApicalDirectionForCodominantSprout(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Sprout)
{
    //if found Guide
    bool bFoundGuide = false; //TODO : Find guid in Sprout

	const auto& Bud = Sprout->Bud;
    //get the Generation, Phototropism and random angle arrays
    // Set Phyllotaxy
	int Generation = Bud.Development.Generation;

	FPVPhototropismParams PhototropismLocal;
	InGrowerParams.GetPhototropism(PhototropismLocal, Generation);

	FPVDirectionalParams DirectionalParamsLocal;
	InGrowerParams.GetDirectionalParams(DirectionalParamsLocal, Generation);

    //Based on bud light collision set the apical phototropism and its bias
    //Set Phototropism
    if (Bud.LightDetected.Collision == 1)
    {
        PhototropismLocal.Apical = 1;
        PhototropismLocal.ApicalBias = 0;
    }

    //for seed bud set the apical phototropism = 0
    if (Bud.Status.Seed == 1)
    {
        PhototropismLocal.Apical = 0;
    }

    //Get the Light direction vector
    FVector LightDirection = FMath::Lerp(Bud.Direction.LightOptimal, Bud.Direction.LightSubOptimal, PhototropismLocal.ApicalBias);

    //Set Apical Direction
    auto ApicalRandomAngle = FMath::DegreesToRadians(DirectionalParamsLocal.ApicalRandomAngle);
    float RandomSeedLocal = Bud.BudNumber + InGrowerParams.RandomSeed + 28776;
	FRandomStream RandomStream(RandomSeedLocal);
	float RandomX = RandomStream.FRand();
	float RandomY = RandomStream.FRand();
    FVector NextApicalDirection = SampleDirectionCone(Bud.Direction.Apical, ApicalRandomAngle, RandomX, RandomY);
	NextApicalDirection = FMath::Lerp(NextApicalDirection,LightDirection,PhototropismLocal.Apical);
	NextApicalDirection.Normalize();
    
    //Set Guide Direction
    if(bFoundGuide && InGrowerParams.GuideSettings.bUse)
    {
		NextApicalDirection = FMath::Lerp(NextApicalDirection,Bud.Direction.CurveGuide, InGrowerParams.GuideSettings.GuidFollowStrength);
	}

	Sprout->NextApicalDirection = NextApicalDirection;
}

void FPVGrower::CodominantSprouting(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Sprout, UPVGrowerData* Skeleton)
{
	auto& Bud = Sprout->Bud;
    // Get Generation Based Arrays
	int Generation = Bud.Development.Generation;

	FPVGrowerPhyllotaxyParams PhyllotaxyLocal;
	InGrowerParams.GetPhyllotaxy(PhyllotaxyLocal, Generation);

	FPVGrowerBifurcationParams BifurcationLocal;
	InGrowerParams.GetBifurcation(BifurcationLocal, Generation);
    
    int32 NumTriggered = Bud.Status.NumTriggered;

	float CodominantRotationDegreeBase = FMath::DegreesToRadians(NumTriggered == 0 ? 0 : 360.0f/NumTriggered);
    float CodominantAxilDegreeBase = -FMath::DegreesToRadians(BifurcationLocal.SplitAngle);
    float CodominantBiasDegreeBase = -FMath::DegreesToRadians((90.0 - BifurcationLocal.SplitAngle) * BifurcationLocal.SplitBias);
	
    for (int i=0; i<NumTriggered; i++)
    {
        //vector codominantAxillaryDirection = axillaryDirection;
        //vector codominantUpVecDirection = upVecDirection;
        
        FVector AxilRotationAxis = FVector::CrossProduct(Sprout->NextApicalDirection,Bud.Direction.Axillary);
    	AxilRotationAxis.Normalize();
        FMatrix AxilRotationMatrix = FQuat(AxilRotationAxis, CodominantAxilDegreeBase).ToMatrix();
        FVector AxilAdjustedApicalDirection = AxilRotationMatrix.TransformVector(Bud.Direction.Axillary);
        
        FMatrix VectorDihedralMatrix = FQuat::FindBetweenNormals(Bud.Direction.Apical.GetSafeNormal(), AxilAdjustedApicalDirection.GetSafeNormal()).ToMatrix();
        FVector CodominantAxillaryDirection = VectorDihedralMatrix.TransformVector(Bud.Direction.Axillary);
        FVector CodominantUpVecDirection =  VectorDihedralMatrix.TransformVector(Bud.Direction.UpVector);
        
        float CodominantRotationDegree = CodominantRotationDegreeBase*i;
        FMatrix CodominantRotationMatrix = FQuat(Sprout->NextApicalDirection, CodominantRotationDegree).ToMatrix();
        FVector CodominantApicalDirection = CodominantRotationMatrix.TransformVector(AxilAdjustedApicalDirection);
        CodominantAxillaryDirection = CodominantRotationMatrix.TransformVector(CodominantAxillaryDirection);
        CodominantUpVecDirection = CodominantRotationMatrix.TransformVector(CodominantUpVecDirection);
        
        FVector BiasRotationAxis = FVector::CrossProduct(Sprout->NextApicalDirection,Bud.Direction.Axillary);
    	BiasRotationAxis.Normalize();
        FMatrix BiasRotationMatrix = FQuat(BiasRotationAxis, CodominantBiasDegreeBase).ToMatrix();
        FVector BiasAdjustedApicalDirection = BiasRotationMatrix.TransformVector(CodominantApicalDirection);
        CodominantAxillaryDirection = BiasRotationMatrix.TransformVector(CodominantAxillaryDirection);
        CodominantUpVecDirection = BiasRotationMatrix.TransformVector(CodominantUpVecDirection);
        
        FVector BaseApicalDirection = BiasAdjustedApicalDirection;
        FVector BaseAxillaryDirection = CodominantAxillaryDirection;
        FVector BaseUpVector = CodominantUpVecDirection;

    	check(Sprout->NextAxialElongations.IsValidIndex(i));
		
    	//Create New Sprouts
		FVector OldPosition = Sprout->Position;
		FVector NewPosition = OldPosition + (BaseApicalDirection * Sprout->NextAxialElongations[i]);
    	int32 BranchNumber = Sprout->Primitive->BranchNumber;
    	TArray<int32> Parents = Sprout->Primitive->Parents;

		float ParentDot = FVector::DotProduct(BaseApicalDirection, Bud.Direction.Apical);
		float AdjustedLateralMultiplier = Bud.LateralMeristem.Multiplier * (1/float(NumTriggered));
		float LateralMultiplier = FMath::Lerp(Bud.LateralMeristem.Multiplier,AdjustedLateralMultiplier, InGrowerParams.TrunkGrowth.WhorledRadiusImpact);

    	// ------------- Set New Attributes --------------//
		UPVGrowerPoint* NewPoint = NewObject<UPVGrowerPoint>(Skeleton, UPVGrowerPoint::StaticClass(), NAME_None, RF_Transactional);
		//int NewMeristem = addpoint(0,newPos);
		//int SourceInternodePrim = pointprims(0,@ptnum)[0];
		//addvertex(0,sourceInternodePrim,newMeristem);
		NewPoint->bNewBud = true;
		NewPoint->Position = NewPosition;
		// Set New Shoot Attributes
		NewPoint->Bud.Status = FPVBudStatus{1,0,0,0,1,0,0,0,0,0};
		NewPoint->Bud.HormoneLevels = FPVBudHormoneLevels{1,1,Bud.HormoneLevels.AxillaryInhibition,Bud.HormoneLevels.Radical / 2.0f,0,0};
		NewPoint->Bud.Development = FPVBudDevelopment{Sprout->Bud.Development.Generation,0,Bud.Development.BranchAge,0,0,0};
		NewPoint->Bud.Direction = FPVBudDirection{BaseApicalDirection, BaseAxillaryDirection, BaseApicalDirection, BaseApicalDirection, BaseApicalDirection, BaseUpVector};
		NewPoint->Bud.LateralMeristem = FPVBudLateralMeristem(0,LateralMultiplier,0,0,ParentDot,(Sprout->LengthFromRoot + Sprout->NextAxialElongations[i]),0);
    	NewPoint->LengthFromRoot = NewPoint->Bud.LateralMeristem.RootDistance;
    	NewPoint->Bud.LightDetected = Sprout->Bud.LightDetected;
    	
    	if(Sprout->Bud.Development.Generation > 1)
    	{
    		NewPoint->LengthFromTrunk = GetLengthFromTrunk(Sprout, Sprout->NextAxialElongations[i]);
    	}
    	
    	NewPoint->SeedInfo = Sprout->SeedInfo;
    	check(Sprout->Primitive);
    	NewPoint->Neighbors.Add(Sprout);
    	Sprout->Neighbors.Add(NewPoint);
    	UPVGrowerPrimitive* Primitive = Sprout->Primitive;
    	
    	if(i > 0)
    	{
    		Primitive = NewObject<UPVGrowerPrimitive>(Skeleton, UPVGrowerPrimitive::StaticClass(), NAME_None, RF_Transactional);
    		Primitive->BranchParentNumber = BranchNumber;
    		Primitive->BranchSourceBudNumber = Bud.BudNumber;
    		Primitive->PlantNumber = Sprout->Primitive->PlantNumber;
    		Primitive->Parents = Parents;
    		Primitive->Parents.Add(BranchNumber);
    		Primitive->bNewBranch = true;
    		NewPoint->Primitive = Primitive;

    		Skeleton->AddPrimitive(Primitive);
    	}

    	Skeleton->AddPoint(NewPoint, Primitive);

		Bud.Status.Dormant = 0;
    }
}

void FPVGrower::PostGrowth(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::PostGrowth);
	// Set New Bud Number
	TArray<UPVGrowerPoint*> NewBuds = Skeleton->Points.FilterByPredicate([](UPVGrowerPoint* p)
	{
		return p->bNewBud;
	});

	// Set New Branch Number
	TArray<UPVGrowerPrimitive*> NewPrimitives = Skeleton->Primitives.FilterByPredicate([](UPVGrowerPrimitive* prim)
	{
		return prim->bNewBranch;
	});

	//Update the Primitive children for the new Branch
	for (auto NewPrimitive : NewPrimitives)
	{
		for (auto Parent : NewPrimitive->Parents)
		{
			if (UPVGrowerPrimitive* ParentPrimitive = Skeleton->GetPrimitive(Parent))
			{
				ParentPrimitive->Children.Add(NewPrimitive->BranchNumber);
			}
		}
	}

	/*for (auto Bud : NewBuds)
	{
		UE_LOGF(LogProceduralVegetation, Log, "New Bud : %i", Bud->Bud.BudNumber);
	}*/

	/*for (auto Point : Skeleton->Points)
	{
		Point->Neighbors.Sort();
		for(auto N : Point->Neighbors)
		{
			UE_LOGF(LogProceduralVegetation, Log, "Bud : %i : AssignedNeighbor : %i", Point->Bud.BudNumber, N->Bud.BudNumber);
		}

		TArray<UPlantSkeletonPoint*> Negibhours = FindNegibhours(Point, Skeleton);
		Negibhours.Sort();
		for(auto N : Negibhours)
		{
			UE_LOGF(LogProceduralVegetation, Log, "Bud : %i : FoundNeighbor : %i", Point->Bud.BudNumber, N->Bud.BudNumber);
		}
	}*/
	//
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PV::Grower::FindNegibhoursForNewBuds);
		for (auto Bud : NewBuds)
		{
			auto CurrentBud = Bud;
			UPVGrowerPoint* PrevBud = nullptr;
			while (CurrentBud)
			{
				if (CurrentBud->Neighbors.Num() > 0)
				{
					if (PrevBud != CurrentBud->Neighbors[0])
					{
						PrevBud = CurrentBud;
						CurrentBud->Neighbors[0]->bLateralAppend = true;
						CurrentBud = CurrentBud->Neighbors[0];
					}
					else
					{
						//Break for cyclic neighbours
						break;
					}
				}

				if (CurrentBud->Bud.Status.Seed == 1 || CurrentBud->Neighbors.Num() == 0)
				{
					CurrentBud = nullptr;
				}
			}
		}
	}

	for (auto Point : Skeleton->Points)
	{
		if (Point->bLateralAppend)
		{
			FPVTrunkGrowthParams LateralElongationLocal = InGrowerParams.TrunkGrowth;
			FPVAuxinParams AuxinLocal = InGrowerParams.Auxin;

			if (Point->Bud.Development.Generation > 1)
			{
				//LateralElongationLocal = LateralElongationChild;
				AuxinLocal = InGrowerParams.Auxin;
			}

			float Elongation = LateralElongationLocal.IncrementalRadius;
			float SeedScaleAdjustedLateralElongation = Elongation * Point->SeedInfo.PScaleRatio;
			Elongation = FMath::Lerp(Elongation, SeedScaleAdjustedLateralElongation, LateralElongationLocal.SeedScaleEffect);

			Elongation *= Point->Bud.LateralMeristem.Multiplier;
			Point->Bud.LateralMeristem.LateralMeristem += Elongation;
			Point->Bud.Development.RelativeBudAge += 1;

			Point->Bud.HormoneLevels.Apical = FMath::Lerp(Point->Bud.HormoneLevels.Apical,0,AuxinLocal.AuxinFalloff);
			Point->Bud.HormoneLevels.Axillary = FMath::Lerp(Point->Bud.HormoneLevels.Axillary,0,0.1);
			Point->Bud.HormoneLevels.AxillaryInhibition = FMath::Lerp(Point->Bud.HormoneLevels.AxillaryInhibition,0,0.1);
			Point->Bud.HormoneLevels.Ethylene += InGrowerParams.Foliage.EthyleneBuildup;
		}
		else if (Point->bNewBud)
		{
			Point->Bud.Development.RelativeBudAge += 1;
		}
		else if (Point->Bud.Status.Inactive == 1)
		{
			float Ethylene = InGrowerParams.Foliage.EthyleneBuildup;
			Ethylene *= 0.5;
			Point->Bud.HormoneLevels.Ethylene += FMath::Clamp(Ethylene,0,1);
		}
		
		Point->Bud.HormoneLevels.Ethylene = FMath::Clamp(Point->Bud.HormoneLevels.Ethylene, 0,1);
	}

	for (UPVGrowerPrimitive* prim : NewPrimitives)
	{
		if (auto Point = Skeleton->GetPoint(prim->BranchSourceBudNumber))
		{
			prim->InsertBranchBud(Point, 0);
		}
	}
}

void FPVGrower::ApplyGravity(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton)
{
	ApplyDavinciWeight(Skeleton, InGrowerParams.GravityParams, InGrowerParams.TrunkGrowth);
	ApplyGravityWeight(Skeleton, InGrowerParams.GravityParams, InGrowerParams.Foliage, InGrowerParams.TrunkGrowth, InGrowerParams.AgeSenescence);
}

void FPVGrower::ApplyDavinciWeight(UPVGrowerData* Skeleton, const FPVGrowerGravityParams& InGravityParams, const FPVTrunkGrowthParams& InLateralParams)
{
	TArray<int> RootBudNumbers;
	TArray<float> RootBudDavinciMeristemArray;
	int BudNumber = 0;

	TArray<UPVGrowerPrimitive*> SortedPrimitives = Skeleton->Primitives;
	SortedPrimitives.Sort([](const UPVGrowerPrimitive& A, const UPVGrowerPrimitive& B)
	{
		return A.BranchNumber > B.BranchNumber;
	});

	for (auto Primitive : SortedPrimitives)
	{
		TArray<int32> Points = Primitive->BranchBuds;
		Algo::Reverse(Points);

		float DavinciMeristem = 0;
		
		for (auto BranchPoint : Points)
		{
			if (UPVGrowerPoint* Point = Skeleton->GetPoint(BranchPoint))
			{
				FPVBud& Bud = Point->Bud;

				float LateralElongation = InLateralParams.IncrementalRadius;
				float LateralMultiplier = Bud.LateralMeristem.Multiplier;
				float LateralElongationSeedScaleEffect = InLateralParams.SeedScaleEffect;
				float SeedScaleRatio = Point->SeedInfo.PScaleRatio;

				float SeedScaleAdjustedLateralElongation = LateralElongation * SeedScaleRatio;
				LateralElongation = FMath::Lerp(LateralElongation, SeedScaleAdjustedLateralElongation, LateralElongationSeedScaleEffect);
				float LateralMeristem = LateralElongation * LateralMultiplier;
				DavinciMeristem += LateralMeristem;

				// Get Root Meristem
				BudNumber = Bud.BudNumber;
				int BudNumberArrayPosition = RootBudNumbers.Find(BudNumber);
				if (BudNumberArrayPosition >= 0)
				{
					float RootBudDavinciMeristem = RootBudDavinciMeristemArray[BudNumberArrayPosition];
					DavinciMeristem += RootBudDavinciMeristem;
				}
        
				// Set Meristem
				Bud.LateralMeristem.Davinci = DavinciMeristem;

				//UE_LOGF(LogProceduralVegetation, Log, "Prim Number %i Bud Number %i LateralMeristem %f Davinci %f", Primitive->BranchNumber, BudNumber, Bud.LateralMeristem.LateralMeristem, DavinciMeristem);
			}
		}

		RootBudNumbers.Add(BudNumber);
		RootBudDavinciMeristemArray.Add(DavinciMeristem);
	}
}

void GetAdjustedMeristem(UPVGrowerPoint* Point, FPVPointGravityAttribute& GravityAttribute)
{
	FPVBud& Bud = Point->Bud;
	float Segment_length = 0;
	
	if (Point->Neighbors.Num() > 0)
	{
		FVector NeighbourPos = Point->Neighbors[0]->Position;
		Segment_length = FVector::Dist(NeighbourPos , Point->Position);
	}
	
	float AdjustedLateralMeristem = Bud.LateralMeristem.LateralMeristem * Segment_length;
	GravityAttribute.Weight = AdjustedLateralMeristem;
	GravityAttribute.Radius = Bud.LateralMeristem.LateralMeristem;
}

void GravityOverride(const FPVGrowerGravityParams& GravityParams, UPVGrowerPoint* Point, FPVPointGravityAttribute& GravityAttribute)
{
	float GravityOverride = 1;
	
	/*if (GuideSettings.bUse)
	{    
		float Found_guide = point(0,"foundGuide",PT);
		float Guide_gravity_override = GuideSettings.GuideGravityOverride;
		GravityOverride = 1- (Found_guide * Guide_gravity_override);
	}*/

	FPVBud& Bud = Point->Bud;
	int bInput = Point->bInput;//inpointgroup(0,"Input_Grp",PT);
	if (Bud.Development.Generation == 1 || bInput)
	{
		float CellDensityCore = GravityParams.TrunkReinforcement;
		float CellDensityInput = GravityParams.InputReinforcement;
		float CellDensityModifier = 0;
		if (Bud.Development.Generation == 1 && bInput)
		{
			CellDensityModifier = FMath::Max(CellDensityCore,CellDensityInput);
		}
		else if (Bud.Development.Generation == 1)
		{
			CellDensityModifier = CellDensityCore;
		}
		else if (bInput)
		{
			CellDensityModifier = CellDensityInput;
		}
		
		GravityOverride = GravityOverride * ( 1 - CellDensityModifier);
	}
    
	if (!Point->bLateralAppend && !Point->bNewBud)
	{
		GravityOverride = 0;
	}

	GravityAttribute.GravityOverride = GravityOverride;
}

void FPVGrower::ApplyGravityWeight(UPVGrowerData* Skeleton, const FPVGrowerGravityParams& InGravityParams, const FPVFoliageParams& InFoliage, const FPVTrunkGrowthParams& InAxialParams, const FPVAgeSenescenceParams& InAgeSenescence)
{
	TArray<FPVPointGravityAttribute> GravityAttributes;
	//int PointIndex = 0;
	for (auto Point : Skeleton->Points)
	{
		FPVPointGravityAttribute PointGravityAttribute;
		FPVBud& Bud = Point->Bud;
		GetAdjustedMeristem(Point, PointGravityAttribute);
		GravityOverride(InGravityParams, Point, PointGravityAttribute);
		
		PointGravityAttribute.Generation = Bud.Development.Generation;
		PointGravityAttribute.BudAge = Bud.Development.BudAge;
		PointGravityAttribute.LeafWeight = Bud.HormoneLevels.Ethylene < InFoliage.EthyleneThreshold ? (InGravityParams.FoliageWeight * 0.1) : 0;
		//UE_LOGF(LogProceduralVegetation, Log, "LeafWeight %f for BudNumber %i  Bud.HormoneLevels.Ethylene %f Leaf Growth Ethylene %f Param LeafWeight %f", PointGravityAttribute.LeafWeight,Bud.BudNumber, Bud.HormoneLevels.Ethylene, LeafGrowth.Ethylene, GravityParams.LeafWeight);
		PointGravityAttribute.Position = Point->Position;
		PointGravityAttribute.Direction = Bud.Direction.Apical;
		PointGravityAttribute.Up = Bud.Direction.UpVector;
		PointGravityAttribute.Length = 0.0;
		PointGravityAttribute.ChildPoints.Add(Bud.BudNumber);
		GravityAttributes.Add(PointGravityAttribute);
		//PointIndex++;
	}

	AttributeAccumulation(Skeleton, GravityAttributes, InGravityParams, InAxialParams);
	ComputeGravity(Skeleton, GravityAttributes, InGravityParams);
	UpdatePositions(Skeleton, GravityAttributes);
	RealignVectors(Skeleton);

	//Find the Primitives/Branches that are broken from root due to gravity
	//the branches that are broken from root can can be removed along with their children
	TArray<int32> BrokenPrimitives;
	GravityBreakRoot(Skeleton,BrokenPrimitives, InGravityParams);
	RemoveBrokenBranches(Skeleton,BrokenPrimitives);
	
	BranchBreak(Skeleton, InAgeSenescence, InGravityParams);
	
	TArray<UPVGrowerPrimitive*> Adopters;
	FindAdopters(Skeleton, Adopters);
	AdoptOrphans(Skeleton, Adopters);
	
	GroundHitBehaviour(Skeleton, InGravityParams);
	DeactivateBrokenBuds(Skeleton, InAgeSenescence);
}

void FPVGrower::ComputeGravity(UPVGrowerData* Skeleton, TArray<FPVPointGravityAttribute>& GravityAttributes, const FPVGrowerGravityParams& InGravityParams)
{
	auto Primitives = Skeleton->Primitives;
	//Algo::Reverse(Primitives);
	
	for (auto Primitive : Primitives)
	{
		TArray<int32> Points = Primitive->BranchBuds;
		check(Points.Num() > 0)

		for (int32 i = 0; i <  Points.Num(); i++)
		{
			int32 PointIndex = Skeleton->GetPointIndex(Points[i]);
			int32 NextPointIndex = INDEX_NONE;
			if (Points.IsValidIndex(i + 1))
			{
				NextPointIndex = Skeleton->GetPointIndex(Points[i + 1]);
			}
			int32 PrevPointIndex = INDEX_NONE;
			if (Points.IsValidIndex(i - 1))
			{
				PrevPointIndex = Skeleton->GetPointIndex(Points[i - 1]);
			}
			bool bValidNextIndex = NextPointIndex != INDEX_NONE && GravityAttributes.IsValidIndex(NextPointIndex);
			bool bValidPreviousIndex = PrevPointIndex != INDEX_NONE && GravityAttributes.IsValidIndex(PrevPointIndex);
			
			//UE_LOGF(LogProceduralVegetation, Log, "BudNumber : %i" , PointIndex + 1);
			if (!GravityAttributes.IsValidIndex(PointIndex))
			{
				continue;
			}

			FPVPointGravityAttribute& CurrentAttribute = GravityAttributes[PointIndex];
			FPVPointGravityAttribute AttributeToUse = CurrentAttribute;

			if (NextPointIndex != INDEX_NONE && GravityAttributes.IsValidIndex(NextPointIndex))
			{
				AttributeToUse.Direction = GravityAttributes[NextPointIndex].Position - AttributeToUse.Position;
			}
			else if (i == Points.Num() - 1 && bValidPreviousIndex)
			{
				AttributeToUse.Direction = AttributeToUse.Position - GravityAttributes[PrevPointIndex].Position;
			}
			AttributeToUse.Direction.Normalize();
			
			if (bValidPreviousIndex)
			{
				AttributeToUse.Up = (i != 1) ? GravityAttributes[PrevPointIndex].Up : AttributeToUse.Up;
				AttributeToUse.Direction = (i == Points.Num() -1) || !GravityAttributes.IsValidIndex(NextPointIndex) ? (CurrentAttribute.Position - GravityAttributes[PrevPointIndex].Position).GetSafeNormal() :
					(GravityAttributes[NextPointIndex].Position - CurrentAttribute.Position).GetSafeNormal();
			}
			
			if (i == 0 && bValidNextIndex)
			{
				FPVPointGravityAttribute& NextGravityAttribute = GravityAttributes[NextPointIndex];
				AttributeToUse.Up = NextGravityAttribute.Up;
				AttributeToUse.Weight = NextGravityAttribute.Weight;
				AttributeToUse.Length = NextGravityAttribute.Length;
				AttributeToUse.BudAge = NextGravityAttribute.BudAge;
				AttributeToUse.ChildPoints = NextGravityAttribute.ChildPoints;
			}

			FQuat Quat = CreateGravityMatrix(AttributeToUse, InGravityParams);
			RotateChildPoints(Skeleton, GravityAttributes, CurrentAttribute, Quat);
		}
	}
}

FQuat FPVGrower::CreateGravityMatrix(const FPVPointGravityAttribute& Attribute, const FPVGrowerGravityParams& InGravityParams)
{
	float GravityDot = FMath::GetMappedRangeValueClamped(FVector2D(0,1), FVector2D(1,0), FMath::Abs(FVector::DotProduct(Attribute.Direction, FVector(0, 0, -1))));
	GravityDot = FMath::Clamp(FMath::Acos(1 - GravityDot),0,1);
	const float ClampedLength = FMath::Clamp(Attribute.Length,0.0001,Attribute.Length);
	const float AdjustedLength = ClampedLength * GravityDot;
    
	const float RelativeAge = FMath::GetMappedRangeValueClamped(FVector2f(0,InGravityParams.CellDevelopmentTime), FVector2f(0,1) ,Attribute.BudAge);
	//UE_LOGF(LogProceduralVegetation, Log, "RelativeAge %f BudAge %i CellDevTime %f" , RelativeAge, Attribute.BudAge, GravityParams.CellDevelopmentTime);
	const float ModulusOfElasticity = FMath::GetMappedRangeValueClamped(FVector2f(0,1),FVector2f(InGravityParams.CellDensity.GetLowerBoundValue(),InGravityParams.CellDensity.GetUpperBoundValue()) , RelativeAge);
	const float PointLoad = ((Attribute.Weight * InGravityParams.CellWeight) + Attribute.LeafWeight) * (InGravityParams.GravitationalForce * Attribute.GravityOverride);
	//UE_LOGF(LogProceduralVegetation, Log, "PointLoad %f" , PointLoad);
	
	//UE_LOGF(LogProceduralVegetation, Log, "Weight %f Cell Weight %f LeafWeight %f GForce %f GOverride %f" , Attribute.Weight, GravityParams.CellWeight, Attribute.LeafWeight, GravityParams.GravitationalForce, Attribute.GravityOverride);
	const float MomentOfInertia = FMath::Pow(0.5 * PI * Attribute.Radius,4);
	//UE_LOGF(LogProceduralVegetation, Log, "Radius %f", Attribute.Radius);
    
	const float Force = FMath::Pow((PointLoad * AdjustedLength), 3);
	const float Resistance = 3 * (ModulusOfElasticity * MomentOfInertia);
	const float Displacement = Force / Resistance;

	//UE_LOGF(LogProceduralVegetation, Log, "Force %f Resistance %f ModulusOfElasticity %f MomentOfInertia %f PointLoad %f AdjustedLength %f", Force, Resistance,ModulusOfElasticity, MomentOfInertia, PointLoad, AdjustedLength);
	//UE_LOGF(LogProceduralVegetation, Log, "Displacement %f Segment Length : %f Displacement / Seg length : %f Asin %f", Displacement, Attribute.Length , Displacement / Attribute.Length , FMath::Asin(Displacement / Attribute.Length));
	
	FVector RotationAxis = FVector::CrossProduct(Attribute.Direction,FVector(0, 0,-1));
	RotationAxis.Normalize();
	
	float RotationAngle = FMath::Clamp(FMath::Asin(Displacement/ClampedLength),0,0.5);
	//UE_LOGF(LogProceduralVegetation, Log, "Gravity:: Dir %ls RotationAxis : %ls RotationAngle : %f", *Attribute.Direction.ToString(), *RotationAxis.ToString() , RotationAngle);
	return FQuat(RotationAxis, RotationAngle);
}

void FPVGrower::RotateChildPoints(UPVGrowerData* Skeleton, TArray<FPVPointGravityAttribute>& GravityAttributes, FPVPointGravityAttribute& Attribute,const FQuat& Quat)
{
	TArray<int> ChildPoints = Attribute.ChildPoints;
	//Algo::Reverse(ChildPoints);
	for (int i=ChildPoints.Num() -2 ; i >= 0; i--)
	{
		const int Point = Skeleton->GetPointIndex(ChildPoints[i]);
		//UE_LOGF(LogProceduralVegetation, Log, "Gravity::RotateChildPoints Child BudNumber %i Quat %ls", Point + 1 , *Quat.ToString());
		if (GravityAttributes.IsValidIndex(Point))
		{
			FPVPointGravityAttribute& ChildAttribute = GravityAttributes[Point];
			const FVector AdjustedPos = Quat.RotateVector(ChildAttribute.Position - Attribute.Position) + Attribute.Position;
			const FVector AdjustedDir = Quat.RotateVector(ChildAttribute.Direction);
			const FVector AdjustedUp = Quat.RotateVector(ChildAttribute.Up);
			const float InDot = FVector::DotProduct(ChildAttribute.Direction.GetSafeNormal(),FVector::UpVector);
			const float OutDot = FVector::DotProduct(AdjustedDir.GetSafeNormal(),FVector::UpVector) - 0.001;
            
			if (OutDot > InDot && InDot < 0)
			{
				ChildAttribute.Weight = 2;
			}
			
			ChildAttribute.Position = AdjustedPos;
			ChildAttribute.Direction = AdjustedDir;
			ChildAttribute.Up = AdjustedUp;
		}
	}
}

void FPVGrower::UpdatePositions(UPVGrowerData* Skeleton,const TArray<FPVPointGravityAttribute>& GravityAttributes)
{
	for (int32 i = 0; i <  Skeleton->Points.Num(); i++)
	{
		if (GravityAttributes.IsValidIndex(i))
		{
			Skeleton->Points[i]->Position = GravityAttributes[i].Position;
		}
	}
}

TArray<TObjectPtr<UPVGrowerPrimitive>> GetBranchPrimitives(UPVGrowerData* Skeleton)
{
	TMap<TObjectPtr<UPVGrowerPrimitive>, int32> Parents;
	Parents.Reserve(Skeleton->Primitives.Num());
	TArray<TObjectPtr<UPVGrowerPrimitive>> Primitives;
	for (int i=0; i<Skeleton->Primitives.Num(); i++)
	{
		TArray<int> BranchParents = Skeleton->Primitives[i]->Parents;
		Parents.FindOrAdd(Skeleton->Primitives[i], BranchParents.Num());        
	}

	Parents.ValueSort([](int32 a, int32 b)
	{
		return a < b;
	});
	
	Parents.GetKeys(Primitives);
	return Primitives;
}

void FPVGrower::AttributeAccumulation(UPVGrowerData* Skeleton, TArray<FPVPointGravityAttribute>& GravityAttributes, const FPVGrowerGravityParams& InGravityParams, const FPVTrunkGrowthParams& InAxialParams)
{
	auto Primitives = GetBranchPrimitives(Skeleton);
	for (auto Primitive : Primitives)
	{
		TArray<int32> Points = Primitive->BranchBuds;
		Algo::Reverse(Points);

		check(Points.Num() > 0)
		auto SourceIndex = Skeleton->GetPointIndex(Points[0]);
		check(GravityAttributes.IsValidIndex(SourceIndex));
		FVector PrevPos = GravityAttributes[SourceIndex].Position;
		float PrevWeight = 0.0f;
		float PrevLeafWeight = 0.0f;
		bool  bHitGround = false;
		TArray<int> PrevChildPTs;

		int J = 0;
		for (auto BranchPoint : Points)
		{
			int PointIndex = Skeleton->GetPointIndex(BranchPoint);
			int NextPointIndex = INDEX_NONE;
			if(Points.IsValidIndex(J + 1))
			{
				NextPointIndex = Skeleton->GetPointIndex(Points[J + 1]);
			}
			//UE_LOGF(LogProceduralVegetation, Log, "AttributeAccumulation Point %i", BranchPoint);
			if (Skeleton->Points.IsValidIndex(PointIndex) && GravityAttributes.IsValidIndex(PointIndex))
			{
				UPVGrowerPoint* Point = Skeleton->Points[PointIndex];
				FPVBud& Bud = Point->Bud;

				TArray<int> CurrChildPTs = GravityAttributes[PointIndex].ChildPoints;
				//PrevChildPTs = PointIndex == Points[0] ? CurrChildPTs : PrevChildPTs.Add(CurrChildPTs[0]);//concat(prev_child_PTs, " ", curr_child_PTs);;
				if (J == 0)
				{
					PrevChildPTs = CurrChildPTs;
				}
				else
				{
					for (auto Child : CurrChildPTs)
					{
						PrevChildPTs.AddUnique(Child);
					}
				}
				FVector CurrPos = GravityAttributes[PointIndex].Position;
				float CurrLength = (CurrPos - PrevPos).Length();
				int RootPT = GravityAttributes[PointIndex].Generation == 1 && PointIndex == Skeleton->GetPointIndex(Points[Points.Num() -1]) ? 1 : 0;
				//int hit_ground_behaviour = chi("../../hitGroundBehaviour");            
                        
				if (RootPT == 0 && CurrPos.Z < (InAxialParams.SegmentLength*0.5) && InGravityParams.HitGroundBehaviour == EPVHitGroundBehaviour::Deflect)
				{
					GravityAttributes[PointIndex].LeafWeight = 0;
					bHitGround = true;
					if (CurrPos.Z < 0 && GravityAttributes.IsValidIndex(NextPointIndex))
					{
						FVector NextPos = GravityAttributes[NextPointIndex].Position;
						FVector Dir = CurrPos - NextPos;
						FVector FlatDir = FVector(Dir.X,Dir.Y, 0) * Dir.Length();
						FlatDir.Normalize();
						GravityAttributes[PointIndex].Position = NextPos + FlatDir;
						// pos_arr[PRPT].y = 0;
					}
				}

				float AccumulatedWeight = 0.0f;
				float AccumulatedLeafWeight = 0.0f;
				if (!bHitGround)
				{
					AccumulatedWeight = GravityAttributes[PointIndex].Weight + PrevWeight;
					//UE_LOGF(LogProceduralVegetation, Log, "AccumulatedWeight %f Weight %f PrevWeight %f", AccumulatedWeight, GravityAttributes[PointIndex].Weight, PrevWeight);
					AccumulatedLeafWeight = GravityAttributes[PointIndex].LeafWeight + (PrevLeafWeight/ (J+1*2));
				}
            
				PrevPos = CurrPos;
				PrevWeight = AccumulatedWeight;
				PrevLeafWeight = AccumulatedLeafWeight;        
				GravityAttributes[PointIndex].Weight = AccumulatedWeight;
				GravityAttributes[PointIndex].Length = CurrLength;
				GravityAttributes[PointIndex].ChildPoints = PrevChildPTs;
				FString ChildStr = "";
				for (auto Child : PrevChildPTs)
				{
					ChildStr = FString::Format(TEXT("{0} {1}"), {ChildStr, Child});
				}
				//UE_LOGF(LogProceduralVegetation, Log , "ChildPoints[%i] %ls", PointIndex, *ChildStr)
				J++;
			}
		}

	}
}

void FPVGrower::RealignVectors(UPVGrowerData* Skeleton)
{
	auto Primitives = GetBranchPrimitives(Skeleton);
	for (auto Primitive : Primitives)
	{
		TArray<int32> Points = Primitive->BranchBuds;
		check(Points.Num() > 0)

		for (int32 i = 1; i <  Points.Num(); i++)
		{
			int Point = Skeleton->GetPointIndex(Points[i]);
			//int curr_PT = Skeleton->Points[PointIndex];
			int PrevPoint = Points.IsValidIndex(i-1) ? Skeleton->GetPointIndex(Points[i -1]) : Point;
			FVector CurrPos = Skeleton->Points[Point]->Position;
			FVector PrevPos = Skeleton->Points.IsValidIndex(PrevPoint) ? Skeleton->Points[PrevPoint]->Position : FVector::Zero();
			FVector NewApical = (CurrPos - PrevPos).GetSafeNormal();
			auto& Bud = Skeleton->Points[Point]->Bud;
			//FVector Direction_arr[] = point(0,"budDirection",curr_PT);
			//int status_arr[] = point(0,"budStatus",curr_PT);
			//int seed_PT = status_arr[3];
			if (Bud.Status.Seed == 1)
			{
				continue;
			}
			FVector& Apical = Bud.Direction.Apical;
			FQuat AlignDihedral = FQuat::FindBetweenVectors(Apical, NewApical);
			Apical = NewApical;
			Bud.Direction.Axillary = AlignDihedral.RotateVector(Bud.Direction.Axillary);
			Bud.Direction.UpVector =  AlignDihedral.RotateVector(Bud.Direction.UpVector);
		}
	}
}

//Outputs the primitives that are broken from the root
//the branch is considered broken if it has more pressure that the branch break threshold
//the pressure is calculated from the bend between te source and the first bud of the branch
void FPVGrower::GravityBreakRoot(UPVGrowerData* Skeleton, TArray<int32>& BrokenPrimitives, const FPVGrowerGravityParams& InGravityParams)
{
	for (int i=0; i<Skeleton->Primitives.Num(); i++)
	{
		auto Primitive = Skeleton->Primitives[i];
		UPVGrowerPoint* RootPoint = nullptr;
		UPVGrowerPoint* FirstPoint = nullptr;
		
		if (Primitive->BranchBuds.IsValidIndex(0) && Primitive->BranchBuds.IsValidIndex(1))
		{
			RootPoint = Skeleton->GetPoint(Primitive->BranchBuds[0]);
			FirstPoint = Skeleton->GetPoint(Primitive->BranchBuds[1]);
		}

		bool bIgnoreBreak = false;
		
		if (RootPoint)
		{
			bIgnoreBreak = RootPoint->IgnoreGravityBreak > 0.5;
		}

		if (!bIgnoreBreak && RootPoint && FirstPoint)
		{
			FPVBud& RootBud = RootPoint->Bud;
			FPVBud& FirstBud = FirstPoint->Bud;
			
			float GravityParentChildDot = FVector::DotProduct(FirstBud.Direction.Apical, RootBud.Direction.Apical);
			float GravitationalPressure = FirstBud.LateralMeristem.ParentDot - GravityParentChildDot;
			GravitationalPressure += (FirstBud.LateralMeristem.Degredation * 1);

			//UE_LOGF(LogProceduralVegetation, Log, "BudNumber %i GravitationalPressure %f , ParentDot %f GravityParentChildDot %f FirstBud.Direction.Apical %ls RootBud.Direction.Apical %ls (1 - GravityParams.BranchWeightBreakThreshold) %f",RootPoint->Bud.BudNumber, GravitationalPressure, FirstBud.LateralMeristem.ParentDot, GravityParentChildDot, *FirstBud.Direction.Apical.ToString(), *RootBud.Direction.Apical.ToString(), (1.0 - GravityParams.BranchWeightBreakThreshold));
			if (GravitationalPressure >= (1.0 - InGravityParams.BranchWeightBreakThreshold))
			{
				BrokenPrimitives.Add(Primitive->BranchNumber);
			}
		}
	}
}

//Remove the branches along with the children and branch points also clean up the children of parents
void FPVGrower::RemoveBrokenBranches(UPVGrowerData* Skeleton,const TArray<int32>& BrokenPrimitives)
{
	TArray<UPVGrowerPrimitive*> RemovedBranches;
	for (int i=0; i<BrokenPrimitives.Num(); i++)
	{
		auto BranchNumber = BrokenPrimitives[i];
		if (auto Primitive = Skeleton->GetPrimitive(BranchNumber))
		{
			RemovedBranches.Add(Primitive);
		}
	}

	for (auto RemovedBranch : RemovedBranches)
	{
		Skeleton->RemovePrimitive(RemovedBranch);
	}
}

void FPVGrower::BranchBreak(UPVGrowerData* Skeleton, const FPVAgeSenescenceParams& InAgeSenescence, const FPVGrowerGravityParams& InGravityParams)
{
	TArray<UPVGrowerPrimitive*> RemoveBranches;
	
	for (auto Prim : Skeleton->Primitives)
	{
		const auto& Points = Prim->BranchBuds;
		const TArray<int32> PrimUnbrokenPoints = FindNonBrokenBuds(Skeleton, Points, InAgeSenescence, InGravityParams);

		//if found some broken points
		//Set the last unbroken bud as broken tip and remove the broken buds from branch
		if (PrimUnbrokenPoints.Num() != Points.Num())
		{
			const TArray<int32> PrimBrokenPoints = ExtractBroken(Points, PrimUnbrokenPoints);

			SetBrokenTip(Skeleton, PrimUnbrokenPoints);
			TArray<UPVGrowerPrimitive*> PrimRemoveBranches;
			RemoveBuds(Skeleton, Prim, PrimBrokenPoints, PrimRemoveBranches);
			RemoveBranches.Append(PrimRemoveBranches);
		}
	}
	
	for (UPVGrowerPrimitive* RemoveBranch : RemoveBranches)
	{
		Skeleton->RemovePrimitive(RemoveBranch);
	}
}

void FPVGrower::FindParentsToClean(UPVGrowerPrimitive* Primitive, TArray<int32>& CleanParents)
{
	TArray<int32> Parents = Primitive->Parents;
	Parents.AddUnique(Primitive->BranchNumber);
	for (int32 Parent : Parents)
	{
		CleanParents.AddUnique(Parent);
	}
}

//Add all the branches that has this point as source to the remove branch list
void FPVGrower::GetBranchesToRemoveForBrokenBud(UPVGrowerData* Skeleton, UPVGrowerPoint* Point, TArray<UPVGrowerPrimitive*>& RemoveBranches)
{
	TArray<UPVGrowerPrimitive*> BudPrimitives = Skeleton->GetSourceBudPrimitives(Point->Bud.BudNumber);
	for (UPVGrowerPrimitive* Primitive : BudPrimitives)
	{
		RemoveBranches.AddUnique(Primitive);
		Primitive->BranchBuds.Empty();

		for (int32 Child : Primitive->Children)
		{
			UPVGrowerPrimitive* ChildPrimitive = Skeleton->GetPrimitive(Child);
			if (ChildPrimitive)
			{
				RemoveBranches.AddUnique(ChildPrimitive);
				ChildPrimitive->BranchBuds.Empty();
			}
		}
	}
}

void FPVGrower::CleanParentsForBrokenBuds(UPVGrowerData* Skeleton, TArray<UPVGrowerPrimitive*>& RemoveBranches, TArray<int32>& CleanParents)
{
	for (int Parent : CleanParents)
	{
		UPVGrowerPrimitive* ParentPrimitive = Skeleton->GetPrimitive(Parent);
		for (UPVGrowerPrimitive* RemovePrimitive : RemoveBranches)
		{
			if (RemovePrimitive && ParentPrimitive && ParentPrimitive->Children.Contains(RemovePrimitive->BranchNumber))
			{
				ParentPrimitive->Children.Remove(RemovePrimitive->BranchNumber);
			}
		}
	}
}

void FPVGrower::RemoveBuds(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Primitive, TArray<int32> BrokenBuds, TArray<UPVGrowerPrimitive*>& PrimitivesToRemove)
{
	for (int BrokenBudNumber : BrokenBuds)
	{
		Skeleton->RemovePoint(Primitive, BrokenBudNumber);
		
		PrimitivesToRemove.Append(Skeleton->Primitives.FilterByPredicate([BrokenBudNumber](UPVGrowerPrimitive* Prim)
		{
			return BrokenBudNumber == Prim->BranchSourceBudNumber;
		}));
	}
}

//This function sets the last point of the input as broken tip
//for all the other unbroken points it sets the Ethylene to 1 so it cannot grow further
//also apical is set to zero for no further apical growth.
void FPVGrower::SetBrokenTip(UPVGrowerData* Skeleton, TArray<int32> UnbrokenPoints)
{
	if (!UnbrokenPoints.IsEmpty())
	{
		if (UPVGrowerPoint* LastPoint = Skeleton->GetPoint(UnbrokenPoints.Last()))
		{
			LastPoint->Bud.Status.BrokenTip = 1;
		}
	}
}

//if a point is not in unbroken points list it is broken
TArray<int> FPVGrower::ExtractBroken(const TArray<int32>& Points, const TArray<int32>& UnBrokenPoints)
{
	TArray<int> BrokenPoints = Points;
	for (int32 UnBrokenPoint : UnBrokenPoints)
	{
		BrokenPoints.Remove(UnBrokenPoint);
	}
	return BrokenPoints;
}

//Checks each point in the branch points and return a list of unbroken points
//if the gravity pressure is < branch break threshold it is unbroken
TArray<int32> FPVGrower::FindNonBrokenBuds(UPVGrowerData* Skeleton, TArray<int32> PrimPoints, const FPVAgeSenescenceParams& InAgeSenescence, const FPVGrowerGravityParams& InGravityParams)
{
	TArray<int32> UnbrokenPTs;

	if (PrimPoints.Num() < 2)
	{
		return UnbrokenPTs;
	}
	
	UnbrokenPTs = { PrimPoints[0], PrimPoints[1]};

	for (int i=2; i<PrimPoints.Num(); i++)
	{
		//int PrevPTIndex = Skeleton->GetPointIndex(PrimPoints[i-1]);
		//check(Skeleton->Points.IsValidIndex(PrevPTIndex));
		UPVGrowerPoint* PrevPoint = Skeleton->GetPoint(PrimPoints[i-1]);
		check(PrevPoint);
		FPVBud& PrevBud = PrevPoint->Bud;

		int CurrentPTIndex = Skeleton->GetPointIndex(PrimPoints[i]);
		check(Skeleton->Points.IsValidIndex(CurrentPTIndex));
		UPVGrowerPoint* CurrentPoint = Skeleton->Points[CurrentPTIndex];
		check(CurrentPoint);
		FPVBud& CurrentBud = CurrentPoint->Bud;
		
		if ( CurrentBud.Status.Codominant == 1 || /*found_guide==1 || */ PrevPoint->IgnoreGravityBreak > 0.5)
		{
			UnbrokenPTs.AddUnique(PrimPoints[i]);
			continue;
		}
		       
		float GravityDot = FVector::DotProduct(CurrentBud.Direction.Apical,PrevBud.Direction.Apical);
		float Degradation = InAgeSenescence.Mode == EPVAbscissionMode::Degrade ? PrevBud.LateralMeristem.Degredation : 0;
		GravityDot -= Degradation;
        
		if (GravityDot >= (1.0f - InGravityParams.BranchWeightBreakThreshold) )
		{
			UnbrokenPTs.AddUnique(PrimPoints[i]);
		}        
		else
		{
			break;
		}
        
	}
	return UnbrokenPTs;
}

//Any branch that has the broken tip and is also the source of other branches can become adopters
void FPVGrower::FindAdopters(UPVGrowerData* Skeleton, TArray<UPVGrowerPrimitive*>& Adopters)
{
	for (UPVGrowerPrimitive* Primitive : Skeleton->Primitives)
	{
		if (!Primitive->BranchBuds.IsEmpty())
		{
			UPVGrowerPoint* LastPoint = Skeleton->GetPoint(Primitive->BranchBuds.Last());
			if (LastPoint && LastPoint->Bud.Status.BrokenTip != 0 && LastPoint->Neighbors.Num() >= 2)
			{
				Adopters.AddUnique(LastPoint->Primitive);
				LastPoint->Bud.Status.BrokenTip = 0;
			}
		}
	}
}

void FPVGrower::AdoptOrphans(UPVGrowerData* Skeleton, TArray<UPVGrowerPrimitive*>& Adopters)
{
	TArray<UPVGrowerPrimitive*> Adopted;
	
	for (UPVGrowerPrimitive* Adopter: Adopters)
	{
		bool bAdopted = Adopted.Contains(Adopter);
		if (!bAdopted && Adopter->BranchBuds.Num() > 0)
		{
			TArray<UPVGrowerPrimitive*> OrphanBranches;
			TArray<int32> OrphanPoints;
            ExtractOrphans(Skeleton, Adopter,Adopters,OrphanBranches,OrphanPoints);
            AdoptOrphanPoints(Skeleton, Adopter,OrphanPoints);
			CleanHierarchyForAdoption(Skeleton, Adopter, OrphanBranches);
			Adopted.Append(OrphanBranches);
		}
	}
}

void FPVGrower::CleanHierarchyForAdoption(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Adopter,const TArray<UPVGrowerPrimitive*>& OrphanBranches)
{
	TArray<UPVGrowerPrimitive*> BranchesToRemove;
	TArray<int32> ChildrenBranches;
	TArray<int32> ParentBranches; 
	
	for(UPVGrowerPrimitive* OrphanBranch : OrphanBranches)
	{
		//BranchesToRemove.AddUnique(OrphanBranch);
		for(int32 OrphanChild : OrphanBranch->Children)
		{
			UPVGrowerPrimitive* ChildBranch = Skeleton->GetPrimitive(OrphanChild);
			if (ChildBranch)
			{
				if ( ChildBranch->BranchParentNumber == OrphanBranch->BranchNumber)
				{
					ChildBranch->BranchParentNumber = Adopter->BranchNumber;
				}

				int32 Index = ChildBranch->Parents.Find(OrphanBranch->BranchNumber);
				if (Index != INDEX_NONE)
				{
					ChildBranch->Parents[Index] = Adopter->BranchNumber;
				}
			}
		}

		Skeleton->RemovePrimitive(OrphanBranch->BranchNumber, false);
	}
}

//The Adopter branch try to adopt its orphan points
//the points left over by the orphan children get merged into the adoptor branch points
void FPVGrower::AdoptOrphanPoints(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Adopter, TArray<int32>& OrphanPoints)
{
	if(Adopter->BranchBuds.IsEmpty())
	{
		return;
	}

	int32 LastBudNumber = Adopter->BranchBuds.Last();
	UPVGrowerPoint* LastPoint = Skeleton->GetPoint(LastBudNumber);
	if(!LastPoint)
	{
		//UE_LOGF(LogProceduralVegetation, Log, "Can not find Point for bud number %i in Grower Adopt Orphan Points.", LastBudNumber);
		return;
	}

	FPVBud& AdopterBud = LastPoint->Bud;
	FVector PrevApical = AdopterBud.Direction.Apical;
	FVector PrevUp = AdopterBud.Direction.UpVector;

	for(const int32 OrphanBudNumber : OrphanPoints)
	{
		UPVGrowerPoint* OrphanPoint = Skeleton->GetPoint(OrphanBudNumber);
		if (OrphanPoint)
		{
			FPVBud& OrphanBud = OrphanPoint->Bud;
			FQuat Dihedral = FQuat::FindBetweenVectors(PrevApical,OrphanBud.Direction.Apical);
			PrevUp = Dihedral.RotateVector( PrevUp);
			OrphanBud.Direction.UpVector = PrevUp;
			PrevApical = OrphanBud.Direction.Apical;
			Adopter->AddBranchBud(OrphanPoint);
			OrphanPoint->Primitive = Adopter;
		}
	}
}

//When a branch has broken tip and is also connected to other branch Neighbors
//the neighbour become orphan branch and its points become orphan points
void FPVGrower::ExtractOrphans(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Adopter,const TArray<UPVGrowerPrimitive*>& Adopters, TArray<UPVGrowerPrimitive*>& OrphanBranches, TArray<int32>& OrphanPoints)
{
	UPVGrowerPrimitive* LoopAdopter = Adopter;    

	while (LoopAdopter && LoopAdopter->BranchBuds.Num() > 0)
	{
		TArray<int32> BranchBuds = LoopAdopter->BranchBuds;
		UPVGrowerPoint* LastBranchBud = Skeleton->GetPoint(BranchBuds.Last());
		if (!LastBranchBud)
		{
			break;
		}
		
		TArray<UPVGrowerPoint*> Neighbors = LastBranchBud->Neighbors;
		if (Neighbors.Num() < 2)
		{
			LastBranchBud->Bud.Status.BrokenTip = 1;
			break;
		}
		UPVGrowerPrimitive* OrphanBranch = Neighbors.Last()->Primitive;

		if (OrphanBranch == LoopAdopter)
		{
			LastBranchBud->Bud.Status.BrokenTip = 1;
			break;
		}
		
		TArray<int32> OrphanBranchPoints = OrphanBranch->BranchBuds;

		if (!OrphanBranchPoints.IsEmpty())
		{
			OrphanBranchPoints.RemoveAt(0);
			OrphanPoints.Append(OrphanBranchPoints);
		}
		
		OrphanBranches.AddUnique(OrphanBranch);
		//Skeleton->RemovePrimitive(OrphanBranch);
		
		if (!Adopters.Contains(OrphanBranch))
		{
			break;
		}
		
		LoopAdopter = OrphanBranch;
	}
}

void FPVGrower::GroundHitBehaviour(UPVGrowerData* Skeleton, const FPVGrowerGravityParams& InGravityParams)
{
	if (InGravityParams.HitGroundBehaviour != EPVHitGroundBehaviour::Kill)
	{
		return;
	}

	TArray<UPVGrowerPrimitive*> BranchesBelowGround;
	for (UPVGrowerPoint* Point : Skeleton->Points)
	{
		if (Point->Position.Z < 0 && Point->Bud.Development.Generation >= 2)
		{
			TObjectPtr<UPVGrowerPrimitive> PointPrimitive = Point->Primitive;
			if (!PointPrimitive)
			{
				continue;
			}

			BranchesBelowGround.AddUnique(PointPrimitive);
		}
	}

	for (TObjectPtr<UPVGrowerPrimitive> Branch : BranchesBelowGround)
	{
		if (Branch)
		{
			Skeleton->RemovePrimitive(Branch);
		}
	}
}

void FPVGrower::RemovePrimitiveFromParent(UPVGrowerData* Skeleton, TObjectPtr<UPVGrowerPrimitive> Primitive)
{
	for (int32 Parent : Primitive->Parents)
	{
		TObjectPtr<UPVGrowerPrimitive> ParentPrimitive = Skeleton->GetPrimitive(Parent);
		if (ParentPrimitive)
		{
			ParentPrimitive->Children.Remove(Primitive->BranchNumber);
		}
	}
}

//The broken buds are deactivated so they do not become sprout candidates and to stop their growth.
void FPVGrower::DeactivateBrokenBuds(UPVGrowerData* Skeleton, const FPVAgeSenescenceParams& InAgeSenescence)
{
	const TArray<UPVGrowerPrimitive*> GenerationalSortedPrimitives = GetGenerationalSortedPrimitives(Skeleton);

	//For every Primitive/Branch if it has a broken tip
	//Loop through all the buds to find active interactions, means if a point is connected to another branch
	//if connected break through otherwise set the bud as broken
	for (UPVGrowerPrimitive* Primitive :  GenerationalSortedPrimitives)
	{
		check(Primitive);

		UPVGrowerPoint* LastPoint = Skeleton->GetPoint(Primitive->BranchBuds.Last());
		if (!LastPoint || !LastPoint->Bud.Status.BrokenTip)
		{
			continue;
		}
		
		LastPoint->Bud.Status.Broken = true;

		for (int32 i = Primitive->BranchBuds.Num() - 2; i > 0; --i)
		{
			if (UPVGrowerPoint* Point = Skeleton->GetPoint(Primitive->BranchBuds[i]))
			{
				if (GetActiveInteractions(Skeleton, Point))
				{
					break;
				}

				Point->Bud.Status.Broken = true;
			}
		}
	}

	//The broken buds become inactive and their lateral growth also stops.
	for (UPVGrowerPoint* Point : Skeleton->Points)
	{
		if (Point && Point->Bud.Status.Broken)
		{
			Point->Bud.Status.Inactive = 1;
			Point->Bud.Status.Dormant = 0;
			Point->Bud.LateralMeristem.Inactive = 1.0;

			if (InAgeSenescence.Mode == EPVAbscissionMode::Degrade)
			{
				Point->Bud.LateralMeristem.Degredation = 0;
				Point->Bud.HormoneLevels.Apical = 0;
			}
		}
	}
}

//Sorts the Primitives in generational descending order like {g3, g2, g1}
TArray<UPVGrowerPrimitive*> FPVGrower::GetGenerationalSortedPrimitives(UPVGrowerData* Skeleton)
{
	TMap<TObjectPtr<UPVGrowerPrimitive>, int32> GenerationalPrimitives;
	GenerationalPrimitives.Reserve(Skeleton->Primitives.Num());
	TArray<TObjectPtr<UPVGrowerPrimitive>> Primitives;
	for (UPVGrowerPrimitive* Primitive : Skeleton->Primitives)
	{
		if (Primitive->BranchBuds.IsValidIndex(1))
		{
			UPVGrowerPoint* Point = Skeleton->GetPoint(Primitive->BranchBuds[1]);
			check(Point);
			FPVBud& Bud = Point->Bud;
			GenerationalPrimitives.FindOrAdd(Primitive, Bud.Development.Generation);
		}
	}

	GenerationalPrimitives.ValueSort([](int32 a, int32 b)
	{
		return a > b;
	});
	
	GenerationalPrimitives.GetKeys(Primitives);

	//UE_LOGF(LogProceduralVegetation, Log, " Generational Sorting Started");
	for (auto Primitive : Primitives)
	{
		UPVGrowerPoint* Point = Skeleton->GetPoint(Primitive->BranchBuds[1]);
		//UE_LOGF(LogProceduralVegetation, Log, " Generational Sorting: Branch Number %i, Generation Number %i",Primitive->BranchNumber, Point->Bud.Development.Generation );
	}
	//UE_LOGF(LogProceduralVegetation, Log, " Generational Sorting Ended");
	return Primitives;
}

//A point has active interactions if it is shared with other primitives/branches that is not broken
bool FPVGrower::GetActiveInteractions(UPVGrowerData* Skeleton, UPVGrowerPoint* Point)
{
	bool bActiveInteractions = false;
	TArray<UPVGrowerPrimitive*> Primitives = Skeleton->GetPointPrimitives(Point);
	for (UPVGrowerPrimitive* Primitive : Primitives)
	{
		if (Primitive && Primitive != Point->Primitive && Primitive->BranchBuds.Num() > 1)
		{
			UPVGrowerPoint* PrimitivePoint = Skeleton->GetPoint(Primitive->BranchBuds[1]);
			if (PrimitivePoint)
			{
				FPVBud& Bud = PrimitivePoint->Bud;
				if (Bud.Status.Broken == 0)
				{
					bActiveInteractions = true;
					break;
				}
			}
		}
	}

	return bActiveInteractions;
}

void FPVGrower::OnGrowthCycleEnd(UPVGrowerData* Skeleton)
{
	for (auto Point : Skeleton->Points)
	{
		Point->bNewBud = false;
		Point->bLateralAppend = false;
	}
	
	for(auto Primitive : Skeleton->Primitives)
	{
		Primitive->bNewBranch = false;
	}
}

float FPVGrower::GetLengthFromTrunk(const UPVGrowerPoint* InPoint, float NextElongation)
{
	check(InPoint);
	return InPoint->LengthFromTrunk + NextElongation;
}

void FPVGrower::AbscissionSenescence(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, int32 Cycle)
{
	TArray<int32> AbscissionPrimitives;
	for (UPVGrowerPrimitive* Primitive : Skeleton->Primitives)
	{
		if(Primitive && Primitive->BranchBuds.Num() >= 2)
		{
			UPVGrowerPoint* Point = Skeleton->GetPoint(Primitive->BranchBuds[1]);
			check(Point);
			FPVBud& Bud = Point->Bud;

			if (Bud.Development.Generation == 1 || Bud.Development.BudAge == 1 || Bud.Status.Codominant == 1)
			{
				continue;
			}

			bool bDeactivate = false;
			if (Bud.Status.Broken == 0)
			{
				//Add Light Senescence here
				bDeactivate = LightSenescence(InGrowerParams, Skeleton, Bud, Primitive);
			}

			bDeactivate |= ApplyAgeSenescence(InGrowerParams, Point);
			SetSenescence(Skeleton, Point, bDeactivate);

			if (Primitive->Senescence.bAbscission)
			{
				AbscissionPrimitives.Add(Primitive->BranchNumber);
			}
		}

		if (Primitive && Primitive->Senescence.bDegradation)
		{
			SetDegradationAmount(InGrowerParams, Skeleton, Primitive, Cycle);
		}

		if (Primitive && Primitive->Senescence.bInactive)
		{
			DeActivateSenescenceChildren(Skeleton, Primitive);
		}
	}
	
	RemoveBrokenBranches(Skeleton, AbscissionPrimitives);
}

bool FPVGrower::LightSenescence(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, FPVBud& Bud, UPVGrowerPrimitive* Primitive)
{
	bool Deactivate = false;

	FRandomStream RandomStream(Primitive->BranchNumber + InGrowerParams.RandomSeed);
	const float RandomThreshold = RandomStream.FRand();
	
	float LateralMaristem = Bud.LateralMeristem.LateralMeristem;
	int32 AbscissionThreshold = int32(rint(FMath::GetMappedRangeValueClamped(FVector2f(0,1),
		FVector2f(InGrowerParams.LightSenescence.SenescenceMin,InGrowerParams.LightSenescence.SenescenceMax), RandomThreshold)));
	float RetentionThreshold = (InGrowerParams.TrunkGrowth.IncrementalRadius * 100) * (1- InGrowerParams.LightSenescence.RetentionRadius);
	int LightRetention = LateralMaristem > RetentionThreshold ? 1 :0;
    
	if (Bud.LightDetected.Branch < InGrowerParams.LightSenescence.SenescenceThreshold)
	{
		Bud.Development.LightSenescense +=1;
		Deactivate = true;
	}    
	if (Bud.Development.LightSenescense > 0 && Bud.Development.LightSenescense > AbscissionThreshold && LightRetention == 0)
	{
		Primitive->Senescence.bAbscission = true;
		Primitive->Senescence.AbscissionType = EPVAbscissionType::Light;
	}

	return Deactivate;
}

void FPVGrower::DeActivateSenescenceChildren(UPVGrowerData* Skeleton, UPVGrowerPrimitive* Primitive)
{
	for (int32 Child : Primitive->Children)
	{
		UPVGrowerPrimitive* ChildPrimitive = Skeleton->GetPrimitive(Child);

		if (ChildPrimitive)
		{
			for (int32 BudNumber : ChildPrimitive->BranchBuds)
			{
				UPVGrowerPoint* Point = Skeleton->GetPoint(BudNumber);
				if (Point)
				{
					FPVBud& Bud = Point->Bud;
					Bud.Status.Inactive = 1;
					Bud.LateralMeristem.Multiplier = 1.0;
				}
			}
		}
	}
}

void FPVGrower::SetDegradationAmount(const FPVGrowerParams& InGrowerParams, UPVGrowerData* Skeleton, UPVGrowerPrimitive* Primitive, int32 Cycle)
{
	for (int32 BudNumber : Primitive->BranchBuds)
	{
		if (BudNumber != Primitive->BranchSourceBudNumber)
		{
			UPVGrowerPoint* Point = Skeleton->GetPoint(BudNumber);
			check(Point);
			FPVBud& Bud = Point->Bud;

			float Denominator = (InGrowerParams.TrunkGrowth.IncrementalRadius * Cycle);
			Denominator = Denominator < KINDA_SMALL_NUMBER ? KINDA_SMALL_NUMBER : Denominator;
			float Retention = Bud.LateralMeristem.LateralMeristem / Denominator;
			float AdditionalDegradation = RandomValue(BudNumber + Cycle + InGrowerParams.RandomSeed) - Retention;
			AdditionalDegradation = FMath::Clamp(AdditionalDegradation,0,1);
			//TODO: Investigate degradation Should be just an Add.
			AdditionalDegradation *= Primitive->Senescence.DegradationAmount;
			Bud.LateralMeristem.Inactive = 1.0;
			Bud.LateralMeristem.Degredation += AdditionalDegradation;
			Bud.Status.Inactive = 1;
		}
	}
}

void FPVGrower::SetSenescence(UPVGrowerData* Skeleton, UPVGrowerPoint* Point, const bool bDeactivate)
{
	check(Point);
	if (!bDeactivate)
	{
		return;
	}

	if (!Point->Primitive)
	{
		return;
	}

	UPVGrowerPrimitive* Primitive = Point->Primitive;
	Primitive->Senescence.bInactive = bDeactivate;
	
	for (int32 BudNumber : Primitive->BranchBuds)
	{
		UPVGrowerPoint* PrimPoint = Skeleton->GetPoint(BudNumber);
		if (PrimPoint && BudNumber != Primitive->BranchSourceBudNumber)
		{
			 FPVBud& Bud = PrimPoint->Bud;
			Bud.Development.AgeSenescense = Point->Bud.Development.AgeSenescense;
			Bud.Development.LightSenescense = Point->Bud.Development.LightSenescense;
			Bud.Status.Inactive = 1;
			Bud.LateralMeristem.Inactive = 1.0; 
		}
	}
}

bool FPVGrower::ApplyAgeSenescence(const FPVGrowerParams& InGrowerParams, UPVGrowerPoint* Point)
{
	FPVBud& Bud = Point->Bud;
	bool bDeactivate = false;

	int AbscissionThreshold = int(rint(FMath::GetMappedRangeValueClamped(FVector2f(0,1),
		FVector2f(InGrowerParams.AgeSenescence.SenescenceMin,InGrowerParams.AgeSenescence.SenescenceMax), RandomValue(Bud.BudNumber + InGrowerParams.RandomSeed))));
	float RetentionThreshold = (InGrowerParams.TrunkGrowth.IncrementalRadius * 100) * (1- InGrowerParams.AgeSenescence.RetentionRadius);
	int AgeRetention = Bud.LateralMeristem.LateralMeristem > RetentionThreshold ? 1 : 0;
    
	if (Bud.Development.BranchAge < InGrowerParams.AgeSenescence.SenescenceAge)
	{
		return bDeactivate;
	}

	Bud.Development.AgeSenescense += 1;
	
	bDeactivate = true;
	
	if (AgeRetention==1)
	{
		return bDeactivate;
	}
	
	if (InGrowerParams.AgeSenescence.Mode == EPVAbscissionMode::Degrade)
	{
		Point->Primitive->Senescence.bDegradation = true;
		Point->Primitive->Senescence.DegradationAmount = InGrowerParams.AgeSenescence.DegradationAmount;
	}
	else if (InGrowerParams.AgeSenescence.Mode == EPVAbscissionMode::Kill && Bud.Development.AgeSenescense > AbscissionThreshold)
	{
		Point->Primitive->Senescence.AbscissionType = EPVAbscissionType::Age;
		Point->Primitive->Senescence.bAbscission = true;
	}

	return bDeactivate;
}

#undef LOCTEXT_NAMESPACE
