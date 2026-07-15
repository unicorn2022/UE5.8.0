// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"

class UCameraCalibration;

namespace UE::MetaHuman
{

/**
 * Splits a calibration array into roughly-equal contiguous views.
 * The first (SourceArray.Num() % InNumberOfChunks) views get one extra element.
 * InNumberOfChunks is clamped to SourceArray.Num().
 */
TArray<TArrayView<UCameraCalibration* const>> SplitCalibrationArrayToViews(const TArray<UCameraCalibration*>& SourceArray, int32 InNumberOfChunks);

} // namespace UE::MetaHuman
