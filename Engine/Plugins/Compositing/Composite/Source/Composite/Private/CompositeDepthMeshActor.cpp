// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeDepthMeshActor.h"

#include "Components/CompositeDepthMeshComponent.h"

ACompositeDepthMeshActor::ACompositeDepthMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CompositeDepthMeshComponent = CreateDefaultSubobject<UCompositeDepthMeshComponent>(TEXT("DefaultCompositeMeshComponent"));
	SetRootComponent(CompositeDepthMeshComponent);
}

ACompositeDepthMeshActor::~ACompositeDepthMeshActor() = default;

