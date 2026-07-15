// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UEdGraphSchema;

/** Interface to the creation of an anim BP compiler context */
class IAnimBlueprintCompilerCreationContext
{
public:
	virtual ~IAnimBlueprintCompilerCreationContext() = default;

	/** Registers a graphs schema class to the anim BP compiler so that default function processing is not performed on it */
	virtual void RegisterKnownGraphSchema(TSubclassOf<UEdGraphSchema> InGraphSchemaClass) = 0;
};

