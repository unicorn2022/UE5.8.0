// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"        
#include "Containers/Map.h"        
#include "Math/Vector.h"          
#include "MetaHumanConformSolverSettings.h"
#include "Camera/CameraTypes.h"

#include "MetaHumanConformTargetParams.generated.h"

UENUM(BlueprintType)
enum class ETargetPartsType : uint8
{
	Combined,
	BodyOnly,
	HeadOnly,
	HeadAndBody
};

USTRUCT(BlueprintType)
struct FTrackingPoints
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<FVector2D> TrackingPoints;
};

USTRUCT(BlueprintType)
struct FConformTargetMesh
{
	GENERATED_BODY();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	ETargetPartsType TargetPartsType = ETargetPartsType::Combined;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<FVector3f> BodyVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<int32> BodyVertexIndices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<FVector3f> BodyJointRotations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<FVector3f> HeadVertices;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<int32> HeadVertexIndices;
};

/**
* Struct to contain parameters used to conform to a target
*/
USTRUCT(BlueprintType)
struct FConformTargetParams
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FConformTargetMesh ConformTargetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TMap<int32, FVector3f> KeyPointTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TMap<FString, FTrackingPoints> CurveTrackingPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FMinimalViewInfo CameraViewInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FIntPoint ImageSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	bool bEstimateBodyJointsFromMesh = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	bool bAutoSolve = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FBodyConformSolveSettings BodyConformSolveSettings;
};


USTRUCT(BlueprintType)
struct FRefinementTargetParams
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FConformTargetMesh ConformTargetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TMap<int32, FVector3f> KeyPointTargets;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TMap<FString, FTrackingPoints> CurveTrackingPoints;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FMinimalViewInfo CameraViewInfo;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FIntPoint ImageSize = FIntPoint::ZeroValue;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FRefinementSettings RefinementSettings;
};