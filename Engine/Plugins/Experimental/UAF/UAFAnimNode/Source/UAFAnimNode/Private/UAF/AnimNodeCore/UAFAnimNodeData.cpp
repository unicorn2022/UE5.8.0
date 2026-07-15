// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAF/AnimNodeCore/UAFAnimNodeData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFAnimNodeData)

namespace UE::UAF
{
	FUAFAnimNodePtr FUAFAnimNodeData::CreateInstance(FUAFAnimGraphUpdateContext& Context) const
	{
		ensureMsgf(false, TEXT("AnimNode CreateInstance Unimplemented!!"));
		return nullptr;
	}
}
