// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::MetaHuman::Pipeline
{

enum class EPinDirection
{
	Input = 0,
	Output
};

enum class EPinType // Cant replace with std::type_info, due to RTTI
{
	Int = 0,
	Float,
	Bool,
	String,
	UE_Image,     // 4 channel BGRA
	UE_GrayImage, // single channel
	HS_Image,
	Points,
	Contours,
	Scaling,
	Animation,
	Depth,
	TrackingConfidence,
	LiveLinkFrame,
	IntArray,
	AnimationArray,
	FlowOutput,
	DepthMapDiagnostics,
	Audio,
	QualifiedFrameTime,
	Calibration
};

class FPin
{
public:

	UE_API FPin(const FString& InName, EPinDirection InDirection, EPinType InType);
	UE_API FPin(const FString& InName, EPinDirection InDirection, EPinType InType, int32 InGroup);
	UE_API FPin(const FString& InName, EPinDirection InDirection, EPinType InType, int32 InGroup, bool bInIsPassthrough, bool bInIsOptional);

	FString Name;
	EPinDirection Direction;
	EPinType Type;
	int32 Group = 0;
	bool bIsPassthrough = true;
	bool bIsOptional = false;

	FString Address;

	UE_API FString ToString() const;
};

}

#undef UE_API
