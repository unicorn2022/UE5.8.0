// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/InstancedStruct.h"

#include "MetaHumanStructHost.generated.h"

/**
 * A simple wrapper to allow an FInstancedStruct to be visible to the Garbage Collector.
 * 
 * Useful for subclasses of UMetaHumanCharacterEditorPipeline, which often hold local objects that 
 * need to survive GC.
 */
UCLASS()
class UMetaHumanStructHost : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FInstancedStruct Struct;
};
