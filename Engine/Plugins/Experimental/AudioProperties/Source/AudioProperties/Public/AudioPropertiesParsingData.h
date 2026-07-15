// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "UObject/NameTypes.h"

class UClass;
struct FAudioPropertiesSheet;

namespace UE
{
	namespace AudioGameplay
	{
		using FAudioPropertiesBindings = TMap<FName, FName>;

		struct FAudioPropertiesParsingData
		{
			const FAudioPropertiesSheet* SourcePropertySheet = nullptr;
			UClass* TargetClass = nullptr;
			FAudioPropertiesBindings SourceToTargetPropertyBindings;
			bool bCreateNewObjects = false;
			TArray<FAudioPropertiesParsingData> InstancedObjectsParsingData;
		};
	}
}

