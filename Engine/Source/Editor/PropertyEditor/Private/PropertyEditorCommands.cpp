// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorCommands.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PropertyEditorCommands"

FPropertyEditorCommands::FPropertyEditorCommands()
	: TCommands<FPropertyEditorCommands>
	(
		TEXT("PropertyEditor"),
		LOCTEXT("PropertyEditorContext", "Property Editor"),
		NAME_None,
		FAppStyle::GetAppStyleSetName()
	)
{
}

void FPropertyEditorCommands::RegisterCommands()
{
	UI_COMMAND(ExpandAll, "Expand All", "Expand all elements in this details view", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::X));
	UI_COMMAND(CollapseAll, "Collapse All", "Collapse all elements in this details view", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::C));
}

#undef LOCTEXT_NAMESPACE
