// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioModulationDebugDataProvider.h"

#if WITH_AUDIOMODULATION
#if !UE_BUILD_SHIPPING

void AudioModulation::IDebugDataProvider::GetControlBusDebugInfo(TArray<FControlBusDebugInfo>& OutDebugInfo) {}
void AudioModulation::IDebugDataProvider::GetControlBusMixDebugInfo(TArray<FControlBusMixDebugInfo>& OutDebugInfo) {}

#endif // !UE_BUILD_SHIPPING
#endif // WITH_AUDIOMODULATION

