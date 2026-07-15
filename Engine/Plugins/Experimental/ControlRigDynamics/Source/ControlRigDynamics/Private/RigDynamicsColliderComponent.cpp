// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigDynamicsColliderComponent.h"

#include "RigDynamicsObjectVersion.h"

//======================================================================================================================
void FRigDynamicsColliderComponent::Save(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRigDynamicsObjectVersion::GUID);

	FRigBaseComponent::Save(Ar);
	Ar << Shapes;
}

//======================================================================================================================
void FRigDynamicsColliderComponent::Load(FArchive& Ar)
{
	FRigBaseComponent::Load(Ar);

	if (Ar.CustomVer(FRigDynamicsObjectVersion::GUID) >= FRigDynamicsObjectVersion::HelperStructs)
	{
		Ar << Shapes;
	}
	else
	{
		Ar << Shapes.Boxes;
		Ar << Shapes.Capsules;
		Ar << Shapes.Planes;
	}
}
