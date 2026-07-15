// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/Guid.h"

#define UE_API AVALANCHEMODIFIERS_API

namespace UE::AvaModifiers
{

/** Describes the versions of the translucent priority modifier */
struct FTranslucentPriorityModifierVersion
{
private:
	FTranslucentPriorityModifierVersion() = delete;

public:
	enum Type : uint8
	{
		PreVersioning = 0,

		/** Upgraded TWeakObjectPtr to TObjectPtr in UAvaTranslucentPriorityModifierShared */
		UpgradeFromWeak,

		/* ------------------------------------------------------ */
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	UE_API const static FGuid Guid;
};

} // UE::AvaModifiers

#undef UE_API
