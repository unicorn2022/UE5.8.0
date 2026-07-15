// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetaHumanConformSolverSettings.generated.h"

UENUM(BlueprintType)
enum class EWeightScheduleCurve : uint8
{
	Static,
	Linear,
	Quadratic,
	Log
};

USTRUCT(BlueprintType)
struct FWeightSchedule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0", UIMax = "1000"))
	float Start = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (EditCondition = "Curve != EWeightScheduleCurve::Static", ClampMin = "0", UIMax = "1000"))
	float End = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights")
	EWeightScheduleCurve Curve = EWeightScheduleCurve::Static;
};


USTRUCT(BlueprintType)
struct FBodyConformSolveSettings
{
	GENERATED_BODY();
	
	/* Name of the solver pipeline to use. */
	UPROPERTY(BlueprintReadWrite, Category = "Pipeline Solve")
	FString PipelineName;

	/* Whether to solve for body pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	bool bSolvePose = true;

	/* Enforces left - right body symmetry during fitting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	bool bSymmetricalSolve = false;
	
	/* Number of fitting passes. More = finer result but slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve", meta = (ClampMin = "0", UIMax = "100"))
	int Iterations = 13;

	/* How strongly the surface tries to match the scan. Higher = tighter fit to scan geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule IcpGeometryWeight = { 70.0f, 70.0f, EWeightScheduleCurve::Static };

	/* Maximum distance (mm) to consider a scan point as a match. Ramps down each iteration for progressively tighter matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule IcpSearchTolerance = { 50.0f, 50.0f, EWeightScheduleCurve::Static };

	/* Rejects scan points whose surface normal disagrees with the model normal. Lower = more permissive matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule IcpNormalCompatibility = { 0.8f, 0.8f, EWeightScheduleCurve::Static };

	/* How strongly detected body keypoints (shoulders, hips, etc.) pull the rig into pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule IcpKeyPointWeight = { 1.0f, 1.0f, EWeightScheduleCurve::Static };

	/* How strongly 2D landmark positions guide the fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule IcpLandmarksWeight = { 0.557f, 0.0f, EWeightScheduleCurve::Linear };

	/* Resistance to overall shape change. Higher = stays closer to the base model shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule RegularizationGlobalControls = { 0.682f, 0.682f, EWeightScheduleCurve::Static };

	/* Resistance to local surface deformation. Higher = smoother surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule RegularizationLocalControls = { 1.0f, 1.0f, EWeightScheduleCurve::Static };

	/* Resistance to changing body proportions. Higher = preserves original proportions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule RegularizationProportions = { 1.0f, 1.0f, EWeightScheduleCurve::Static };

	/* Resistance to changing joint angles. Higher = stays closer to detected pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve")
	FWeightSchedule RegularizationPose = { 2.0f, 0.5f, EWeightScheduleCurve::Linear };

	/* Number of points used to resample curve constraints. Higher = more accurate curve matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Solve", meta = (ClampMin = "0", UIMax = "100"))
	int CurveResampling = 5;

	/* Number of fitting passes for face solve. More = finer result but slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve", meta = (ClampMin = "0", UIMax = "100"))
	int FaceIterations = 7;
	
	/* How strongly the face surface tries to match the scan. Higher = tighter fit to scan geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule FaceIcpWeight = { 30.0f, 30.0f, EWeightScheduleCurve::Static };

	/* Maximum distance (mm) to consider a scan point as a match for face solve. Ramps down each iteration for progressively tighter matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule FaceIcpSearchTolerance = { 30.0f, 30.0f, EWeightScheduleCurve::Static };

	/* Rejects scan points whose surface normal disagrees with the model normal for face solve. Lower = more permissive matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule FaceNormalCompatibility = { 0.8f, 0.8f, EWeightScheduleCurve::Static };

	/* How strongly detected face keypoints (eye corners, nose tip, etc.) pull the rig into pose for face solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule FaceKeypointWeight = { 10.0f, 10.0f, EWeightScheduleCurve::Static };

	/* How strongly 2D face landmark positions guide the face fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule FaceLandmark2DWeight = { 0.2f, 0.2f, EWeightScheduleCurve::Static };

	/* Regularization strength for the face patch blend model. Higher = less face deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule ModelRegularization = { 10.0f, 10.0f, EWeightScheduleCurve::Static };

	/* Smoothness penalty for face patch deformation. Higher = smoother face patches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Solve")
	FWeightSchedule PatchSmoothness = { 1.0f, 1.0f, EWeightScheduleCurve::Static };

	UPROPERTY(BlueprintReadWrite, Category = "Face Solve", meta = (ClampMin = "0", UIMax = "100"))
	float LandmarkDamping = 0.01f;
	
	/* Apply neck seam smoothing after solve */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neck Seam")
	bool bApplyNeckSeamSmoothing = true;
	
	/* Number of fitting passes for the neck seam. More = finer result but slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neck Seam", meta = (ClampMin = "0", UIMax = "100"))
	int SeamIterations = 3;

	/* Laplacian smoothness regularization on vertex offsets for the neck seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neck Seam", meta = (ClampMin = "0", UIMax = "100"))
	float SeamLaplacian = 1.5f;

	/* Number of rings around neck seam to apply smoothing over */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neck Seam", meta = (ClampMin = "0", UIMax = "100"))
	int SeamRings = 12;
};

USTRUCT(BlueprintType)
struct FRefinementSettings
{
	GENERATED_BODY();
	
	/* Number of fitting passes. More = finer result but slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	int Iterations = 5;

	/* Weight of the vertex position constraint relative to other constraints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float VertexWeight = 0.5f;

	/* How strongly detected body keypoints (shoulders, hips, etc.) pull the rig into pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float KeypointWeight = 0.0f;

	/* How strongly 2D face landmark positions guide the face fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float Landmark2DWeight = 0.0f;

	/* Laplacian smoothness regularization on vertex offsets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float Laplacian = 0.8f;
	
	/* Bending resistance for triangle mesh deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float Bending = 0.0f;
	
	/* Strain regularization resisting stretching and compression. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float Strain = 0.135f;

	/* Regularization penalizing the magnitude of per-vertex offsets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float VertexOffsetReg = 0.0f;

	/* Small regularization term on vertex variables for numerical stability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float VertexRegularization = 3e-3f;

	/* Maximum distance (mm) to consider a scan point as a match. Ramps down each iteration for progressively tighter matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refinement", meta = (ClampMin = "0", UIMax = "100"))
	float DistanceTolerance = 5.95f;

};
