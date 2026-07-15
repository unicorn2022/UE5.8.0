// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"

#include "MetaHumanPostAssemblyParameterOutput.generated.h"

// TODO: Consider renaming to FMetaHumanPostAssemblyParameterState?
USTRUCT()
struct FMetaHumanPostAssemblyParameterOutput
{
	GENERATED_BODY()
	
	/**
	 * Each supported Instance Parameter should have a property set to the default value of the 
	 * parameter in this property bag.
	 */
	UPROPERTY()
	FInstancedPropertyBag Parameters;

	/**
	 * A context struct that has meaning only to the pipeline instance that produced it.
	 * 
	 * Other code should not try to parse this struct.
	 */
	UPROPERTY()
	FInstancedStruct ParameterContext;

	/**
	 * Used to locate this Item's Assembly Output in an array on the Collection Assembly Output
	 * when a slot specifies ArrayProperty as its EMetaHumanAssemblyOutputMappingMethod.
	 */
	UPROPERTY()
	int32 PipelineAssemblyOutputArrayIndex = INDEX_NONE;
};
