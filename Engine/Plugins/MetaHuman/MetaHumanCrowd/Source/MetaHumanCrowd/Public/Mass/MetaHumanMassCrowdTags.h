// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MetaHumanMassCrowdTags.generated.h"

/**
 * Tag added to Mass entities whose high-res actor implements IMetahumanMassCrowdActorBlueprintInterface.
 * Used to filter entities that have a player-controllable or interface-driven actor representation.
 */
USTRUCT()
struct FMetahumanMassCrowdActorTag : public FMassTag
{
	GENERATED_BODY()
};
