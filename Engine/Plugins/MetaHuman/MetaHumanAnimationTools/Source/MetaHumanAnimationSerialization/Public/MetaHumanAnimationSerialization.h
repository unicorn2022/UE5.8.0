// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/Archive.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"

#define UE_API METAHUMANANIMATIONSERIALIZATION_API



class FMetaHumanAnimationSerialization
{

public:

	enum class EMaxPrecisionType : uint8
	{
		Undefined = 0,
		Float,
		Int16,
		Int10,
	};

	enum class ECompressionMethod : uint8
	{
		Undefined = 0, 
		None,
		Sparse,
	};

	UE_API bool SetupEncoder(FArchive& InOutArchive, EMaxPrecisionType InMaxPrecisionType, ECompressionMethod InCompressionMethod);
	UE_API bool Encode(FArchive& InOutArchive, float InTime, const TArray<float>& InCurves);

	UE_API bool SetupDecoder(FArchive& InOutArchive);
	UE_API bool Decode(FArchive& InOutArchive, float& OutTime, TArray<float>& OutCurves);

	UE_API EMaxPrecisionType GetMaxPrecisionType() const;
	UE_API ECompressionMethod GetCompressionMethod() const;

private:

	EMaxPrecisionType MaxPrecisionType = EMaxPrecisionType::Undefined;
	ECompressionMethod CompressionMethod = ECompressionMethod::Undefined;

	TArray<float> PreviousCurves;

	float SparsePrecision = 0.001f;
	float SparseFullDataInterval = 1;
	float SparseLastFullDataTime = -1;

	bool EncodeMaxPrecisionValues(FArchive& InOutArchive, const TArray<float>& InValues);
	bool DecodeMaxPrecisionValues(FArchive& InOutArchive, TArray<float>& OutValues);

	static const FString MagicNumber;
};

#undef UE_API
