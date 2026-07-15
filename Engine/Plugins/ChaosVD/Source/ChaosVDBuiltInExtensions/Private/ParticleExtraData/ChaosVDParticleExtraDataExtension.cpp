// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParticleExtraData/ChaosVDParticleExtraDataExtension.h"

#include "Trace/DataProcessors/ChaosVDParticleExtraDataProcessor.h"
#include "Trace/ChaosVDTraceProvider.h"

FChaosVDParticleExtraDataExtension::FChaosVDParticleExtraDataExtension()
{
	ExtensionName = FName(TEXT("FChaosVDParticleExtraDataExtension"));
}

void FChaosVDParticleExtraDataExtension::RegisterDataProcessorsInstancesForProvider(const TSharedRef<FChaosVDTraceProvider>& InTraceProvider)
{
	FChaosVDExtension::RegisterDataProcessorsInstancesForProvider(InTraceProvider);

	TSharedPtr<FChaosVDParticleExtraDataProcessor> Processor = MakeShared<FChaosVDParticleExtraDataProcessor>();
	Processor->SetTraceProvider(InTraceProvider);
	InTraceProvider->RegisterDataProcessor(Processor);
}
