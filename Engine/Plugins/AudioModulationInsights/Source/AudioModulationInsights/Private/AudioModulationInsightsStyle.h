// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Textures/SlateIcon.h"
#include "UObject/NameTypes.h"

class FAudioModulationInsightsStyle final : public FSlateStyleSet
{
public:
	FAudioModulationInsightsStyle();
	~FAudioModulationInsightsStyle();
	
	static const FAudioModulationInsightsStyle& Get();
	static const FName& GetStyleName();

	FSlateIcon CreateIcon(const FName& InName) const;
};
