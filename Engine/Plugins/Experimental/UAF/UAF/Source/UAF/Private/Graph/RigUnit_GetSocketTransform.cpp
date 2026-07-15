// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetSocketTransform.h"
#include "UAFLogging.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_GetSocketTransform)

FRigUnit_GetSocketTransform_Execute()
{
	if (SceneComponent)
	{
		Result = SceneComponent->GetSocketTransform(SocketName, TransformSpace);
	}
	else
	{
		UAF_RIGUNIT_LOG(Warning, TEXT("Could not get transform for socket [%s] - SceneComponent is not valid."), *SocketName.ToString());
	}
}
