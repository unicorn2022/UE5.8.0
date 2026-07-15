// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/FunctionFwd.h"

struct ID3D12CommandQueue;

class IElectraPlayerResourceDelegate
{
public:
	virtual ~IElectraPlayerResourceDelegate() = default;

	virtual void ExecuteCodeWithCopyCommandQueueUsage(TFunction<void(ID3D12CommandQueue*)>&& CodeToRun) = 0;
};
