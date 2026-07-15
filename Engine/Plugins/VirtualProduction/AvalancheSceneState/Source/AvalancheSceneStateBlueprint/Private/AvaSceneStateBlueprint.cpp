// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaSceneStateBlueprint.h"
#include "AvaSceneStateSchema.h"
#include "SceneStateVersion.h"

void UAvaSceneStateBlueprint::PostLoad()
{
	Super::PostLoad();

	// Previous versions to default to the motion design schema type
	if (GetLinkerCustomVersion(UE::SceneState::FVersion::Guid) < UE::SceneState::FVersion::Schema)
	{
		SetSceneStateSchema(UAvaSceneStateSchema::StaticClass());
	}
}
