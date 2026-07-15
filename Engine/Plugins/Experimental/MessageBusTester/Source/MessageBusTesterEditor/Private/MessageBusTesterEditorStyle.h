// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

/**
 * Slate style set that defines all the styles for the MessageBus Tester UI
 */
class FMessageBusTesterEditorStyle : public FSlateStyleSet
{
public:
	/** Access the singleton instance for this style set */
	static FMessageBusTesterEditorStyle& Get();


private:
	FMessageBusTesterEditorStyle();
	~FMessageBusTesterEditorStyle();
};
