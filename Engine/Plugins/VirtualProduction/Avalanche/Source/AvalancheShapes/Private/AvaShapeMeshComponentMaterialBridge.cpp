// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaShapeMeshComponentMaterialBridge.h"
#include "AvaShapeActor.h"
#include "Components/DynamicMeshComponent.h"

namespace UE::Ava
{

const UStruct* FShapeMeshComponentMaterialBridge::OnGetBridgedType() const
{
	return UDynamicMeshComponent::StaticClass();
}

bool FShapeMeshComponentMaterialBridge::OnIsMaterialContainerSupported(FConstDataView InMaterialContainer) const
{
	const UDynamicMeshComponent& MeshComponent = InMaterialContainer.Get<const UDynamicMeshComponent>();

	const AAvaShapeActor* ShapeActor = Cast<AAvaShapeActor>(MeshComponent.GetOuter());

	return ShapeActor && ShapeActor->GetShapeMeshComponent() == &MeshComponent;
}
} // UE::Ava
