// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"

namespace UE::Trace
{
	class FTraceWriter;
} // namespace UE::Trace

#define UE_API AUDIOINSIGHTS_API

namespace UE::Audio::Insights
{
	class FAudioInsightsCacheManager;

	class FAudioInsightsCacheTraceWriter
	{
	public:
		UE_API static bool WriteCacheToFile(const FAudioInsightsCacheManager& InCacheManager, const FString& InFilePath);
	};
} // namespace UE::Audio::Insights

#undef UE_API
