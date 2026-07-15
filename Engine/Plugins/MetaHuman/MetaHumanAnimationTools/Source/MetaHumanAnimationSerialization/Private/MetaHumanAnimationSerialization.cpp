// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAnimationSerialization.h"
#include "Containers/BitArray.h"



const FString FMetaHumanAnimationSerialization::MagicNumber = TEXT("#MHADS01");

bool FMetaHumanAnimationSerialization::SetupEncoder(FArchive& InOutArchive, EMaxPrecisionType InMaxPrecisionType, ECompressionMethod InCompressionMethod)
{
	MaxPrecisionType = InMaxPrecisionType;
	CompressionMethod = InCompressionMethod;

	PreviousCurves.Reset();

	FString WriteMagicNumber = MagicNumber;

	InOutArchive << WriteMagicNumber;
	InOutArchive << MaxPrecisionType;
	InOutArchive << CompressionMethod;

	if (CompressionMethod == ECompressionMethod::Sparse)
	{
		InOutArchive << SparsePrecision;
		InOutArchive << SparseFullDataInterval;
	}

	return true;
}

bool FMetaHumanAnimationSerialization::Encode(FArchive& InOutArchive, float InTime, const TArray<float>& InCurves)
{
	if (MaxPrecisionType == EMaxPrecisionType::Undefined || CompressionMethod == ECompressionMethod::Undefined)
	{
		return false;
	}

	InOutArchive << InTime;

	if (CompressionMethod == ECompressionMethod::None)
	{
		if (!EncodeMaxPrecisionValues(InOutArchive, InCurves))
		{
			return false;
		}
	}
	else if (CompressionMethod == ECompressionMethod::Sparse)
	{
		TBitArray SparseValuesMask;
		
		if (PreviousCurves.IsEmpty() || FMath::Abs(InTime - SparseLastFullDataTime) > SparseFullDataInterval)
		{
			PreviousCurves.SetNum(InCurves.Num());
			SparseValuesMask.SetNum(InCurves.Num(), true);
			SparseLastFullDataTime = InTime;
		}
		else
		{
			if (PreviousCurves.Num() != InCurves.Num())
			{
				return false;
			}

			SparseValuesMask.SetNum(InCurves.Num(), false);

			for (int32 Index = 0; Index < InCurves.Num(); ++Index)
			{
				SparseValuesMask[Index] = FMath::Abs(InCurves[Index] - PreviousCurves[Index]) > SparsePrecision;
			}
		}

		InOutArchive << SparseValuesMask;

		TArray<float> SparseValues;
		SparseValues.Reserve(InCurves.Num());

		for (int32 Index = 0; Index < InCurves.Num(); ++Index)
		{
			if (SparseValuesMask[Index])
			{
				SparseValues.Add(InCurves[Index]);
				PreviousCurves[Index] = InCurves[Index];
			}
		}

		if (!EncodeMaxPrecisionValues(InOutArchive, SparseValues))
		{
			return false;
		}
	}
	else
	{
		checkf(false, TEXT("Unknown compression type"));
	}

	return true;
}

bool FMetaHumanAnimationSerialization::EncodeMaxPrecisionValues(FArchive& InOutArchive, const TArray<float>& InValues)
{
	if (InValues.Num() >= MAX_uint16)
	{
		return false;
	}

	uint16 NumValues = (uint16) InValues.Num();

	InOutArchive << NumValues;

	if (MaxPrecisionType == EMaxPrecisionType::Float)
	{
		for (int32 Index = 0; Index < InValues.Num(); ++Index)
		{
			float EncodedValue = InValues[Index];
			InOutArchive << EncodedValue;
		}
	}
	else if (MaxPrecisionType == EMaxPrecisionType::Int16)
	{
		for (int32 Index = 0; Index < InValues.Num(); ++Index)
		{
			uint16 EncodedValue = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt32(InValues[Index] * MAX_uint16), 0, MAX_uint16));
			InOutArchive << EncodedValue;
		}
	}
	else if (MaxPrecisionType == EMaxPrecisionType::Int10)
	{
		TBitArray EncodedBitValues;
		EncodedBitValues.Reserve(InValues.Num() * 10);

		for (int32 Index = 0; Index < InValues.Num(); ++Index)
		{
#define MAX_uint10 ((uint16) 0x3ff)
			uint16 EncodedValue = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt32(InValues[Index] * MAX_uint10), 0, MAX_uint10));
#undef MAX_uint10

			for (int32 Bit = 0; Bit < 10; ++Bit)
			{
				bool EncodedBitValue = (EncodedValue & (1 << Bit));
				EncodedBitValues.Add(EncodedBitValue);
			}
		}

		InOutArchive << EncodedBitValues;
	}
	else
	{
		checkf(false, TEXT("Unknown max precision type"));
	}

	return true;
}

bool FMetaHumanAnimationSerialization::SetupDecoder(FArchive& InOutArchive)
{
	MaxPrecisionType = EMaxPrecisionType::Undefined;
	CompressionMethod = ECompressionMethod::Undefined;

	PreviousCurves.Reset();

	FString ReadMagicNumber;

	InOutArchive << ReadMagicNumber;

	if (ReadMagicNumber != MagicNumber)
	{
		return false;
	}

	InOutArchive << MaxPrecisionType;
	InOutArchive << CompressionMethod;

	if (CompressionMethod == ECompressionMethod::Sparse)
	{
		InOutArchive << SparsePrecision;
		InOutArchive << SparseFullDataInterval;
	}

	return true;
}

bool FMetaHumanAnimationSerialization::Decode(FArchive& InOutArchive, float& OutTime, TArray<float>& OutCurves)
{
	OutCurves.Reset();

	if (MaxPrecisionType == EMaxPrecisionType::Undefined || CompressionMethod == ECompressionMethod::Undefined)
	{
		return false;
	}

	InOutArchive << OutTime;

	if (CompressionMethod == ECompressionMethod::None)
	{
		if (!DecodeMaxPrecisionValues(InOutArchive, OutCurves))
		{
			return false;
		}
	}
	else if (CompressionMethod == ECompressionMethod::Sparse)
	{
		TBitArray SparseValuesMask;
		InOutArchive << SparseValuesMask;

		TArray<float> SparseValues;
		if (!DecodeMaxPrecisionValues(InOutArchive, SparseValues))
		{
			return false;
		}

		for (int32 MaskIndex = 0, ValueIndex = 0; MaskIndex < SparseValuesMask.Num(); ++MaskIndex)
		{
			if (SparseValuesMask[MaskIndex])
			{
				if (ValueIndex >= SparseValues.Num())
				{
					return false;
				}

				OutCurves.Add(SparseValues[ValueIndex++]);
			}
			else
			{
				if (MaskIndex >= PreviousCurves.Num())
				{
					return false;
				}

				OutCurves.Add(PreviousCurves[MaskIndex]);
			}
		}
	}
	else
	{
		checkf(false, TEXT("Unknown compression type"));
		return false;
	}

	PreviousCurves = OutCurves;

	return true;
}

bool FMetaHumanAnimationSerialization::DecodeMaxPrecisionValues(FArchive& InOutArchive, TArray<float>& OutValues)
{
	uint16 NumValues = 0;

	InOutArchive << NumValues;

	OutValues.Reset();
	OutValues.Reserve(NumValues);

	if (MaxPrecisionType == EMaxPrecisionType::Float)
	{
		for (int32 Index = 0; Index < NumValues; ++Index)
		{
			float DecodedValue = 0;
			InOutArchive << DecodedValue;
			OutValues.Add(DecodedValue);
		}
	}
	else if (MaxPrecisionType == EMaxPrecisionType::Int16)
	{
		for (int32 Index = 0; Index < NumValues; ++Index)
		{
			uint16 DecodedValue = 0;
			InOutArchive << DecodedValue;
			OutValues.Add(DecodedValue / (float) MAX_uint16);
		}
	}
	else if (MaxPrecisionType == EMaxPrecisionType::Int10)
	{
		TBitArray DecodedBitValues;
		DecodedBitValues.Reserve(NumValues * 10);

		InOutArchive << DecodedBitValues;

		for (int32 Index = 0, BitIndex = 0; Index < NumValues; ++Index)
		{
			int32 DecodedValue = 0;

			for (int32 Bit = 0; Bit < 10; ++Bit, ++BitIndex)
			{
				if (DecodedBitValues[BitIndex])
				{
					DecodedValue += (1 << Bit);
				}
			}

#define MAX_uint10 ((uint16) 0x3ff)
			OutValues.Add(DecodedValue / (float) MAX_uint10);
#undef MAX_uint10
		}
	}
	else
	{
		checkf(false, TEXT("Unknown max precision type"));
		return false;
	}

	return true;
}

FMetaHumanAnimationSerialization::EMaxPrecisionType FMetaHumanAnimationSerialization::GetMaxPrecisionType() const
{
	return MaxPrecisionType;
}

FMetaHumanAnimationSerialization::ECompressionMethod FMetaHumanAnimationSerialization::GetCompressionMethod() const
{
	return CompressionMethod;
}
