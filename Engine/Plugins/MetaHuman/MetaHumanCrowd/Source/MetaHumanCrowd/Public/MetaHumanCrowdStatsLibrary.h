// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "MetaHumanCrowdStatsLibrary.generated.h"

#define UE_API METAHUMANCROWD_API

class UObject;

/**
 * Lightweight stats about a MetaHuman crowd
 */
USTRUCT(BlueprintType)
struct FMetaHumanCrowdStats
{
	GENERATED_BODY()

	/** Total number of MetaHuman crowd entities currently alive. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Crowd|Debug")
	int32 NumEntities = 0;

	/** Number of entities currently on screen: in LOD range AND inside the view frustum. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Crowd|Debug")
	int32 NumVisible = 0;

	/** Number of entities currently visualized as a spawned actor. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Crowd|Debug")
	int32 NumActors = 0;

	/** Number of entities currently visualized as an instanced skinned mesh. */
	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman|Crowd|Debug")
	int32 NumISKMs = 0;
};


/**
 * Blueprint function library for MetaHuman crowd statistics
 */
UCLASS(MinimalAPI)
class UMetaHumanCrowdStatsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Gather a snapshot of MetaHuman crowd statistics.
	 *
	 * Reads cached per-entity state already maintained by the Mass visualization pipeline.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Crowd|Debug", meta = (WorldContext = "WorldContextObject"))
	static UE_API FMetaHumanCrowdStats GatherMetaHumanCrowdStats(const UObject* WorldContextObject);
};

#undef UE_API
