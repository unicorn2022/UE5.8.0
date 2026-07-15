// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_AnimNextSetNotifyContext.h"

#include "Components/SkeletalMeshComponent.h"
#include "Module/AnimNextModuleInstance.h"
#include "Module/UAFNotifyDispatcherComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextSetNotifyContext)

FRigUnit_AnimNextSetNotifyContext_Execute()
{
	FAnimNextModuleInstance& ModuleInstance = ExecuteContext.GetContextData<FAnimNextModuleContextData>().GetModuleInstance();
	FUAFNotifyDispatcherComponent& NotifyDispatcher = ModuleInstance.GetOrAddComponent<FUAFNotifyDispatcherComponent>();
	NotifyDispatcher.SkeletalMeshComponent = SkeletalMeshComponent;
	NotifyDispatcher.NotifyQueue.PredictedLODLevel = SkeletalMeshComponent ? SkeletalMeshComponent->GetPredictedLODLevel() : 0;
}
