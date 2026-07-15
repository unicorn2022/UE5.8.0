// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/CommonTypes.h"

#define UE_API TEDSEDITORCOMPATIBILITY_API

class FWorldPartitionActorDescInstance;

namespace UE::Editor::DataStorage::UnloadedActor
{
	UE_API FName GetActorDescTableName();
	
	UE_API FName GetActorDescMappingDomain();
	
	UE_API FMapKey GetActorDescMappingKey(const FWorldPartitionActorDescInstance* ActorDescInstance);
}

#undef UE_API