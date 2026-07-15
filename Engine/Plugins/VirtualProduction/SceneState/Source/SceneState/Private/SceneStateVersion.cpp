// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneStateVersion.h"
#include "Serialization/CustomVersion.h"

namespace UE::SceneState
{
	const FGuid FVersion::Guid(0x2333E4E3, 0xA940435F, 0x8B481930, 0x906B27F9);
	FCustomVersionRegistration GRegisterSceneStateVersion(FVersion::Guid, FVersion::LatestVersion, TEXT("SceneStateVersion"));

} // UE::SceneState
