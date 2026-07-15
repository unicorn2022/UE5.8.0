// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SSKMQuickAccessMenu.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"

void SSkeletalMeshModelingToolsQuickAccessMenu::Construct(const FArguments& InArgs)
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/true, InArgs._CommandList);
	InArgs._OnPopulateMenu.ExecuteIfBound(MenuBuilder);

	ChildSlot
	[
		MenuBuilder.MakeWidget()
	];
}
