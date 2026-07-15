// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetaHumanEvaluateRig.h"
#include "EvaluateRig.generated.h"


#define UE_API METAHUMANCORETECHLIB_API

/**
 * Wrapper struct so we can return an array of mesh vertex data
 */
USTRUCT(BlueprintType)
struct UE_API FArrayOfVertices
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "MetaHuman|EvaluateRig")
	TArray<FVector> Vertices;
};


/** 
*   Blueprint class to allow a DNA rig to be evaluated for raw control values and the mesh vertices returned
*/
UCLASS(BlueprintType, Blueprintable)
class UE_API UEvaluateRig : public UObject
{
	GENERATED_BODY()

public:

	// Set the rig DNA from UDNA asset
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|EvaluateRig")
	bool SetRigDNAFromAsset(UDNA* InDNA)
	{
		return EvaluateRig.SetRigDNA(InDNA);
	}

	// Set the rig DNA from legacy DNAAsset (deprecated - use UDNA instead)
	UE_DEPRECATED(5.8, "Use SetRigDNAFromAsset(UDNA*) instead")
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|EvaluateRig", meta = (DeprecatedFunction, DeprecationMessage = "Use SetRigDNAFromAsset with UDNA asset instead"))
	bool SetRigDNA(UDNAAsset* InDNAAsset)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return EvaluateRig.SetRigDNA(InDNAAsset);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// evaluate the passed in raw controls for the specified mesh indices and lod, returning the evaluated vertices for each mesh in OutMeshVertices. Note that only valid raw control names will be set in the rig
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|EvaluateRig")
	void EvaluateRawControls(const TMap<FString, float>& InControls, const TArray<int>& InMeshIndices, int32 InLod, TArray<FArrayOfVertices>& OutMeshVertices, bool & bOutSuccess) const
	{
		TArray<TArray<FVector>> MeshVertices;
		bOutSuccess = EvaluateRig.EvaluateRawControls(InControls, InMeshIndices, InLod, MeshVertices);
		if (bOutSuccess)
		{
			OutMeshVertices.SetNum(MeshVertices.Num());
			for (int32 I = 0; I < MeshVertices.Num(); ++I)
			{
				OutMeshVertices[I].Vertices = MoveTemp(MeshVertices[I]);
			}
		}
	}

private:
	UE::Wrappers::FMetaHumanEvaluateRig EvaluateRig;
};

#undef UE_API
