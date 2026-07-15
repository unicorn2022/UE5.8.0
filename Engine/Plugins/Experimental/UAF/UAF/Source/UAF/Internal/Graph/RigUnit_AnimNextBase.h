// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMStruct.h"
#include "AnimNextExecuteContext.h"
#include "RigUnit_AnimNextBase.generated.h"

/*
 * base class for all UAF rigvm nodes
 */
USTRUCT(meta=(ExecuteContext="FAnimNextExecuteContext", DocumentationPolicy = "Strict"))
struct FRigUnit_AnimNextBase : public FRigVMStruct
{
	GENERATED_BODY()
};
