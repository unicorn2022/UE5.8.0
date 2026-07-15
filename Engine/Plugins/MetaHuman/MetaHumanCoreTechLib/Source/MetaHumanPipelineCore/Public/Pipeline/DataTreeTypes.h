// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "FrameTrackingContourData.h"
#include "FrameTrackingConfidenceData.h"
#include "FrameAnimationData.h"
#include "DepthMapDiagnosticsResult.h"
#include "Misc/QualifiedFrameTime.h"


namespace UE::MetaHuman::Pipeline
{

class FInvalidDataType
{
};

enum class EPipelineExitStatus
{
	Unknown = 0,
	OutOfScope,
	AlreadyRunning,
	NotInGameThread,
	InvalidNodeTypeName,
	InvalidNodeName,
	DuplicateNodeName,
	InvalidPinName,
	DuplicatePinName,
	InvalidConnection,
	AmbiguousConnection,
	Unconnected,
	LoopConnection,
	Ok,
	Aborted,
	StartError,
	ProcessError,
	EndError,
	TooFast,
	InsufficientThreadsForNodes
};

class FUEImageDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<uint8> Data; // bgra order, a=255 for fully opaque
};

class FUEGrayImageDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<uint8> Data;
};

class FHSImageDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<float> Data;
};

class FScalingDataType
{
public:

	float Factor = -1.0f;
};

class FDepthDataType
{
public:

	int32 Width = -1;
	int32 Height = -1;
	TArray<float> Data;
};

class FFlowOutputDataType
{
public:

	TArray<float> Flow;
	TArray<float> Confidence;
	TArray<float> SourceCamera;
	TArray<float> TargetCamera;
};

class FAudioDataType
{
public:

	int32 NumChannels = -1;
	int32 SampleRate = -1;
	int32 NumSamples = -1;
	TArray<float> Data;
	bool bContiguous = true; // whether consecutive audio sample are contiguous
};

class FCalibrationDataType
{
public:

	enum EType
	{
		Video = 0,
		Depth,

		Unknown,
	};

	FString CameraId;
	EType CameraType = EType::Unknown;

	FVector2D ImageSize = FVector2D::Zero();
	FVector2D FocalLength = FVector2D::Zero();
	FVector2D PrincipalPoint = FVector2D::Zero();

	double K1 = 0;
	double K2 = 0;
	double P1 = 0;
	double P2 = 0;
	double K3 = 0;
	double K4 = 0;
	double K5 = 0;
	double K6 = 0;
	FMatrix Transform = FMatrix::Identity;
};

using FDataTreeType = TVariant<FInvalidDataType, EPipelineExitStatus, int32, float, double, bool, FString, FUEImageDataType, FUEGrayImageDataType, FHSImageDataType, FScalingDataType, FFrameTrackingContourData, FFrameAnimationData, FDepthDataType, FFrameTrackingConfidenceData, TArray<int32>, TArray<FFrameAnimationData>, FFlowOutputDataType, TMap<FString, FDepthMapDiagnosticsResult>, FAudioDataType, FQualifiedFrameTime, FCalibrationDataType>;

}
