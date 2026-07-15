// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FRigMapperGraphCommands : public TCommands<FRigMapperGraphCommands>
{
public:
	FRigMapperGraphCommands()
		: TCommands<FRigMapperGraphCommands>(
			TEXT("RigMapperGraph"),
			NSLOCTEXT("RigMapperDefinitionGraphCommands", "RigMapperDefinitionGraphCommands", "Rig Mapper Graph commands"),
			NAME_None,
			FAppStyle::GetAppStyleSetName()) {
	}

	TSharedPtr<FUICommandInfo> FocusConnectedNodes;
	TSharedPtr<FUICommandInfo> CreateComment;

	virtual void RegisterCommands() override;
};