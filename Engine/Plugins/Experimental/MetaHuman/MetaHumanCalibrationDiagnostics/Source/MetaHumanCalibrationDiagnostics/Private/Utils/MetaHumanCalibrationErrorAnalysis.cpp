// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationErrorAnalysis.h"

namespace UE::MetaHuman::Private
{
struct FCalibrationScoringSettings
{
	// Error thresholds as % of diagonal (normalized units)
	// "Good" targets (<=) and "Bad" caps (>=) for linear score mapping
	double MedianGood;
	double MedianBad;
	double P90Good;
	double P90Bad;
	double RMSGood;
	double RMSBad;

	double WeightMedian = 0.4; // Most value goes to median
	double WeightRMS = 0.3;
	double WeightPointCount = 0.2;
	double WeightP90 = 0.1;

	// Coverage
	int32 MinFeaturesPerBlock = 10;
	int32 MaxFeaturesPerFrame = 550; // It depends on which Matcher we are using (SIFT = 550, AKAZE = 800)

	double WeightError = 0.8;
	double WeightCoverage = 0.2;
};

static inline double CalculatePercentageFromValueOfDiag(double InDiag, double InValue)
{
	return InValue / InDiag;
}

static inline double MapToInterval(double InValue, double InGood, double InBad)
{
	double Score = (InValue - InGood) / (InBad - InGood);
	Score = FMath::Clamp(Score, 0.0, 1.0);

	return 1.0 - Score; // 1 at Good, 0 at Bad
}

static FCalibrationScoringSettings CreateNormalizedSettings(double InDiag)
{
	FCalibrationScoringSettings Settings;
	Settings.MedianGood = CalculatePercentageFromValueOfDiag(InDiag, 1.0);
	Settings.MedianBad = CalculatePercentageFromValueOfDiag(InDiag, 3.5);
	Settings.P90Good = CalculatePercentageFromValueOfDiag(InDiag, 1.4);
	Settings.P90Bad = CalculatePercentageFromValueOfDiag(InDiag, 4.0);
	Settings.RMSGood = CalculatePercentageFromValueOfDiag(InDiag, 1.2);
	Settings.RMSBad = CalculatePercentageFromValueOfDiag(InDiag, 3.7);

	return Settings;
}
}

FMetaHumanCalibrationErrorAnalysis::FMetaHumanCalibrationErrorAnalysis(const FMetaHumanCalibrationErrorCalculator& InCalculator,
																	   const TArray<int32>& InFrames)
	: Calculator(InCalculator)
	, Frames(InFrames)
{
	Frames.Sort();

	check(Calculator.ContainsErrors());
}

TMap<FString, FCameraCalibrationScore> FMetaHumanCalibrationErrorAnalysis::Analyze() const
{
	using namespace UE::MetaHuman::Private;

	TArray<FString> CameraNames = Calculator.GetCameraNames();
	TArray<FBox2D> AreaOfInterest = Calculator.GetAreaOfInterest();
	TArray<FIntVector2> ImageSizes = Calculator.GetImageSizes();

	TMap<FString, FCameraCalibrationScore> Score;
	
	for (int32 CameraIndex = 0; CameraIndex < CameraNames.Num(); ++CameraIndex)
	{
		const FString& CameraName = CameraNames[CameraIndex];

		double ImageDiagonal = FVector2D(ImageSizes[CameraIndex]).Length();

		FCalibrationScoringSettings ScoringSettings = CreateNormalizedSettings(ImageDiagonal);

		// Normalizes by area
		double ImageArea = static_cast<double>(ImageSizes[CameraIndex].X) * ImageSizes[CameraIndex].Y;
		double AOIArea = AreaOfInterest[CameraIndex].GetArea(); 
		ScoringSettings.MaxFeaturesPerFrame = (ScoringSettings.MaxFeaturesPerFrame * AOIArea) / ImageArea;

		FCameraCalibrationScore CameraScore;
		double MeanTotalScore = 0.0;
		double MeanErrorScore = 0.0;
		double MeanCoverageScore = 0.0;

		TArray<int32> BlockIndices = Calculator.GetBlockIndices(CameraName);
		checkf(!BlockIndices.IsEmpty(), TEXT("Number of blocks can't be 0"));

		for (int32 FrameIndex : Frames)
		{
			FMetaHumanCalibrationErrorCalculator::FErrors FrameErrors =
				Calculator.GetErrorsForFrame(CameraName, FrameIndex);

			const double NormalizedRMS = CalculatePercentageFromValueOfDiag(ImageDiagonal, FrameErrors.RMSError);
			const double NormalizedMedian = CalculatePercentageFromValueOfDiag(ImageDiagonal, FrameErrors.MedianError);
			const double NormalizedP90 = CalculatePercentageFromValueOfDiag(ImageDiagonal, FrameErrors.P90Error);

			const double RMSScore = MapToInterval(NormalizedRMS, ScoringSettings.RMSGood, ScoringSettings.RMSBad);
			const double MedianScore = MapToInterval(NormalizedMedian, ScoringSettings.MedianGood, ScoringSettings.MedianBad);
			const double P90Score = MapToInterval(NormalizedP90, ScoringSettings.P90Good, ScoringSettings.P90Bad);
			// More points == better score
			const double PointCountScore = MapToInterval(FrameErrors.Errors.Num(), ScoringSettings.MaxFeaturesPerFrame, 0.0);

			const double TotalErrorScore = MedianScore * ScoringSettings.WeightMedian + 
				RMSScore * ScoringSettings.WeightRMS + 
				PointCountScore * ScoringSettings.WeightPointCount + 
				P90Score * ScoringSettings.WeightP90;

			MeanErrorScore += TotalErrorScore;

			FFrameCalibrationErrorScore ResultErrorScore
			{
				.TotalErrorScore = TotalErrorScore,
				.MedianScore = MedianScore,
				.RMSScore = RMSScore,
				.PointCountScore = PointCountScore,
				.P90Score = P90Score
			};

			CameraScore.ErrorScorePerFrame.Add(FrameIndex, MoveTemp(ResultErrorScore));

			int32 GoodBlocks = 0;
			for (int32 BlockIndex : BlockIndices)
			{
				FMetaHumanCalibrationErrorCalculator::FErrors BlockErrors =
					Calculator.GetErrorsForBlock(CameraName, BlockIndex, FrameIndex);

				if (BlockErrors.Errors.IsEmpty())
				{
					continue;
				}

				bool bHasEnoughPoints = BlockErrors.Errors.Num() >= ScoringSettings.MinFeaturesPerBlock;

				const double NormalizedBlockMedian = CalculatePercentageFromValueOfDiag(ImageDiagonal, BlockErrors.MedianError);

				// Normalized error is less than settings
				const double BlockMedianScore = MapToInterval(NormalizedBlockMedian, ScoringSettings.MedianGood, ScoringSettings.MedianBad);
				bool bHasGoodMedian = BlockMedianScore >= 0.8;

				if (bHasEnoughPoints && bHasGoodMedian)
				{
					++GoodBlocks;
				}
			}

			const double CoverageBlockScore = static_cast<double>(GoodBlocks) / BlockIndices.Num();
			CameraScore.CoverageScorePerFrame.Add(FrameIndex, CoverageBlockScore);
			MeanCoverageScore += CoverageBlockScore;

			const double TotalScore = TotalErrorScore * ScoringSettings.WeightError + 
				CoverageBlockScore * ScoringSettings.WeightCoverage;
			CameraScore.TotalScorePerFrame.Add(FrameIndex, TotalScore);
			MeanTotalScore += TotalScore;
		}

		CameraScore.MeanErrorScore = MeanErrorScore / Frames.Num();
		CameraScore.MeanCoverageScore = MeanCoverageScore / Frames.Num();
		CameraScore.MeanTotalScore = MeanTotalScore / Frames.Num();

		Score.Add(CameraName, MoveTemp(CameraScore));
	}

	check(Score.Num() == CameraNames.Num());

	return Score;
}
