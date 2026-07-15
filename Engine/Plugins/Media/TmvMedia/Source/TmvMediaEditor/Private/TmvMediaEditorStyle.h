// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

class FTmvMediaEditorStyle final : public FSlateStyleSet
{
public:
	static FTmvMediaEditorStyle& Get();

	FTmvMediaEditorStyle();

	virtual ~FTmvMediaEditorStyle() override;
};
