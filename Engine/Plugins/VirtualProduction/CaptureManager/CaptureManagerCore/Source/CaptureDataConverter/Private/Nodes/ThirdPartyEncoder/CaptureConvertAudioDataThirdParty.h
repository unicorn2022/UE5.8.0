// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/ConvertAudioNode.h"

#include "CaptureManagerEncoderCommands.h"

#include "CaptureDataConverterNodeParams.h"

class FCaptureConvertAudioDataThirdParty final :
	public FConvertAudioNode
{
public:

	FCaptureConvertAudioDataThirdParty(UE::CaptureManager::FAudioEncoderCommand InThirdPartyCommand,
									   const FTakeMetadata::FAudio& InAudio,
									   const FString& InOutputDirectory,
									   const FCaptureConvertDataNodeParams& InParams,
									   const FCaptureConvertAudioOutputParams& InAudioParams
	);

private:

	virtual FResult Run() override;

	FResult CopyAudioFile();
	FResult ConvertAudioFile();

	UE::CaptureManager::FAudioEncoderCommand ThirdPartyCommand;
	FCaptureConvertDataNodeParams Params;
	FCaptureConvertAudioOutputParams AudioParams;
};