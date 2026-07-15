// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


class FSubsonicEngineTestModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FSubsonicEngineTestModule, SubsonicEngineTest)
