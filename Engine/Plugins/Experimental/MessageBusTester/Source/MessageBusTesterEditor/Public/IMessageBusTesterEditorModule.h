// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class IMessageBusTesterEditorModule : public IModuleInterface
{
public:
	virtual void DisplayMessageBusTester() = 0;
};

