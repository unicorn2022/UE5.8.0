// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/UAFAssetData.h"

#if WITH_EDITOR
FString FUAFAssetData::GetName() const
{
	TArray<const UObject*> Objects;
	GetObjectReferences(Objects);
	return Objects.Num() > 0 ? GetNameSafe(Objects[0]) : FString();
}
#endif

