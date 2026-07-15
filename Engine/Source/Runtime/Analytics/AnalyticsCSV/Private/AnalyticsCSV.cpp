// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnalyticsCSV.h"
#include "AnalyticsProviderCSV.h"

IMPLEMENT_MODULE( FAnalyticsCSV, AnalyticsCSV );

void FAnalyticsCSV::StartupModule()
{
}

void FAnalyticsCSV::ShutdownModule()
{
}

TSharedPtr<IAnalyticsProvider> FAnalyticsCSV::CreateAnalyticsProvider(const FAnalyticsProviderConfigurationDelegate& GetConfigValue) const
{
	if (GetConfigValue.IsBound())
	{
		return MakeShared<FAnalyticsProviderCSV>(GetConfigValue);
	}

	return nullptr;
}

