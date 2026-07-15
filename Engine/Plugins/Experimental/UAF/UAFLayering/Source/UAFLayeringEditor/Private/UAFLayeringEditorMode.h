// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "UAFLayeringEditorMode.generated.h"

UCLASS()
class UUAFLayeringEditorMode : public UEdMode
{
	GENERATED_BODY()
public:
	const static FEditorModeID EM_UAFLayeringMode;
	
	UUAFLayeringEditorMode();
};
