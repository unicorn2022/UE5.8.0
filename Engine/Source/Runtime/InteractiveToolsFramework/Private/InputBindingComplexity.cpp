// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputBehaviorBindingComplexity.h"
#include "HAL/IConsoleManager.h"

namespace UE::ITF::Private
{

static int32 ITFWideBindingComplexityPriority = 1;
static FAutoConsoleVariableRef CVarITFWideBindingComplexityPriority(
	TEXT("ITF.UseITFWideBindingComplexityPriority"),
	ITFWideBindingComplexityPriority,
	TEXT("Enable or disable the use of binding complexity based behavior priority across the base ITF classes.")
);

bool UseInputBindingComplexity()
{
	return ITFWideBindingComplexityPriority > 0;
}

}

