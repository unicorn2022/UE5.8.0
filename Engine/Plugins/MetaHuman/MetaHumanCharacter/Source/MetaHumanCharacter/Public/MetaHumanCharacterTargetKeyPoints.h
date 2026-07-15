// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "MetaHumanCharacterTargetKeyPoints.generated.h"

UENUM(BlueprintType)
enum class EKeyPointType : uint8
{
	Custom,    
	PresetMH, 
	PresetTarget  
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTargetKeyPoints
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Points")
	TMap<FName, int32> CharacterBodyVertexIndexes;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Points")
	TMap<FName, int32> CharacterHeadVertexIndexes;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Points")
	TMap<FName, FVector3f> TargetBodyPositions;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Points")
	TMap<FName, FVector3f> TargetHeadPositions;


	bool operator==(const FMetaHumanCharacterTargetKeyPoints& Other) const
	{
		return CharacterBodyVertexIndexes.OrderIndependentCompareEqual(Other.CharacterBodyVertexIndexes) && 
			CharacterHeadVertexIndexes.OrderIndependentCompareEqual(Other.CharacterHeadVertexIndexes) && 
			TargetBodyPositions.OrderIndependentCompareEqual(Other.TargetBodyPositions) &&
			TargetHeadPositions.OrderIndependentCompareEqual(Other.TargetHeadPositions);
	}
	
	bool operator!=(const FMetaHumanCharacterTargetKeyPoints& Other) const
	{
		return !(*this == Other);
	}
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTargetMeshKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TSoftObjectPtr<UObject> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TSoftObjectPtr<UObject> HeadMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TSoftObjectPtr<UObject> CombinedMesh;

	bool operator==(const FMetaHumanCharacterTargetMeshKey& Other) const
	{
		return BodyMesh == Other.BodyMesh && HeadMesh == Other.HeadMesh && CombinedMesh == Other.CombinedMesh;
	}

	bool operator!=(const FMetaHumanCharacterTargetMeshKey& Other) const
	{
		return !(*this == Other);
	}
};

FORCEINLINE FArchive& operator<<(FArchive& Ar, FMetaHumanCharacterTargetMeshKey& K)
{
	Ar << K.BodyMesh;
	Ar << K.HeadMesh;
	Ar << K.CombinedMesh;
	return Ar;
}

FORCEINLINE uint32 GetTypeHash(const FMetaHumanCharacterTargetMeshKey& Key)
{
	return HashCombine(GetTypeHash(Key.BodyMesh), GetTypeHash(Key.HeadMesh), GetTypeHash(Key.CombinedMesh));
}

FORCEINLINE bool operator<(const FMetaHumanCharacterTargetMeshKey& Left,
						   const FMetaHumanCharacterTargetMeshKey& Right)
{
	auto ComparePaths = [](const TSoftObjectPtr<UObject>& Left,
				  const TSoftObjectPtr<UObject>& Right)
		{
			return Left.ToSoftObjectPath().ToString() < Right.ToSoftObjectPath().ToString();
		};

	if (ComparePaths(Left.BodyMesh, Right.BodyMesh)) return true;
	if (ComparePaths(Right.BodyMesh, Left.BodyMesh)) return false;

	if (ComparePaths(Left.HeadMesh, Right.HeadMesh)) return true;
	if (ComparePaths(Right.HeadMesh, Left.HeadMesh)) return false;

	return ComparePaths(Left.CombinedMesh, Right.CombinedMesh);
}

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTargetKeyPointCollection
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key Points")
	TMap<FMetaHumanCharacterTargetMeshKey, FMetaHumanCharacterTargetKeyPoints> PerMeshTargetKeyPoints;	
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterCurveTrackingPoints
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TArray<FVector2D> Points;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTargetTrackingResults
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FMinimalViewInfo CameraViewInfo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	FIntPoint ImageSize = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TMap<FString, FMetaHumanCharacterCurveTrackingPoints> CurveTrackingPoints;

	UPROPERTY(BlueprintReadWrite, Category = "Conform")
	TArray<FColor> TrackedImage;

	/** Control vertex positions per curve, saved after tracking so they can be restored exactly on
	    reload without re-running the lossy shape-annotation fitting pass. */
	UPROPERTY()
	TMap<FString, FMetaHumanCharacterCurveTrackingPoints> ControlVertexPoints;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTargetTrackingResultsCollection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conform")
	TMap<FMetaHumanCharacterTargetMeshKey, FMetaHumanCharacterTargetTrackingResults> PerMeshTrackingResults;
};