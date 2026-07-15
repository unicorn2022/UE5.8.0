// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextVariableReferenceTest.generated.h"

USTRUCT()
struct FUAFVariableReferenceTestStruct
{
	GENERATED_BODY()
	
	UPROPERTY()
	bool Renamed_BoolVariable = false;
	
	UPROPERTY()
	float FloatVariable = 0.f;
};
