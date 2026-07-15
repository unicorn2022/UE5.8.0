// Copyright Epic Games, Inc. All Rights Reserved.

#include "PaletteEditor/MetaHumanCharacterPaletteEditorCommands.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

FMetaHumanCharacterPaletteEditorCommands::FMetaHumanCharacterPaletteEditorCommands()
	: TCommands<FMetaHumanCharacterPaletteEditorCommands>(
		TEXT("MetaHumanCharacterPaletteEditor"),
		LOCTEXT("ContextDescription", "MetaHuman Collection Editor"),
		NAME_None,
		FAppStyle::GetAppStyleSetName()
	)
{
}

void FMetaHumanCharacterPaletteEditorCommands::RegisterCommands()
{
	// These are part of the asset editor UI
	UI_COMMAND(Apply, "Apply", "Apply changes made in this editor to the asset", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(AutoApplyInstanceEdits, "Auto-Apply Edits", "Automatically apply changes to the asset after each edit", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(RebuildProduction, "Rebuild", "Builds the Collection for Production. You may need to do this if a source asset has changed since the last time this Collection was edited.", EUserInterfaceActionType::Button, FInputChord{});
	UI_COMMAND(AutoBuildPreview, "Auto-Build For Preview", "Builds the Collection for Preview whenever an edit is made. You may want to disable this while making multiple edits if the Preview build is slow.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(BuildAllItemsOnEdit, "Build All Items On Edit", "Builds the entire Collection for Preview when the Collection is edited. Once the build has finished, you can switch between items quickly. Without this setting, only the selected items will be built for Preview, so the build will be faster but it will have to be redone whenever you select a different item.", EUserInterfaceActionType::ToggleButton, FInputChord{});
	UI_COMMAND(ClearBuildCache, "Clear Build Cache", "Clears the build cache for this Collection, causing a full rebuild the next time the Collection is built.", EUserInterfaceActionType::Button, FInputChord{});
}

#undef LOCTEXT_NAMESPACE
