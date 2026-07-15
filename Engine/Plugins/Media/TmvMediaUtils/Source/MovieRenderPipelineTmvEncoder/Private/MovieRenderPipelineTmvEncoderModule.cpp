// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FMovieRenderPipelineTmvEncoderModule : public IModuleInterface
{
public:
	/*~ Begin IModuleInterface interface */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/*~ End IModuleInterface interface */
};

void FMovieRenderPipelineTmvEncoderModule::StartupModule()
{
}

void FMovieRenderPipelineTmvEncoderModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FMovieRenderPipelineTmvEncoderModule, MovieRenderPipelineTmvEncoder)