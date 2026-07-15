// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_GetMotionMatchInteractionConstraint.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_GetMotionMatchInteractionConstraint)

FRigUnit_GetMotionMatchInteractionConstraint_Execute()
{
	UPoseSearchInteractionLibrary::GetMotionMatchInteractionConstraint(ExecuteContext.GetOwningObject(), SocketName, DesiredReach, Transform, IsValid, bCompareOwningActors);
}
