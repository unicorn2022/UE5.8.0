// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ExtensionsSystem/ChaosVDExtension.h"

/**
 * Built-in CVD extension that registers the FChaosVDParticleExtraDataProcessor.
 *
 * UChaosVDParticleExtraDataComponent is registered directly as a default subobject
 * on AChaosVDSolverInfoActor, so this extension only handles processor registration.
 */
class FChaosVDParticleExtraDataExtension final : public FChaosVDExtension
{
public:
	FChaosVDParticleExtraDataExtension();
	virtual ~FChaosVDParticleExtraDataExtension() override = default;

	virtual void RegisterDataProcessorsInstancesForProvider(const TSharedRef<FChaosVDTraceProvider>& InTraceProvider) override;
};
