// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Templates/SharedPointer.h"

class FUICommandInfo;

class FPropertyEditorCommands : public TCommands<FPropertyEditorCommands>
{
public:
	FPropertyEditorCommands();

	//~ Begin TCommands
	virtual void RegisterCommands() override;
	//~ End TCommands

	TSharedPtr<FUICommandInfo> CollapseAll;
	TSharedPtr<FUICommandInfo> ExpandAll;
};