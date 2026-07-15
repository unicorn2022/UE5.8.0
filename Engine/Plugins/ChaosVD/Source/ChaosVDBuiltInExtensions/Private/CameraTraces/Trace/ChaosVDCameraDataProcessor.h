// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced camera data
 */
class FChaosVDCameraDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FChaosVDCameraDataProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};

