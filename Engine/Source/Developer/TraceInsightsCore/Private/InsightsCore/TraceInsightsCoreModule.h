// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "InsightsCore/ITraceInsightsCoreModule.h"

class FTraceInsightsCoreModule : public ITraceInsightsCoreModule
{
public:
	virtual ~FTraceInsightsCoreModule() = default;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
