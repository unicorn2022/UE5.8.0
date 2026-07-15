// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestGameModeBase.h"
#include "AutomatedPerfTesting.h"
#include "Logging/StructuredLog.h"

void AAutomatedPerfTestGameModeBase::SetupTest_Implementation()
{
	UE_LOGFMT(LogAutomatedPerfTest, Log, "AutomatedPerfTestGameModeBase SetupTest");
}

void AAutomatedPerfTestGameModeBase::TeardownTest_Implementation()
{
	UE_LOGFMT(LogAutomatedPerfTest, Log, "AutomatedPerfTestGameModeBase TeardownTest");
}

void AAutomatedPerfTestGameModeBase::RunTest_Implementation()
{
	UE_LOGFMT(LogAutomatedPerfTest, Log, "AutomatedPerfTestGameModeBase RunTest");
}

void AAutomatedPerfTestGameModeBase::Exit_Implementation()
{
	UE_LOGFMT(LogAutomatedPerfTest, Log, "AutomatedPerfTestGameModeBase Exit");
}