// Copyright Epic Games, Inc. All Rights Reserved.

#include "Submodes/Submode.h"
#include "Toolkits/BaseToolkit.h"

using namespace UE::MeshTerrain;

// FSubmodeToolPalette

FSubmodeToolPalette::FSubmodeToolPalette(
	const FText& InPaletteLabel,
	const TArray<TSharedPtr<FUICommandInfo>>& InToolCommands)
: PaletteLabel(InPaletteLabel)
, ToolCommands(InToolCommands)
{
	
}

FSubmodeToolPalette::FSubmodeToolPalette(const FSubmodeToolPalette& Other)
: PaletteLabel(Other.PaletteLabel)
, ToolCommands(Other.ToolCommands)
{
	
}


// FSubmode

FSubmode::FSubmode(const TSharedPtr<FModeToolkit>& InToolkit)
	: Toolkit(InToolkit)
{
	
}


TSharedPtr<FUICommandInfo> FSubmode::GetEnterSubmodeAction() const
{
	return EnterSubmodeAction;
}

const TArray<FSubmodeToolPalette>& FSubmode::GetToolPalettes() const
{
	return ToolPalettes;
}

void FSubmode::AddToolPalette(const FSubmodeToolPalette& Palette)
{
	ToolPalettes.Add(Palette);
}
