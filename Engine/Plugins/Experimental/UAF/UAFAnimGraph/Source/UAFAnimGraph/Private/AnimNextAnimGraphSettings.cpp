// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextAnimGraphSettings.h"
#include "Factory/AnimGraphFactory.h"
#include "UAF/UAFAssetFactory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextAnimGraphSettings)

TArray<UClass*> UAnimNextAnimGraphSettings::GetAllowedAssetClasses()
{
	TArray<UClass*> Array;
	Array = UE::UAF::FAssetDataFactory::GetRegisteredObjectClasses<FUAFGraphFactoryAsset>();
	return Array;
}
