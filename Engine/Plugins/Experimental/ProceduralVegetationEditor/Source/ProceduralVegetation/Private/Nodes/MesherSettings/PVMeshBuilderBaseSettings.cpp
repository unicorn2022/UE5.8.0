// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVMeshBuilderBaseSettings.h"
#include "PVCommon.h"

#define LOCTEXT_NAMESPACE "PVMeshBuilderBaseSettings"

#if WITH_EDITOR
FLinearColor UPVMeshBuilderBaseSettings::GetNodeTitleColor() const
{
	return PV::NodeColors::Mesh;
}

FText UPVMeshBuilderBaseSettings::GetCategoryOverride() const
{
	return PV::Categories::MeshBuilderSettings;
}



FText UPVMeshBuilderBaseSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "\n\nPress Ctrl + L to lock/unlock node output");
}

#endif

TArray<FPCGPinProperties> UPVMeshBuilderBaseSettings::InputPinProperties() const
{
	return TArray<FPCGPinProperties>();
}

#undef LOCTEXT_NAMESPACE
