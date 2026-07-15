// Copyright Epic Games, Inc. All Rights Reserved.

#include "CalibrationArraySplitter.h"

#include "CameraCalibration.h"
#include "Math/UnrealMathUtility.h"

namespace UE::MetaHuman
{

TArray<TArrayView<UCameraCalibration* const>> SplitCalibrationArrayToViews(const TArray<UCameraCalibration*>& SourceArray, int32 InNumberOfChunks)
{
	TArray<TArrayView<UCameraCalibration* const>> Views;
	int32 TotalNum = SourceArray.Num();
	if (SourceArray.IsEmpty())
	{
		return Views;
	}

	int32 ActualNumberOfChunks = FMath::Min(InNumberOfChunks, TotalNum);

	int32 StandardBatchSize = TotalNum / ActualNumberOfChunks;
	int32 Remainder = TotalNum % ActualNumberOfChunks;
	int32 StartIndex = 0;

	for (int32 Index = 0; Index < ActualNumberOfChunks; ++Index)
	{
		int32 SizeForThisBatch = StandardBatchSize + (Index < Remainder ? 1 : 0);

		Views.Add(TArrayView<UCameraCalibration* const>(SourceArray.GetData() + StartIndex, SizeForThisBatch));
		StartIndex += SizeForThisBatch;
	}

	return Views;
}

} // namespace UE::MetaHuman
