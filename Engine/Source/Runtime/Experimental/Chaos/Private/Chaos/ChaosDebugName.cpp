// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/ChaosDebugName.h"
#include "CoreTypes.h"

namespace Chaos
{
	TSharedPtr<FString, ESPMode::ThreadSafe> FSharedDebugName::DefaultName = TSharedPtr<FString, ESPMode::ThreadSafe>(new FString(TEXT("NoName")));
}