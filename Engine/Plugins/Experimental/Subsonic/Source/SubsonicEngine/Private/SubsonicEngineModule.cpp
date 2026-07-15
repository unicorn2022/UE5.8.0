// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

namespace UE::Subsonic
{
	class FSubsonicEngineModule : public IModuleInterface
	{
	public:
		virtual void StartupModule() override
		{
		}

		virtual void ShutdownModule() override
		{
		}
	};
} // namespace UE::Subsonic

IMPLEMENT_MODULE(UE::Subsonic::FSubsonicEngineModule, SubsonicEngine)
