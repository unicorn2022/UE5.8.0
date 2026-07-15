// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetaHumanMeshData.h"

#include "FrameAnimationData.generated.h"

#define UE_API METAHUMANCORETECH_API

UENUM()
enum class EFrameAnimationQuality : uint8
{
	Undefined,
	Preview,
	Final,
	PostFiltered,
	Custom1,
	Custom2,
};

UENUM()
enum class EAudioProcessingMode : uint8
{
	Undefined,
	FullFace,
	TongueTracking,
	MouthOnly
};

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EFrameAnimationDataType : uint8
{
	Face = 1 << 0,
	Body = 1 << 1,

	Any = Face | Body,
};

ENUM_CLASS_FLAGS(EFrameAnimationDataType)

USTRUCT(BlueprintType)
struct FFrameAnimationData
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Pose = FTransform(FQuat(ForceInitToZero), FVector(ForceInitToZero), FVector(ForceInitToZero));

	UPROPERTY(Transient)
	TArray<float> RawPoseData;

	UPROPERTY(BlueprintReadOnly, Category = "AnimationData")
	TMap<FString, float> AnimationData;

	UPROPERTY(Transient)
	TMap<FString, float> RawAnimationData;

	UPROPERTY(BlueprintReadOnly, Category = "AnimationData")
	TMap<FString, FTransform> BodyAnimationData;

	UPROPERTY(BlueprintReadOnly, Category = "AnimationData")
	TArray<float> RawBodyAnimationSMPLXShape;

	UPROPERTY(BlueprintReadOnly, Category = "AnimationData")
	TArray<float> RawBodyAnimationSMPLXPose;

	UPROPERTY(BlueprintReadOnly, Category = "AnimationData")
	FVector RawBodyAnimationSMPLXTranslation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AnimationData")
	TMap<FString, FTransform> RawBodyAnimationSMPLXData;

	UPROPERTY(Transient)
	FMetaHumanMeshData MeshData;

	UPROPERTY()
	EFrameAnimationQuality AnimationQuality = EFrameAnimationQuality::Undefined;

	UPROPERTY()
	EAudioProcessingMode AudioProcessingMode = EAudioProcessingMode::Undefined;

	UE_DEPRECATED(5.8, "ContainsData() is deprecated. Use new ContainsData function which takes the data type as a parameter")
	UE_API bool ContainsData() const;

	UE_API bool ContainsData(EFrameAnimationDataType InDataType) const;

	friend FArchive& operator<<(FArchive& Ar, FFrameAnimationData& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	UE_API void Serialize(FArchive& Ar);
};

#undef UE_API
