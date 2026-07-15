// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_WaveTableBank.h"
#include "WaveTableBankEditor.h"

EAssetCommandResult UAssetDefinition_WaveTableBank::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace WaveTable::Editor;
	const EToolkitMode::Type Mode = OpenArgs.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UWaveTableBank* Patch : OpenArgs.LoadObjects<UWaveTableBank>())
	{
		TSharedRef<FBankEditor> Editor = MakeShared<FBankEditor>();
		Editor->Init(Mode, OpenArgs.ToolkitHost, Patch);
	}

	return EAssetCommandResult::Handled;
}
