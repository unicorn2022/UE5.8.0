// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPCGMegaMeshInteropEditor, Log, All);

class FPCGMegaMeshInteropEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterPinColors();
	void UnregisterPinColors();

	void RegisterDataVisualizations();
	void UnregisterDataVisualizations();
};