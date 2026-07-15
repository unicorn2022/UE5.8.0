// Copyright Epic Games, Inc. All Rights Reserved.

#include "AccumulationDOFEditorCommands.h"

#define LOCTEXT_NAMESPACE "AccumulationDOFEditor"

void FAccumulationDOFEditorCommands::RegisterCommands()
{
	UI_COMMAND(
		ToggleAccumulate,
		"Toggle Accumulate",
		"Toggle accumulation depth of field preview in the active viewport",
		EUserInterfaceActionType::ToggleButton,
		FInputChord(EModifierKey::Alt, EKeys::D)
	);
}

#undef LOCTEXT_NAMESPACE
