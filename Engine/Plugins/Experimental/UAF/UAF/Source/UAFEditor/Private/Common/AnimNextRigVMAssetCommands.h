// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

namespace UE::UAF
{

// Shared commands valid for any UAF RigVM Asset.
class FAnimNextRigVMAssetCommands : public TCommands<FAnimNextRigVMAssetCommands>
{
public:
	FAnimNextRigVMAssetCommands();

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> FindInAnimNextRigVMAsset;

	TSharedPtr<FUICommandInfo> FindAndReplaceInAnimNextRigVMAsset;

	TSharedPtr<FUICommandInfo> OpenUAFBrowser;
};

}