// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureManagerPipelineNode.h"

#include "CaptureDataConverterNodeParams.h"

#define UE_API CAPTUREDATACONVERTER_API

class FCaptureConvertParams
{
public:

	UE_API FCaptureConvertParams();
	UE_API virtual ~FCaptureConvertParams();

	UE_API void SetParams(const FCaptureConvertDataNodeParams& InParams);

protected:

	FCaptureConvertDataNodeParams Params;

};

#undef UE_API