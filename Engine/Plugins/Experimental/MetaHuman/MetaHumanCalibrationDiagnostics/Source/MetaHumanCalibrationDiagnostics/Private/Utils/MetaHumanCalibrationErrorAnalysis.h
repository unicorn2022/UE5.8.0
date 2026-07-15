// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCalibrationErrorCalculator.h"

struct FFrameCalibrationErrorScore
{
	/* Stores the score from error analysis for the current frame */
	double TotalErrorScore = 0.0;

	/* Stores the median score from error analysis for the current frame */
	double MedianScore = 0.0;
	
	/* Stores the RMS score from error analysis for the current frame */
	double RMSScore = 0.0;

	/* Stores the point count score from error analysis for the current frame */
	double PointCountScore = 0.0;

	/* Stores the P90 score from error analysis for the current frame */
	double P90Score = 0.0;
};

struct FCameraCalibrationScore
{
	/* Stores the mean score for the current camera across all selected frames */
	double MeanTotalScore = 0.0;

	/* Stores the mean score from error analysis for the current camera across all selected frames */
	double MeanErrorScore = 0.0;

	/* Stores the mean score from coverage analysis for the current camera across all selected frames */
	double MeanCoverageScore = 0.0;

	/* Stores the score from error analysis for the current camera per frame */
	TMap<int32, FFrameCalibrationErrorScore> ErrorScorePerFrame;

	/* Stores the score from coverage analysis for the current camera per frame */
	TMap<int32, double> CoverageScorePerFrame;

	/* Stores the total score for the current camera per frame */
	TMap<int32, double> TotalScorePerFrame;
};

class FMetaHumanCalibrationErrorAnalysis
{
public:

	static constexpr double MinRMS = 1.0; // Sensible default

	FMetaHumanCalibrationErrorAnalysis(const FMetaHumanCalibrationErrorCalculator& InCalculator,
									   const TArray<int32>& InFrames);

	TMap<FString, FCameraCalibrationScore> Analyze() const;

private:

	const FMetaHumanCalibrationErrorCalculator& Calculator;
	TArray<int32> Frames;
};