// Copyright Epic Games, Inc. All Rights Reserved.

#include "Apple/AppleHttpManager.h"

#include "Apple/AppleEventLoopHttpThread.h"
#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CommandLine.h"
#include "HttpModule.h"
#include "Http.h"

FHttpThreadBase* FAppleHttpManager::CreateHttpThread()
{
	if (bUseEventLoop)
	{
		UE_LOGF(LogHttp, Log, "CreateHttpThread using FAppleEventLoopHttpThread");
		return new FAppleEventLoopHttpThread();
	}

	UE_LOGF(LogHttp, Log, "CreateHttpThread using FLegacyHttpThread");
	return new FLegacyHttpThread();
}
