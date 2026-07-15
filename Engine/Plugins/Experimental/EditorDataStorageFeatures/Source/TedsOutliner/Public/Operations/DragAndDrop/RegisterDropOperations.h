// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE::Editor::DataStorage
{
class ICoreProvider;
}

#define UE_API TEDSOUTLINER_API

namespace UE::Editor::DataStorage::Operations
{
	UE_API void RegisterReparentActor(ICoreProvider& Storage);
	UE_API void RegisterReparentFolder(ICoreProvider& Storage);

	inline void RegisterDropOperations(ICoreProvider& Storage)
	{
		RegisterReparentActor(Storage);
		RegisterReparentFolder(Storage);
	}
}

#undef UE_API
