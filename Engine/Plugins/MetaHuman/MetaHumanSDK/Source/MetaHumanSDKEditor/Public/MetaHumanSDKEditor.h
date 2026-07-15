// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Logging/LogMacros.h"
DECLARE_LOG_CATEGORY_EXTERN(LogMetaHumanSDK, Log, All);

struct FAnalyticsEventAttribute;
namespace UE::MetaHuman
{
extern METAHUMANSDKEDITOR_API const FName MessageLogName;

void AnalyticsEvent(FString&& EventName, const TArray<FAnalyticsEventAttribute>& Attributes = {});
}
