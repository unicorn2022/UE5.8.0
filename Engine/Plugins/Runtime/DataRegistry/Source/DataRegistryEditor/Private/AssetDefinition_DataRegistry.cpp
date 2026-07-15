// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_DataRegistry.h"
#include "DataRegistryEditorToolkit.h"

EAssetCommandResult UAssetDefinition_DataRegistry::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UDataRegistry* DataRegistry : OpenArgs.LoadObjects<UDataRegistry>())
	{
		FDataRegistryEditorToolkit::CreateEditor(EToolkitMode::Standalone, OpenArgs.ToolkitHost, DataRegistry);
	}
	return EAssetCommandResult::Handled;
}
