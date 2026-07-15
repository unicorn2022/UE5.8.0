// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Tools/InteractiveToolsCommands.h"

class FMetaHumanCharacterPaletteEditorCommands : public TCommands<FMetaHumanCharacterPaletteEditorCommands>
{
public:
	FMetaHumanCharacterPaletteEditorCommands();

	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> Apply;
	TSharedPtr<FUICommandInfo> AutoApplyInstanceEdits;
	TSharedPtr<FUICommandInfo> RebuildProduction;
	TSharedPtr<FUICommandInfo> AutoBuildPreview;
	TSharedPtr<FUICommandInfo> BuildAllItemsOnEdit;
	TSharedPtr<FUICommandInfo> ClearBuildCache;
};

