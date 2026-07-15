// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFTestBindableTraitData.h"
#include "TraitCore/TraitBinding.h"

namespace UE::UAF
{
	AUTO_REGISTER_ANIM_TRAIT(FTestBindableTrait)
	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FTestBindableTrait, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
}
