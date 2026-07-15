// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FContentBrowserAssetDataSourceCommands : public TCommands<FContentBrowserAssetDataSourceCommands>
{
public:
	FContentBrowserAssetDataSourceCommands();

	//~ Begin TCommands
	virtual void RegisterCommands() override;
	//~ End TCommands

	TSharedPtr<FUICommandInfo> CaptureThumbnail;
};
