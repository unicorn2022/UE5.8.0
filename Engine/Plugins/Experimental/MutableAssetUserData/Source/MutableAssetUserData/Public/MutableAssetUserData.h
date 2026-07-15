// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectPtr.h"

#include "MutableAssetUserData.generated.h"

class UAssetUserData;

USTRUCT(BlueprintType, meta = (DisplayName = "Asset User Data", PinColor = "#949494"))
struct FMutableAssetUserData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Transient, Category = AssetUserData)
	TObjectPtr<UAssetUserData> AssetUserData;
};