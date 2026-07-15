// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Graph/PCGStackContext.h"

#include "PVToolContext.generated.h"

class UPVData;

/** PVE-specific context object stored alongside UPCGNodeToolContext in the context store.
 *  Carries vegetation data that PCG's generic context does not hold. */
UCLASS()
class UPVToolContextObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGStack InspectionStack;
};
