// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "HAL/IConsoleManager.h"

MOVIESCENEMIXEDCONTROLRIG_API DECLARE_LOG_CATEGORY_EXTERN(LogMovieSceneMixedControlRig, Log, All);

namespace UE::MovieScene
{
class FMovieSceneMixedControlRigModule : public IModuleInterface
{
	// IModuleInterface interface
	MOVIESCENEMIXEDCONTROLRIG_API virtual void StartupModule() override;
	MOVIESCENEMIXEDCONTROLRIG_API virtual void ShutdownModule() override;
};

}
