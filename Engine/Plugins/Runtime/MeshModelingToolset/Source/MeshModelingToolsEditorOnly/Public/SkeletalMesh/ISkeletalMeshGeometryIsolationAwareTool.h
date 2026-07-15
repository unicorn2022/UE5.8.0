// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "ISkeletalMeshGeometryIsolationAwareTool.generated.h"


UINTERFACE(MinimalAPI)
class USkeletalMeshGeometryIsolationAwareTool :
	public UInterface
{
	GENERATED_BODY()
};


class ISkeletalMeshGeometryIsolationAwareTool : public IInterface
{
	GENERATED_BODY()

public:
	virtual bool IsInputIsolationValidOnOutput() const = 0;
};
