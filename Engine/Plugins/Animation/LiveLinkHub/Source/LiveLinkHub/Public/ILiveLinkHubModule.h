// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Modules/ModuleInterface.h"

class ILiveLinkHubModule : public IModuleInterface
{
public:
	/** Retrieve the namespace for Live Link Hub naming tokens. */
	static FString GetLiveLinkHubNamingTokensNamespace()
	{
		return TEXT("llh");
	}
};
