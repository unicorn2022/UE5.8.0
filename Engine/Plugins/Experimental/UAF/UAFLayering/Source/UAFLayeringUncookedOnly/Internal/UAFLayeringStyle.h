// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

class FUAFLayeringStyle final : public FSlateStyleSet
{
public:
	static UAFLAYERINGUNCOOKEDONLY_API FUAFLayeringStyle& Get();

private:
	FUAFLayeringStyle();
	~FUAFLayeringStyle();
};