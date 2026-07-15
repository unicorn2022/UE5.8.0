// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IMassCharacterTrajectoryModule.h"


class FMassCharacterTrajectoryModule : public IMassCharacterTrajectoryModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FMassCharacterTrajectoryModule, MassCharacterTrajectory)


void FMassCharacterTrajectoryModule::StartupModule()
{
}


void FMassCharacterTrajectoryModule::ShutdownModule()
{
}
