// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "NNERuntimeCoreMLUtilsLog.h"

DEFINE_LOG_CATEGORY(LogNNERuntimeCoreMLUtils);

class FNNERuntimeCoreMLUtilsModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FNNERuntimeCoreMLUtilsModule, NNERuntimeCoreMLUtils)