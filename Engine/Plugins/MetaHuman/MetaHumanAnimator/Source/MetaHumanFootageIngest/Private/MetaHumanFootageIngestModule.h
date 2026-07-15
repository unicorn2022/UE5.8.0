// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FMetaHumanFootageIngestModule
	: public IModuleInterface
{
public:

	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	class FCaptureManager* CaptureManager = nullptr;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};
