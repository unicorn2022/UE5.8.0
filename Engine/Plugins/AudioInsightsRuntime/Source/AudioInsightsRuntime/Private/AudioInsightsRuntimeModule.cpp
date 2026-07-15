// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioInsightsRuntimeModule.h"

#include "Modules/ModuleManager.h"

namespace UE::Audio::Insights
{
	void FAudioInsightsRuntimeModule::StartupModule()
	{
	}

	void FAudioInsightsRuntimeModule::ShutdownModule()
	{
	}

} // namespace UE::Audio::Insights

IMPLEMENT_MODULE(UE::Audio::Insights::FAudioInsightsRuntimeModule, AudioInsightsRuntime)
