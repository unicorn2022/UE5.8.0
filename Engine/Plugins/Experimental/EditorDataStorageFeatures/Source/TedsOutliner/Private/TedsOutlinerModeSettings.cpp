// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerModeSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TedsOutlinerModeSettings)

TObjectPtr<UTedsOutlinerConfig> UTedsOutlinerConfig::Instance = nullptr;

void UTedsOutlinerConfig::Initialize()
{
	if(!Instance)
	{
		Instance = NewObject<UTedsOutlinerConfig>(); 
		Instance->AddToRoot();
	}
}
