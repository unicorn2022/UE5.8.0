// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigHierarchyImportSettings.generated.h"

class USkeletalMesh;
template<typename T> struct TObjectPtr;

USTRUCT()
struct FRigHierarchyImportSettings
{
	GENERATED_BODY()

	FRigHierarchyImportSettings()
		: Mesh(nullptr)
	{
	}

	UPROPERTY(EditAnywhere, Category = "Hierarchy Import")
	TObjectPtr<USkeletalMesh> Mesh;
};
