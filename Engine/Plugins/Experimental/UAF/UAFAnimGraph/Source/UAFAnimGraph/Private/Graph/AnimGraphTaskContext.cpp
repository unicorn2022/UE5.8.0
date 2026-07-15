// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimGraphTaskContext.h"
#include "Graph/AnimNextGraphInstance.h"

namespace UE::UAF
{

FAnimNextGraphInstance& FAnimGraphTaskContext::GetAnimGraphInstance() const
{
	return static_cast<FAnimNextGraphInstance&>(Instance);
}

FAnimGraphTaskContext::FAnimGraphTaskContext(FAnimNextGraphInstance& InGraphInstance)
	: FInstanceTaskContext(InGraphInstance)
{
}

}