// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

class FMediaProfileEditorStyle : public FSlateStyleSet
{
public:
	static FMediaProfileEditorStyle& Get();
	
	void Register();
	void Unregister();
	
private:
	FMediaProfileEditorStyle();
};
