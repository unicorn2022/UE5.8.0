// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameOffsetCalculator.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::MetaHuman::Private
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_OneCamera,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.OneCamera",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_OneCamera::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 0, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1 });

	UTEST_TRUE("Frame Offset", FrameOffsets.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_FrameRatesIncompatible,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.FrameRatesIncompatible",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_FrameRatesIncompatible::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 0, false),
		.FrameRate = FFrameRate(48'000, 1'000)
	};

	DepthGenerator::FCameraTimecodeInfo TimecodeInfo2 =
	{
		.Timecode = FTimecode(0, 0, 0, 0, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1, TimecodeInfo2 });

	UTEST_TRUE("Frame Offset", FrameOffsets.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_OneFrameMismatch,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.OneFrameMismatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_OneFrameMismatch::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 0, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	DepthGenerator::FCameraTimecodeInfo TimecodeInfo2 = 
	{
		.Timecode = FTimecode(0, 0, 0, 1, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1, TimecodeInfo2 });

	UTEST_EQUAL("Frame Offset", FrameOffsets[0], 1);
	UTEST_EQUAL("Frame Offset", FrameOffsets[1], 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_MoreFrameMismatch,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.MoreFrameMismatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_MoreFrameMismatch::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 2, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	DepthGenerator::FCameraTimecodeInfo TimecodeInfo2 =
	{
		.Timecode = FTimecode(0, 0, 0, 6, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1, TimecodeInfo2 });

	UTEST_EQUAL("Frame Offset", FrameOffsets[0], 4);
	UTEST_EQUAL("Frame Offset", FrameOffsets[1], 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_FrameMismatch_DiffFrameRate,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.FrameMismatchDiffFrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_FrameMismatch_DiffFrameRate::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 2, false),
		.FrameRate = FFrameRate(30'000, 1'000)
	};

	DepthGenerator::FCameraTimecodeInfo TimecodeInfo2 =
	{
		.Timecode = FTimecode(0, 0, 0, 6, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1, TimecodeInfo2 });

	// 0, 0, 1, 1, 2, 2, 3
	// 0, 1, 2, 3, 4, 5, 6,
	UTEST_EQUAL("Frame Offset", FrameOffsets[0], 1);
	UTEST_EQUAL("Frame Offset", FrameOffsets[1], 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_NoFrameMismatch_DiffFrameRate,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.NoFrameMismatchDiffFrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_NoFrameMismatch_DiffFrameRate::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 3, false),
		.FrameRate = FFrameRate(30'000, 1'000)
	};

	DepthGenerator::FCameraTimecodeInfo TimecodeInfo2 =
	{
		.Timecode = FTimecode(0, 0, 0, 6, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1, TimecodeInfo2 });

	// 0, 0, 1, 1, 2, 2, 3
	// 0, 1, 2, 3, 4, 5, 6,
	UTEST_EQUAL("Frame Offset", FrameOffsets[0], 0);
	UTEST_EQUAL("Frame Offset", FrameOffsets[1], 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCalculateFrameOffset_MoreFrameMismatch_DiffFrameRate,
	"MetaHuman.Capture.DepthGenerator.CalculateFrameOffset.MoreFrameMismatchDiffFrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FCalculateFrameOffset_MoreFrameMismatch_DiffFrameRate::RunTest(const FString& InParameters)
{
	DepthGenerator::FCameraTimecodeInfo TimecodeInfo1 =
	{
		.Timecode = FTimecode(0, 0, 0, 1, false),
		.FrameRate = FFrameRate(30'000, 1'000)
	};

	DepthGenerator::FCameraTimecodeInfo TimecodeInfo2 =
	{
		.Timecode = FTimecode(0, 0, 0, 6, false),
		.FrameRate = FFrameRate(60'000, 1'000)
	};

	TArray<int32> FrameOffsets = DepthGenerator::CalculateFrameOffset({ TimecodeInfo1, TimecodeInfo2 });

	// 0, 0, 1, 1, 2, 2, 3
	// 0, 1, 2, 3, 4, 5, 6,
	UTEST_EQUAL("Frame Offset", FrameOffsets[0], 2);
	UTEST_EQUAL("Frame Offset", FrameOffsets[1], 0);

	return true;
}

}

#endif