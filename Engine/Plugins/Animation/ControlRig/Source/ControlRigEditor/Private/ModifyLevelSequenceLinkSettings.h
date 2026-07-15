// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LevelSequenceAnimSequenceLink.h"
#include "ModifyLevelSequenceLinkSettings.generated.h"

//Object to hold the LinkItem so we can modify it in asset details
UCLASS(Transient)
class UModifyLevelSequenceLinkSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = Property, meta = (ShowOnlyInnerProperties))
	FLevelSequenceAnimSequenceLinkItem LinkItem;
};
