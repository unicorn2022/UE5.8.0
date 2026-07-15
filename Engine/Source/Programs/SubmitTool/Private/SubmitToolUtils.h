// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Widgets/SWindow.h"

class FString;

class FSubmitToolUtils
{
public:
	static void EnsureWindowIsInView(TSharedRef<SWindow> InWindow, bool bSingleWindow);
	static TSharedRef<SHorizontalBox> BuildUserPrefCheckboxUI(bool& InUserPrefOption, const FText&& InText);
private:
	static TMap<FString, TMap<bool, TSet<FString>>> HierarchyWildcardsCache;
};
