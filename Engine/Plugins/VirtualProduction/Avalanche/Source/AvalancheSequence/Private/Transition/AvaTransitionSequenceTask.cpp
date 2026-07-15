// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transition/AvaTransitionSequenceTask.h"
#include "AvaTransitionUtils.h"
#include "StateTreeExecutionContext.h"
#include "Transition/AvaTransitionSequenceUtils.h"

EAvaTransitionSequenceWaitType FAvaTransitionSequenceTask::GetWaitType(FStateTreeExecutionContext& InContext) const
{
	return InContext.GetInstanceData(*this).WaitType;
}
