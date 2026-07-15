// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildWorkers.h"

#include "Containers/StringView.h"
#include "Misc/Optional.h"
#include "MultiPlatformTargetReceiptBuildWorkers.h"

struct FDerivedDataBuildWorkers
{
	FMultiPlatformTargetReceiptBuildWorkers BaseTextureBuildWorkerFactory{TEXTVIEW("$(EngineDir)/Binaries/$(Platform)/BaseTextureBuildWorker.target")};
	FMultiPlatformTargetReceiptBuildWorkers ShaderBuildWorkerFactory{TEXTVIEW("$(EngineDir)/Binaries/$(Platform)/ShaderBuildWorker.target")};
};

TOptional<FDerivedDataBuildWorkers> GBuildWorkers;

void InitDerivedDataBuildWorkers()
{
	if (!GBuildWorkers.IsSet())
	{
		GBuildWorkers.Emplace();
	}
}

void ShutdownDerivedDataBuildWorkers()
{
	GBuildWorkers.Reset();
}
