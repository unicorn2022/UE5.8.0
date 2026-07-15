// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyle.h"

class FSlateStyleSet;
class ISlateStyle;

namespace UE::SandboxedEditing
{
class FSandboxedEditingStyle final : public FSlateStyleSet
{
public:
	
	static FSandboxedEditingStyle& Get();

	FSandboxedEditingStyle();
	virtual ~FSandboxedEditingStyle() override;
	
private:
	
	void RegisterBrowserStyle();
	void RegisterSharedStyle();
};
}

