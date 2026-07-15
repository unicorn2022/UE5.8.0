// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"

#include "MetaHumanCharacterEditorMeshTargetHitResult.generated.h"

UENUM()
enum class EHitMeshType : uint8
{
	None,
	TargetBody,
	TargetHead,
	CharacterBody,
	CharacterHead
};

USTRUCT()
struct FMetaHumanTargetHitResult
{
	GENERATED_BODY()
	
	UPROPERTY()
	FHitResult HitResult;
	
	UPROPERTY()
	EHitMeshType HitMeshType = EHitMeshType::None;
	
	UPROPERTY()
	int32 HitVertexID = INDEX_NONE;
};