// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteGroomBuilder.h"

#if WITH_EDITOR

#include "HairDescription.h"
#include "NaniteBuilder.h"
#include "GroomBuilder.h"

static int32 GHairStrandsNaniteProceduralShapes = 0;
static FAutoConsoleVariableRef CVarHairStrandsNaniteProceduralShapes(TEXT("r.HairStrands.Nanite.ProceduralShape"), GHairStrandsNaniteProceduralShapes, TEXT("Import a procedural shape in place of the desired asset. For debugging purpose"));

static int32 GHairStrandsNaniteMaxPointPerCurve = 32;
static FAutoConsoleVariableRef CVarHairStrandsNaniteMaxPointPerCurve(TEXT("r.HairStrands.Nanite.MaxPointPerCurve"), GHairStrandsNaniteMaxPointPerCurve, TEXT("Max number of point per curve that will be used for clamping imported groom's curves"), ECVF_ReadOnly);

static int32 GetNaniteGroomProceduralShape()
{
	return FMath::Max(0, GHairStrandsNaniteProceduralShapes);
}

static uint32 GetNaniteGroomMaxNumPointPerCurve()
{
	return FMath::Clamp(uint32(FMath::RoundDownToPowerOfTwo64(GHairStrandsNaniteMaxPointPerCurve)), 2u, 256u);
}

static void GetTangentBasis(const FVector3f& InTangentZ, FVector3f& OutTangentX, FVector3f& OutTangentY, FVector3f& OutTangentZ)
{
	const float Sign = InTangentZ.Z >= 0 ? 1 : -1;
	const float a = -1.f/( Sign + InTangentZ.Z );
	const float b = InTangentZ.X * InTangentZ.Y * a;

	OutTangentX = FVector3f( 1 + Sign * a * FMath::Square( InTangentZ.X ), Sign * b, -Sign * InTangentZ.X );
	OutTangentY = FVector3f( b,  Sign + a * FMath::Square( InTangentZ.Y ), -InTangentZ.Y );
	OutTangentZ = InTangentZ;
}

static FVector3f InterpolateCubicBezier(const FVector3f& P0, const FVector3f& P1, const FVector3f& P2, const FVector3f& P3, float Alpha)
{
	const float a  = Alpha;
	const float a2 = a * a;
	const float a3 = a * a2;

	const float b  = (1-Alpha);
	const float b2 = (b * b);
	const float b3 = (b * b2);
	return b3*P0 + (3*b2*a)*P1 + (3*b*a2)*P2 + a3*P3;
}

static FVector3f InterpolateCubicBezierDerivative(const FVector3f& P0, const FVector3f& P1, const FVector3f& P2, const FVector3f& P3, float Alpha)
{
	const float a  = Alpha;
	const float a2 = a * a;

	const float b  = (1-Alpha);
	const float b2 = (b * b);
	return 3*b2*(P1-P0) + 6*b*a*(P2-P1) + 3*a2*(P3-P2);
}

struct FCurvePoint
{
	FVector3f Position = FVector3f::ZeroVector;
	FVector3f Tangent = FVector3f::ZeroVector;
	float Radius = 0;
	float UCoord = 0;
	FVector2f RootUV = FVector2f::ZeroVector;
};

struct FCurve
{
	uint32 GetNumPoints() const 
	{ 
		return Positions.Num(); 
	}

	void SetNumPoints(uint32 In)
	{ 
		Positions.SetNum(In);
		Tangents.SetNum(In);
		Radius.SetNum(In);
		UCoords.SetNum(In);
		RootUVs.SetNum(In);
	}

	uint32 GetLowerIndex(float u, uint32 StartIt=0) const
	{
		const uint32 Count = GetNumPoints();
		check(StartIt < Count);
		for (uint32 It = StartIt; It < Count; ++It)
		{
			if (It + 1 == Count)
			{
				return Count-1;
			}
			else if (UCoords[It] <= u && u <= UCoords[It + 1])
			{
				return It;
			}
		}
		check(false);
		return Count-1;
	}

	FCurvePoint GetPoint(uint32 Index) const
	{
		check(Positions.IsValidIndex(Index));
		FCurvePoint Out;		
		Out.Position = Positions[Index];
		Out.Tangent = Tangents[Index];
		Out.Radius =  Radius[Index];
		Out.UCoord =  UCoords[Index];
		Out.RootUV =  RootUVs[Index];
		return Out;
	}

	void SetPoint(const FCurvePoint& In, uint32 Index)
	{
		check(Positions.IsValidIndex(Index));
		Positions[Index] = In.Position;
		Tangents[Index]  = In.Tangent;
		Radius[Index]    = In.Radius;
		UCoords[Index]   = In.UCoord;
		RootUVs[Index]   = In.RootUV;
	}

	TArray<FVector3f> Positions;
	TArray<FVector3f> Tangents;
	TArray<float> Radius;
	TArray<float> UCoords;
	TArray<FVector2f> RootUVs;
};

struct FCurveGroup
{
	uint32 GetNumCurves() const 
	{
		return Curves.Num(); 
	}
	uint32 GetNumPoints() const 
	{
		uint32 Out = 0;
		for (const FCurve& C : Curves)
		{
			Out += C.GetNumPoints();
		}
		return Out; 
	}

	TArray<FCurve> Curves;
};

// Interpolate two Curve points. S is the parametric distance between In0 (S=0), and In1 (S=1)
static FCurvePoint Interpolate(const FCurvePoint& In0, const FCurvePoint& In1, float S, bool bSmooth)
{
	FCurvePoint Out;

	// Linear or Cubic-Bezier
	if (!bSmooth)
	{
		Out.Position = FMath::Lerp(In0.Position, In1.Position, S);
		Out.Tangent = FMath::Lerp(In0.Tangent, In1.Tangent, S);
		Out.Tangent.Normalize();
	}
	else
	{
		const FVector3f P0 = In0.Position;
		const FVector3f P3 = In1.Position;
		const FVector3f T0 = In0.Tangent;
		const FVector3f T3 = In1.Tangent;

		// Use the distance between the two control points to resize the tangents. 
		// Each tangents is scaled by 1/4 of the distance between the control points
		const float D = (P3 - P0).Length();
		const FVector3f P1 = P0 + T0 * D * 0.25f;
		const FVector3f P2 = P3 - T3 * D * 0.25f;
		Out.Position = InterpolateCubicBezier(P0, P1, P2, P3, S);
		Out.Tangent = InterpolateCubicBezierDerivative(P0, P1, P2, P3, S);
		Out.Tangent.Normalize();
		// S is not correct, but good enough if the curvature is not too strong and point are equally spaced
	}

	Out.Radius   = FMath::Lerp(In0.Radius, In1.Radius, S);
	Out.UCoord   = FMath::Lerp(In0.UCoord, In1.UCoord, S);
	Out.RootUV   = In0.RootUV;

	return Out;
}

// Evaluate the curve at the parametric distance T [0..1]
static FCurvePoint Evaluate(const FCurve& In, float T, bool bSmooth, uint32& LowerIndex)
{
	LowerIndex = In.GetLowerIndex(T, LowerIndex);
	const FCurvePoint P0 = In.GetPoint(LowerIndex);
	const FCurvePoint P1 = In.GetPoint(FMath::Min(LowerIndex+1, In.GetNumPoints()-1));
	const float S = (T - P0.UCoord) / FMath::Max(P1.UCoord - P0.UCoord, KINDA_SMALL_NUMBER);
	check(S >= 0 && S <= 1);
	return Interpolate(P0, P1, S, bSmooth);
}

static void Resample(const FCurve& In, FCurve& Out, uint32 NumPoints, bool bSmooth)
{
	Out.SetNumPoints(NumPoints);
	const uint32 InPointCount = In.GetNumPoints();
	const uint32 OutPointCount = Out.GetNumPoints();

	uint32 InPointIndex = 0;
	for (uint32 OutPointIndex = 0; OutPointIndex < OutPointCount; ++OutPointIndex)
	{
		const float T = OutPointIndex / float(OutPointCount - 1);
		const FCurvePoint OutPoint = Evaluate(In, T, bSmooth, InPointIndex);
		Out.SetPoint(OutPoint, OutPointIndex);
	}
}

static float DistanceError(const FCurve& In0, const FCurve& In1)
{
	float ErrorSum2 = 0;
	uint32 LowerIndex1 = 0;
	const uint32 PointCount0 = In0.GetNumPoints();
	const uint32 PointCount1 = In1.GetNumPoints();
	check(PointCount0 >= PointCount1);
	for (uint32 PointIt = 0; PointIt < PointCount0; ++PointIt)
	{
		const float T = PointIt / float(PointCount0 - 1);
		const FVector3f Position0 = In0.Positions[PointIt];
		const FVector3f Position1 = Evaluate(In1, T, true/*bSmooth*/, LowerIndex1).Position;
		const float Distance2 = FVector3f::DistSquared(Position0, Position1);
		ErrorSum2 += Distance2;
	}
	return ErrorSum2 / PointCount0;
}

static bool FitCurve(const FCurve& In, FCurve& Out)
{
	// 1. Resample input curve (polyline) using cubic spline interpolation to get a smooth curve, and equally spaced points
	FCurve Reference;
	Resample(In, Reference, FMath::RoundUpToPowerOfTwo(In.GetNumPoints() * 4u), true/*bSmooth*/);

	// 2. Iteratively fit curve
	struct FResult
	{
		TArray<FVector3f> ControlPoints;
		FCurve Curve;
		float RMSE = 0;
		float MaxError = 0;
	};
	TArray<FResult> Results;
	Results.Reserve(uint32(FMath::Log2(float(GetNaniteGroomMaxNumPointPerCurve()))));
	for (uint32 NumControlPoints = 2; NumControlPoints <= In.GetNumPoints(); NumControlPoints *=2)
	{
		FResult& Result = Results.AddDefaulted_GetRef();

		// a. Fit
		FCurve Fitted;
		Resample(Reference, Fitted, NumControlPoints, true/*bSmooth*/);

		// b. Compute error
		{
			FCurve FittedResampled;
			Resample(Fitted, FittedResampled, Reference.GetNumPoints(), true/*bSmooth*/);

			float SumError2 = 0;
			float MaxError2 = 0;
			const uint32 ResampledPointCount = Reference.GetNumPoints();
			for (uint32 PointIt = 0; PointIt < ResampledPointCount; ++PointIt)
			{
				const FVector3f PRef = Reference.Positions[PointIt];
				const FVector3f PFit = FittedResampled.Positions[PointIt];

				const float Distance2 = FVector3f::DistSquared(PRef, PFit);
				SumError2 += Distance2;
				MaxError2 = FMath::Max(MaxError2, Distance2);
			}
			Result.Curve = Fitted;
			Result.RMSE = FMath::Sqrt(SumError2 / ResampledPointCount);
			Result.MaxError = FMath::Sqrt(MaxError2);
		}
	}

	// 3. Pick the fit with the minimal error. 
	// Results are ordered from coarsest to finest (2,4,8,...,N control points).
	// We pick the first (simplest) result whose MaxError is within the threshold.
	// Threshold is fixed at 1.5mm — consider making it proportional to curve length or AABB.
	const float ErrorThreshold = 0.15f;
	FResult OutResult;
	OutResult.MaxError = FLT_MAX;
	for (const FResult& R : Results)
	{
		if (R.MaxError < OutResult.MaxError)
		{
			OutResult = R;
		}

		if (OutResult.MaxError <= ErrorThreshold)
		{
			break;
		}
	}
	Out = OutResult.Curve;

	return true;
}

static void ConvertHairData(const FHairStrandsDatas& HairData, FCurveGroup& Out, TArray<FCurve*>& OutAllCurves)
{
	const uint32 CurveCount = HairData.GetNumCurves();
	Out.Curves.SetNum(CurveCount);
	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const uint32 SrcCurvePointCount = HairData.StrandsCurves.CurvesCount[CurveIndex];
		const uint32 CurvePointOffset = HairData.StrandsCurves.CurvesOffset[CurveIndex];
		const bool bHasRootUV = HairData.StrandsCurves.HasAttribute(EHairAttribute::RootUV);
		check(SrcCurvePointCount >= 2);

		// Check if there are any identical consecutive points
		uint32 DstCurvePointCount = 1;
		for (uint32 PointIndex = 1; PointIndex < SrcCurvePointCount; ++PointIndex)
		{
			const FVector3f P0 = HairData.StrandsPoints.PointsPosition[CurvePointOffset + PointIndex-1];
			const FVector3f P1 = HairData.StrandsPoints.PointsPosition[CurvePointOffset + PointIndex];
			if (P0 != P1)
			{
				DstCurvePointCount++;
			}
		}
		check(DstCurvePointCount >= 2);

		FCurve& Curve = Out.Curves[CurveIndex];
		Curve.SetNumPoints(DstCurvePointCount);
		OutAllCurves.Add(&Curve);

		// Build curve with unique consecutive points
		uint32 DstPointIndex = 0;
		for (uint32 SrcPointIndex = 0; SrcPointIndex < SrcCurvePointCount; ++SrcPointIndex)
		{
			const FVector3f P0 = HairData.StrandsPoints.PointsPosition[CurvePointOffset + SrcPointIndex];
			if (SrcPointIndex>0)
			{
				const FVector3f PPrev = HairData.StrandsPoints.PointsPosition[CurvePointOffset + SrcPointIndex-1];
				const bool bValid = P0 != PPrev;
				if (!bValid)
				{
					continue;
				}
			}

			// Smooth normal
			FVector3f Tgt = FVector3f::ZeroVector;
			if (SrcPointIndex > 0 && SrcPointIndex + 1 < SrcCurvePointCount)
			{
				const FVector3f PPrev = HairData.StrandsPoints.PointsPosition[CurvePointOffset + SrcPointIndex - 1];
				const FVector3f PNext = HairData.StrandsPoints.PointsPosition[CurvePointOffset + SrcPointIndex + 1];
				FVector3f T0 = P0 - PPrev;
				FVector3f T1 = PNext - P0;
				T0.Normalize();
				T1.Normalize();
				Tgt = T0 + T1;
				check(P0 != PPrev || P0 != PNext);
			}
			else if (SrcPointIndex + 1 < SrcCurvePointCount)
			{
				const FVector3f P1 = HairData.StrandsPoints.PointsPosition[CurvePointOffset + SrcPointIndex + 1];
				Tgt = P1 - P0;
				check(P0 != P1);
			}
			else if (SrcPointIndex >= 1)
			{
				const FVector3f P1 = HairData.StrandsPoints.PointsPosition[CurvePointOffset + SrcPointIndex - 1];
				Tgt = P0 - P1;
				check(P0 != P1);
			}
			Tgt.Normalize();


			Curve.Positions[DstPointIndex] = P0;
			Curve.Tangents[DstPointIndex] = Tgt;
			Curve.UCoords[DstPointIndex] = HairData.StrandsPoints.PointsCoordU[CurvePointOffset + SrcPointIndex];
			Curve.Radius[DstPointIndex] = HairData.StrandsPoints.PointsRadius[CurvePointOffset + SrcPointIndex];
			Curve.RootUVs[DstPointIndex] = bHasRootUV ? HairData.StrandsCurves.CurvesRootUV[CurveIndex] : FVector2f::ZeroVector;

			++DstPointIndex;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Procedural groom (for debugging)
static void AddGridCurves(TArray<FCurveGroup>& OutCurveGroups, uint32& OutNumCurves, uint32& OutNumPoints)
{
	OutNumPoints = 0;
	OutNumCurves = 0;

	OutCurveGroups.SetNum(1);
	FCurveGroup& Group = OutCurveGroups[0];
	Group.Curves.Empty();
	
	FMath::RandInit(1337);

	uint32 GridSize = 32;
	Group.Curves.Reserve(GridSize * GridSize);
	for (uint32 Y = 0; Y < GridSize; ++Y)
	{
		for (uint32 X = 0; X < GridSize; ++X)
		{
			FCurve& Curve = Group.Curves.AddDefaulted_GetRef();
			const uint32 NumPointPerCurve = 3;
			Curve.SetNumPoints(NumPointPerCurve);
			for (uint32 It = 0; It < NumPointPerCurve; ++It)
			{
				FCurvePoint P;
				P.Position = FVector3f(X + FMath::FRand() * 0.9f, Y + FMath::FRand() * 0.9f, It);
				P.Tangent = FVector3f(0, 0, 1);
				P.Radius = 0.1f;
				P.UCoord = It / float(NumPointPerCurve-1);
				P.RootUV = FVector2f::ZeroVector;
				Curve.SetPoint(P, It);
			}
			OutNumPoints += NumPointPerCurve;
		}
	}
	OutNumCurves = Group.Curves.Num();
}

static void AddCurledCurves(TArray<FCurveGroup>& OutCurveGroups, uint32& OutNumCurves, uint32& OutNumPoints)
{
	OutNumPoints = 0;
	OutNumCurves = 0;

	OutCurveGroups.SetNum(1);
	FCurveGroup& Group = OutCurveGroups[0];
	Group.Curves.Empty();

	FMath::RandInit(1337);

	uint32 GridSize = 1;
	Group.Curves.Reserve(GridSize * GridSize);
	for (uint32 Y = 0; Y < GridSize; ++Y)
	{
		for (uint32 X = 0; X < GridSize; ++X)
		{
			FCurve& Curve = Group.Curves.AddDefaulted_GetRef();
			const uint32 NumPointPerCurve = 32;
			const uint32 Rotation = 8;
			const uint32 Height = 16;
			const float R = 1;
			Curve.SetNumPoints(NumPointPerCurve);
			FVector3f RootOffset(4 * R * X + FMath::FRand() * 0.5f, 4 * R * Y + FMath::FRand() * 0.5f, 0);
			for (uint32 It = 0; It < NumPointPerCurve; ++It)
			{
				const float S = It / float(NumPointPerCurve);
				const float A = S * Rotation * 2 * PI;

				FCurvePoint P;
				P.Position = FVector3f(FMath::Cos(A) * R, FMath::Sin(A) * R, S * Height) + RootOffset;
				P.Tangent = FVector3f(-FMath::Sin(A), FMath::Cos(A), Height / float(Rotation * 2 * PI));
				P.Tangent.Normalize();
				P.Radius = 0.1f;
				P.UCoord = It / float(NumPointPerCurve-1);
				P.RootUV = FVector2f::ZeroVector;
				Curve.SetPoint(P, It);
			}
			OutNumPoints += NumPointPerCurve;
		}
	}
	OutNumCurves = Group.Curves.Num();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void InitNaniteBuildInputAsCurves(Nanite::IBuilderModule::FInputMeshData& InputMeshData, const FHairDescription& InDescription)
{
	// Transform input data into curve array for easier manipulation/resampling
	const bool bRooUVs = InDescription.HasAttribute(EHairAttribute::RootUV);
	const uint32 NumTexCoords = bRooUVs ? 2u : 1u;
	TArray<FCurveGroup> CurveGroups;
	uint32 NumPoints = 0;
	uint32 NumCurves = 0;
	if (GetNaniteGroomProceduralShape() == 0)
	{
		// Read & build data from hair description
		uint32 TotalCurveCount = 0;
		TArray<FHairStrandsDatas> Groups;
		{
			FHairDescriptionGroups HairDescriptionGroups;
			FGroomBuilder::BuildHairDescriptionGroups(InDescription, HairDescriptionGroups, false /*bAllowAddEndControlPoint*/);		

			Groups.Reserve(HairDescriptionGroups.HairGroups.Num());
			for (const FHairDescriptionGroup& Group : HairDescriptionGroups.HairGroups)
			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS;
				FGroomBuilder::BuildData(Group.Strands, Groups.AddDefaulted_GetRef());
				PRAGMA_ENABLE_DEPRECATION_WARNINGS;				
				TotalCurveCount += Group.Strands.GetNumCurves();
			}
		}

		// Convert FHairStrandsData into FCurve
		TArray<FCurve*> AllCurves;
		AllCurves.Reserve(TotalCurveCount);
		CurveGroups.Reserve(Groups.Num());
		for (const FHairStrandsDatas& HairData : Groups)
		{
			FCurveGroup& Group = CurveGroups.AddDefaulted_GetRef();
			ConvertHairData(HairData, Group, AllCurves);
		}

		// Resample all curves to ensure all curves have the same number of points
		{
			uint32 MinNumPointPerCurve = ~0u;
			uint32 MaxNumPointPerCurve = 0u;
			uint32 TotalNumPoints = 0u;
			for (FCurve* Curve : AllCurves)
			{
				const uint32 CurveNumPoints = Curve->GetNumPoints();
				MinNumPointPerCurve = FMath::Min(MinNumPointPerCurve, CurveNumPoints);
				MaxNumPointPerCurve = FMath::Max(MaxNumPointPerCurve, CurveNumPoints);
				TotalNumPoints += CurveNumPoints;
			}
			const uint32 GlobalMaxNumPointPerCurve = GetNaniteGroomMaxNumPointPerCurve();
			const uint32 TargetPointCount = FMath::Clamp(uint32(FMath::RoundDownToPowerOfTwo64(MaxNumPointPerCurve)), 2u, GlobalMaxNumPointPerCurve);

			ParallelFor(AllCurves.Num(), [&AllCurves, TargetPointCount] (uint32 InCurveIndex)
			{
				FCurve* Curve = AllCurves[InCurveIndex];
				FCurve ResampledCurve;
				Resample(*Curve, ResampledCurve, TargetPointCount, true /*bSmooth*/);
				*Curve = ResampledCurve;
			});
		}

		// Count
		for (FCurveGroup& Group : CurveGroups)
		{
			NumCurves += Group.GetNumCurves();
			NumPoints += Group.GetNumPoints();
		}
	}
	else
	{
		switch (GetNaniteGroomProceduralShape())
		{
			case 1 : AddGridCurves(CurveGroups, NumCurves, NumPoints); break;
			case 2 : AddCurledCurves(CurveGroups, NumCurves, NumPoints); break;
			default: check(false);
		}
	}

	InputMeshData.CurveIndices.SetNum(NumCurves);
	InputMeshData.CurveCounts.Add(NumCurves);

	// Build new vertex buffers
	InputMeshData.NumTexCoords = NumTexCoords;
	InputMeshData.MaterialIndices.SetNumUninitialized(NumCurves);
	InputMeshData.Vertices.Position.SetNumUninitialized(NumPoints);
	InputMeshData.Vertices.CurveRadius.SetNumUninitialized(NumPoints);
	InputMeshData.Vertices.TangentX.SetNumUninitialized(NumPoints);
	InputMeshData.Vertices.TangentY.SetNumUninitialized(NumPoints);
	InputMeshData.Vertices.TangentZ.SetNumUninitialized(NumPoints);
	InputMeshData.Vertices.UVs.SetNum(NumTexCoords);
	for (uint32 UVCoord = 0; UVCoord < NumTexCoords; ++UVCoord)
	{
		InputMeshData.Vertices.UVs[UVCoord].SetNumUninitialized(NumPoints);
	}
	InputMeshData.TriangleIndices.Empty();
	InputMeshData.TriangleCounts.Empty();

	FStaticMeshSectionArray Sections;
	Sections.Reserve(CurveGroups.Num());

	int32 SectionIndex = 0;
	int32 SectionBaseIndex = 0;
	int32 SectionBaseVertexIndex = 0;
	int32 SectionMaterialIndex = 0;
	uint32 CheckIndices = 0;
	uint32 CheckPoints = 0;
	
	for (const FCurveGroup& Group : CurveGroups)
	{
		check(CheckIndices == SectionBaseIndex);
		check(CheckPoints == SectionBaseVertexIndex);

		int32 SectionPointIndex = 0;
		const uint32 CurveCount = Group.GetNumCurves();
		for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
		{
			const FCurve& Curve = Group.Curves[CurveIndex];
			const uint32 CurvePointCount = Curve.GetNumPoints();
			check(CurvePointCount >= 2);

			InputMeshData.CurveIndices[SectionBaseIndex + CurveIndex].Offset = SectionBaseVertexIndex + SectionPointIndex;
			InputMeshData.CurveIndices[SectionBaseIndex + CurveIndex].Count = CurvePointCount;
			InputMeshData.MaterialIndices[SectionBaseIndex + CurveIndex] = SectionMaterialIndex;

			for (uint32 PointIndex = 0; PointIndex < CurvePointCount; ++PointIndex)
			{
				const FVector3f P   = Curve.Positions[PointIndex];
				const FVector3f Tgt = Curve.Tangents[PointIndex];

				FVector3f T, N, B;
				GetTangentBasis(Tgt, T, B, N);

				// Vertices
				InputMeshData.Vertices.Position[SectionBaseVertexIndex + SectionPointIndex] = P;
				InputMeshData.Vertices.TangentX[SectionBaseVertexIndex + SectionPointIndex] = T;
				InputMeshData.Vertices.TangentY[SectionBaseVertexIndex + SectionPointIndex] = B;
				InputMeshData.Vertices.TangentZ[SectionBaseVertexIndex + SectionPointIndex] = N;
				InputMeshData.Vertices.CurveRadius[SectionBaseVertexIndex + SectionPointIndex] = Curve.Radius[PointIndex];

				InputMeshData.VertexBounds += P;

				// Curve UV
				{
					InputMeshData.Vertices.UVs[0][SectionBaseVertexIndex + SectionPointIndex] = FVector2f(FMath::Clamp(Curve.UCoords[PointIndex], 0.f, 1.f), 0.f);
				}
				// Root UV
				if (bRooUVs)
				{
					InputMeshData.Vertices.UVs[1][SectionBaseVertexIndex + SectionPointIndex] = Curve.RootUVs[PointIndex];
				}
				++SectionPointIndex;
			}
		}

		// Section
		FStaticMeshSection& Section = Sections.AddDefaulted_GetRef();
		Section.MaterialIndex = SectionMaterialIndex;
		Section.FirstIndex = SectionBaseIndex;
		Section.MinVertexIndex = SectionBaseVertexIndex;
		Section.MaxVertexIndex = SectionBaseVertexIndex + SectionPointIndex - 1;
		Section.bEnableCollision = false;
		Section.bCastShadow = true;
		Section.bVisibleInRayTracing = false;
		Section.bAffectDistanceFieldLighting = false;
		Section.bForceOpaque = true;
		Section.UVDensities[0] = 1;
		Section.Weights[0] = 1;

		// For next section
		++SectionIndex;
		++SectionMaterialIndex;
		SectionBaseIndex += CurveCount;
		SectionBaseVertexIndex += SectionPointIndex;

		CheckIndices += CurveCount;
		CheckPoints += SectionPointIndex;
	}

	InputMeshData.Sections = Nanite::BuildMeshSections(Sections);

	check(CheckPoints == InputMeshData.Vertices.Position.Num());
	check(CheckIndices == InputMeshData.CurveIndices.Num());
	check(NumCurves == CheckIndices);
	check(NumPoints == CheckPoints);

	// TODO: Nanite-Skinning
	// We can save memory by figuring out the max number of influences across all sections instead of allocating MAX_TOTAL_INFLUENCES
	// Also check if any of the sections actually require 16bit, or if 8bit will suffice
	bool b16BitSkinning = false;
	InputMeshData.NumBoneInfluences = 0;
	InputMeshData.Vertices.BoneIndices.SetNum(InputMeshData.NumBoneInfluences);
	InputMeshData.Vertices.BoneWeights.SetNum(InputMeshData.NumBoneInfluences);
}

static bool BuildNanite(Nanite::FResources& OutNaniteResources, const FHairDescription& InDescription)
{
	Nanite::IBuilderModule::FInputMeshData InputMeshData;
	InitNaniteBuildInputAsCurves(InputMeshData, InDescription);

	FMeshNaniteSettings NaniteSettings;
	NaniteSettings.bEnabled = true;
	NaniteSettings.ShapePreservation = ENaniteShapePreservation::None;
	NaniteSettings.bVoxelNDF = 0;
	NaniteSettings.bVoxelOpacity = 1;
	NaniteSettings.VoxelLevel = 0;
	NaniteSettings.bExplicitTangents = true;

	Nanite::IBuilderModule* NaniteBuilder = &Nanite::IBuilderModule::Get();
	if (!NaniteBuilder->Build(
		OutNaniteResources,
		InputMeshData,
		nullptr, // fallback
		nullptr, // OutRayTracingFallbackMeshData
		nullptr, // RayTracingFallbackBuildSettings
		NaniteSettings,
		nullptr) // fallback
		)
	{
		return false;
	}

	return true;
}

bool FGroomAssetNaniteBuilder::Build(TPimplPtr<Nanite::FResources>& OutNaniteResourcesPtr, const UGroomAsset* InGroomAsset)
{
	check(InGroomAsset);

	FHairDescription MeshDescription = InGroomAsset->GetHairDescription();
	if (!MeshDescription.IsValid())
	{
		UE_LOG(LogHairStrands, Error, TEXT("Failed to build groom asset. Invalid Hair Description"));
		return false;
	}

	ClearNaniteResources(OutNaniteResourcesPtr);

	const bool bBuildSuccess = BuildNanite(*OutNaniteResourcesPtr.Get(), MeshDescription);
	if (!bBuildSuccess)
	{
		UE_LOG(LogHairStrands, Error, TEXT("Failed to build Nanite for groom asset. See previous line(s) for details."));
	}

	return bBuildSuccess;
}

#endif // WITH_EDITOR
