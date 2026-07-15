// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ChunkAssignerCommandlet.cpp: used to update cook output with chunk assignment.
	Updates asset registry
	Updates shadercode libraries
	Updates loc settings.
=============================================================================*/

#pragma once
#include "Commandlets/Commandlet.h"
#include "ChunkAssignerCommandlet.generated.h"


UCLASS(config=Editor)
class UChunkAssignerCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:

	//~ Begin UCommandlet Interface

	virtual int32 Main(const FString& CmdLineParams) override;
	
	//~ End UCommandlet Interface
};


