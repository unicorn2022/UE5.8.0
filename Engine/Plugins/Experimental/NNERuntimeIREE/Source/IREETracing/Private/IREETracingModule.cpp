// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FIREETracingModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }
};

IMPLEMENT_MODULE(FIREETracingModule, IREETracing)