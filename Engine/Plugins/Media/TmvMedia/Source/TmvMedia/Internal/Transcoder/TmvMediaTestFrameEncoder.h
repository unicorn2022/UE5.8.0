// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Transcoder/TmvMediaFrameEncoder.h"
#include "TmvMediaTestFrameEncoder.generated.h"

/**
 * Implementation of an encoder stage that writes to files directly.
 * Bypass muxer stage.
 * For testing only.
 */
UCLASS(MinimalAPI)
class UTmvMediaTestFrameEncoder : public UTmvMediaFrameEncoder
{
	GENERATED_BODY()
public:
	//~ Begin UTmvMediaTranscodeStage
	virtual bool Start(UTmvMediaTranscodeJob* InParentJob) override;
	//~ End UTmvMediaTranscodeStage

	//~ Begin UTmvMediaFrameEncoder
	virtual void ReceiveMips(UTmvMediaTranscodeJob* InParentJob, TUniquePtr<FTmvMediaFrameMips>&& InMips) override;
	//~ End UTmvMediaFrameEncoder
};