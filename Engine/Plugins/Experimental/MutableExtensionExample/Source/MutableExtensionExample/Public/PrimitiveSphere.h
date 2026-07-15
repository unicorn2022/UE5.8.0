// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PrimitiveSphere.generated.h"


USTRUCT(DisplayName = "Sphere")
struct FPrimitiveSphere
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Primitive)
	FSphere3d Sphere;
};