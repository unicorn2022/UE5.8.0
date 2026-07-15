// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ActorStaticMeshComponentInterface.h"
#include "Components/StaticMeshComponent.h"

#if WITH_EDITOR
void FActorStaticMeshComponentInterface::OnMeshRebuild(bool bRenderDataChanged)
{
	UStaticMeshComponent::GetStaticMeshComponent(this)->OnMeshRebuild(bRenderDataChanged);
}

void FActorStaticMeshComponentInterface::PreStaticMeshCompilation()
{
	UStaticMeshComponent::GetStaticMeshComponent(this)->PreStaticMeshCompilation();
}

void FActorStaticMeshComponentInterface::PostStaticMeshCompilation()
{
	UStaticMeshComponent::GetStaticMeshComponent(this)->PostStaticMeshCompilation();
}
#endif

UStaticMesh* FActorStaticMeshComponentInterface::GetStaticMesh() const
{
	return UStaticMeshComponent::GetStaticMeshComponent(this)->GetStaticMesh();
}

IPrimitiveComponent* FActorStaticMeshComponentInterface::GetPrimitiveComponentInterface()
{
	return UStaticMeshComponent::GetStaticMeshComponent(this)->GetPrimitiveComponentInterface();
}
