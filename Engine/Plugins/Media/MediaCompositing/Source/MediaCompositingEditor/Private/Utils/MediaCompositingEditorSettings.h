// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "MediaCompositingEditorSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Media Compositing Editor Settings"))
class UMediaCompositingEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category="Media Compositing")
	bool bDisplayMediaTexturePrompt = true;
};
