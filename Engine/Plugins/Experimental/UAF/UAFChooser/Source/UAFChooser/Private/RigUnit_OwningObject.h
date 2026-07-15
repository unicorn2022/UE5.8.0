// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "ControlRigDefines.h"
#include "Units/RigUnit.h"

#include "RigUnit_OwningObject.generated.h"

// A node to retrieve the owning object of this graph
USTRUCT(meta = (DisplayName="Owning Object"))
struct FRigUnit_OwningObject : public FRigVMStruct
{
	GENERATED_BODY()

	// The object owning this graph
	UPROPERTY(meta = (Output))
	TObjectPtr<UObject> Result;

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute() override;
};