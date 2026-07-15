// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTerrainModeSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MeshTerrainModeSettings)


#define LOCTEXT_NAMESPACE "MeshTerrainModeSettings"


FText UMeshTerrainModeSettings::GetSectionText() const 
{ 
	return LOCTEXT("MeshTerrainModeSettingsName", "Mesh Terrain Mode"); 
}

FText UMeshTerrainModeSettings::GetSectionDescription() const
{
	return LOCTEXT("MeshTerrainModeSettingsDescription", "Configure the Mesh Terrain Editor Mode plugin");
}


FText UMeshTerrainModeCustomizationSettings::GetSectionText() const 
{ 
	return LOCTEXT("MeshTerrainModeSettingsName", "Mesh Terrain Mode"); 
}

FText UMeshTerrainModeCustomizationSettings::GetSectionDescription() const
{
	return LOCTEXT("MeshTerrainModeSettingsDescription", "Configure the Mesh Terrain Editor Mode plugin");
}


#undef LOCTEXT_NAMESPACE
