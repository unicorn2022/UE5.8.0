// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameOffsetCalculator.h"

#include "Algo/MaxElement.h"
#include "FrameNumberTransformer.h"
#include "SequencedImageTrackInfo.h"

namespace UE::MetaHuman::DepthGenerator
{

TArray<int32> CalculateFrameOffset(const TArray<FCameraTimecodeInfo>& InCameraTimecodeInfos)
{
	if (InCameraTimecodeInfos.IsEmpty() || InCameraTimecodeInfos.Num() < 2)
	{
		return TArray<int32>();
	}

	// Reference frame rate for the multi-camera system. Each camera's original
	// frame rate is converted to this value, allowing frame numbers to be aligned
	// and compared reliably.
	FFrameRate TargetFrameRate = 
		Algo::MaxElement(InCameraTimecodeInfos, 
						  [](const FCameraTimecodeInfo& InLeft, const FCameraTimecodeInfo& InRight)
						  {
							 return InLeft.FrameRate.AsDecimal() < InRight.FrameRate.AsDecimal();
						  })->FrameRate;

	TArray<int32> FrameNumbers;
	for (int32 CameraIndex = 0; CameraIndex < InCameraTimecodeInfos.Num(); ++CameraIndex)
	{
		const FCameraTimecodeInfo& TimecodeInfo = InCameraTimecodeInfos[CameraIndex];

		// If the frame rates are not compatible we have no way of determining the offset
		if (!FrameRatesAreCompatible(TargetFrameRate, TimecodeInfo.FrameRate))
		{
			return TArray<int32>();
		}

		FFrameNumberTransformer Transformer(TargetFrameRate, TimecodeInfo.FrameRate);
		FFrameNumber StartingFrameNumber = TimecodeInfo.Timecode.ToFrameNumber(TimecodeInfo.FrameRate);

		int32 FrameNumber = Transformer.Transform(StartingFrameNumber.Value);
		FrameNumbers.Add(FrameNumber);
	}

	int32 MaxElem = *Algo::MaxElement(FrameNumbers);

	for (int32 CameraIndex = 0; CameraIndex < InCameraTimecodeInfos.Num(); ++CameraIndex)
	{
		const FCameraTimecodeInfo& TimecodeInfo = InCameraTimecodeInfos[CameraIndex];

		int32& FrameNumber = FrameNumbers[CameraIndex];
		FrameNumber = MaxElem - FrameNumber;

		FFrameNumberTransformer Transformer(TimecodeInfo.FrameRate, TargetFrameRate);

		FrameNumber = Transformer.Transform(FrameNumber);
	}

	return FrameNumbers;
}

}