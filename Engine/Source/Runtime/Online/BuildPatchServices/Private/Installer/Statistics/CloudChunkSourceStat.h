// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Installer/CloudChunkSource.h"

namespace BuildPatchServices
{
	class FStatsCollector;
	/**
	 * A factory for creating an ICloudChunkSourceStat instance.
	 */
	class FCloudChunkSourceStatFactory
	{
	public:
		/**
		 * Creates the cloud source stat dependency interface and exposes wraps stats collector.
		 * @param InStatsCollector    The stats collector which will be used to collect statistics on cloud source events.
		 * @return the new ICloudChunkSourceStat instance created.
		 */
		static ICloudChunkSourceStat* Create(FStatsCollector* InStatsCollector);
	};
}