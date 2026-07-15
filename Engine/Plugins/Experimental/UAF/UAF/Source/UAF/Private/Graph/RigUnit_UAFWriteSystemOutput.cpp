// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RigUnit_UAFWriteSystemOutput.h"

#include "AnimNextStats.h"
#include "Graph/UAFSystemOutputComponent.h"
#include "Module/AnimNextModuleInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_UAFWriteSystemOutput)

FRigUnit_UAFWriteSystemOutput_Execute()
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Write_Pose);

	using namespace UE::UAF;

	const FAnimNextModuleContextData& ModuleContextData = ExecuteContext.GetContextData<FAnimNextModuleContextData>();
	FAnimNextModuleInstance& ModuleInstance = ModuleContextData.GetModuleInstance();
	FUAFSystemOutputComponent& OutputPoseComponent = ModuleInstance.GetOrAddComponent<FUAFSystemOutputComponent>();
	OutputPoseComponent.WriteOutput(Value);
}
