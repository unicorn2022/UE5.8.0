// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "MetaHumanMinSpec.h"

class FMetaHumanPlatformModule
	: public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

void FMetaHumanPlatformModule::StartupModule()
{
	IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("mh.Core.CheckMinSpec"));

	if (ConsoleVariable)
	{
		ConsoleVariable->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			FMetaHumanMinSpec::Reset();
		}));
	}
}

void FMetaHumanPlatformModule::ShutdownModule()
{
	IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("mh.Core.CheckMinSpec"));

	if (ConsoleVariable)
	{
		ConsoleVariable->ClearOnChangedCallback();
	}
}

IMPLEMENT_MODULE(FMetaHumanPlatformModule, MetaHumanPlatform)
