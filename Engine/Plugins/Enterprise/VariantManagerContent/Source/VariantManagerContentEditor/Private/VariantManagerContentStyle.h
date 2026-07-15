// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

struct FVariantManagerContentStyle : public FSlateStyleSet
{
public:
	FVariantManagerContentStyle();

	static void Initialize();

	static void Shutdown();

	static const FName GetAppStyleSetName();

	static FVariantManagerContentStyle& Get()
	{
		static FVariantManagerContentStyle Inst;
		return Inst;
	}
};