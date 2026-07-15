// Copyright Epic Games, Inc. All Rights Reserved.

#include "MP4UtilitiesModule.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MP4UtilitiesModule"

DEFINE_LOG_CATEGORY(LogMP4Utilities);

class FMP4UtilitiesModule : public IModuleInterface
{
public:
	void StartupModule() override
	{
	}

	void ShutdownModule() override
	{
	}

	bool SupportsDynamicReloading() override
	{
		return false;
	}

private:
};

IMPLEMENT_MODULE(FMP4UtilitiesModule, MP4Utilities);

#undef LOCTEXT_NAMESPACE
