// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsPlatformFeatures.h"

class FMSGameStorePlatformFeaturesModule : public FWindowsPlatformFeaturesModule
{
public:
#if WITH_GRDK
	virtual FString GetUniqueAppId() override;
	virtual void SetScreenshotEnableState(bool bEnabled) override;
#endif
};

