// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAnimationSerialization.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanAnimationSerializationEditorTest, "MetaHuman.AnimationTools.Serialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)



bool FMetaHumanAnimationSerializationEditorTest::RunTest(const FString& InTestCommand)
{
	bool bIsOK = true;

	for (FMetaHumanAnimationSerialization::EMaxPrecisionType MaxPrecisionType : {
			FMetaHumanAnimationSerialization::EMaxPrecisionType::Float,
			FMetaHumanAnimationSerialization::EMaxPrecisionType::Int16,
			FMetaHumanAnimationSerialization::EMaxPrecisionType::Int10 } )
	{
		for (FMetaHumanAnimationSerialization::ECompressionMethod CompressionMethod : {
				FMetaHumanAnimationSerialization::ECompressionMethod::None,
				FMetaHumanAnimationSerialization::ECompressionMethod::Sparse })
		{
			TArray<uint8> Data;
			TMap<int32, TArray<float>> ExpectedValues;

			FMemoryWriter WriteArchive(Data);

			FMetaHumanAnimationSerialization Writer;

			bIsOK &= TestTrue(TEXT("Setup encoder"), Writer.SetupEncoder(WriteArchive, MaxPrecisionType, CompressionMethod));

			for (int32 Frame = 0; Frame < 500 && bIsOK; ++Frame)
			{
				TArray<float> Values;

				for (int32 Curve = 0; Curve < 50; ++Curve)
				{
					const int32 CurveType = Curve % 3;

					if (CurveType == 0)
					{
						Values.Add(0.3f); // Constant
					}
					else if (CurveType == 1)
					{
						Values.Add((Frame / 500.0) * 0.5 + (Curve / 50.0) * 0.5); // Changes every frame and curve
					}
					else
					{
						Values.Add(((Frame / 18) / 30.0) * 0.5 + (Curve / 50.0) * 0.5); // Changes every curve but held for 18 frames (not some factor of 60 fps rate)
					}
				}

				bIsOK &= TestTrue(TEXT("Encode frame"), Writer.Encode(WriteArchive, Frame / 60.0f, Values));

				ExpectedValues.Add(Frame, Values);
			}

			if (!bIsOK)
			{
				goto done;
			}

			FMemoryReader ReadArchive(Data);

			FMetaHumanAnimationSerialization Reader;

			bIsOK &= TestTrue(TEXT("Setup decoder"), Reader.SetupDecoder(ReadArchive));

			bIsOK &= TestEqual(TEXT("Expected max precision type"), Reader.GetMaxPrecisionType(), MaxPrecisionType);
			bIsOK &= TestEqual(TEXT("Expected compression method"), Reader.GetCompressionMethod(), CompressionMethod);

			float Tolerance = 0;

			if (MaxPrecisionType == FMetaHumanAnimationSerialization::EMaxPrecisionType::Float)
			{
				Tolerance = 0; // Should be able to represent exactly
			}
			else if (MaxPrecisionType == FMetaHumanAnimationSerialization::EMaxPrecisionType::Int16)
			{
				Tolerance = 1 / 65536.0; // Should be able to represent to 16 bit precision
			}
			else if (MaxPrecisionType == FMetaHumanAnimationSerialization::EMaxPrecisionType::Int10)
			{
				Tolerance = 1 / 1024.0; // Should be able to represent to 10 bit precision
			}

			if (CompressionMethod == FMetaHumanAnimationSerialization::ECompressionMethod::Sparse)
			{
				Tolerance += 0.001f; // Sparse compressed results can differ by 0.001 (SparsePrecision value in FMetaHumanAnimationSerialization)
			}

			for (int32 Frame = 0; Frame < ExpectedValues.Num() && bIsOK; ++Frame)
			{
				TArray<float> Values;
				float Time = 0;

				bIsOK &= TestTrue(TEXT("Decode frame"), Reader.Decode(ReadArchive, Time, Values));

				bIsOK &= TestEqual(TEXT("Expected time"), Time, Frame / 60.0f);
				bIsOK &= TestEqual(TEXT("Expected quantity"), Values.Num(), ExpectedValues[Frame].Num());

				for (int32 Curve = 0; Curve < ExpectedValues[Frame].Num() && bIsOK; ++Curve)
				{
					bIsOK &= TestEqual(TEXT("Expected value"), Values[Curve], ExpectedValues[Frame][Curve], Tolerance);
				}
			}

			if (!bIsOK)
			{
				goto done;
			}
		}
	}

done:

	return bIsOK;
}

#endif
