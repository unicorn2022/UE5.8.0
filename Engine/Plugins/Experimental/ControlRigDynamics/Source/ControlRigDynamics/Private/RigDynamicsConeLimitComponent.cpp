// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigDynamicsConeLimitComponent.h"

#include "RigDynamicsObjectVersion.h"

//======================================================================================================================
void FRigDynamicsConeLimitComponent::Serialize(FArchive& Ar)
{
	Ar << GrandparentComponentKey;
	Ar << ParentComponentKey;
	Ar << ChildComponentKey;
	Ar << Strength;
	Ar << DampingRatio;
	Ar << Angle;
}

//======================================================================================================================
void FRigDynamicsConeLimitComponent::Save(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRigDynamicsObjectVersion::GUID);
	FRigBaseComponent::Save(Ar);
	Serialize(Ar);
}

//======================================================================================================================
void FRigDynamicsConeLimitComponent::Load(FArchive& Ar)
{
	FRigBaseComponent::Load(Ar);
	Serialize(Ar);
}
