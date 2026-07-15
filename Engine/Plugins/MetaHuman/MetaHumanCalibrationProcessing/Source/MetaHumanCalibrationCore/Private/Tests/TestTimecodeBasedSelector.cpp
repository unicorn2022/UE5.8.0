// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selectors/MetaHumanTimecodeBasedSelector.h"

#include "CaptureData.h"
#include "CameraCalibration.h"
#include "CameraCalibrationMetadata.h"
#include "ImgMediaSource.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::MetaHuman::Private
{

namespace TimecodeBasedSelectorTestUtils
{
/**
 * Creates a UFootageCaptureData with a single UImgMediaSource configured with the given timecode and frame rate.
 * Objects are created in the transient package so they are cleaned up by GC after the test.
 */
UFootageCaptureData* CreateFootageCaptureData(const FTimecode& InTimecode, const FFrameRate& InFrameRate)
{
	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();

	UImgMediaSource* ImageSequence = NewObject<UImgMediaSource>(CaptureData);
	ImageSequence->StartTimecode = InTimecode;
	ImageSequence->FrameRateOverride = InFrameRate;

	CaptureData->ImageSequences.Add(ImageSequence);

	return CaptureData;
}

/**
 * Creates a UCameraCalibration with attached UCameraCalibrationMetadata.
 */
UCameraCalibration* CreateCalibrationWithMetadata(
	const FTimecode& InGenerationTimecode,
	const FFrameRate& InGenerationFrameRate,
	double InReprojectionRMSError,
	int32 InNumSelectedFrames)
{
	UCameraCalibration* Calibration = NewObject<UCameraCalibration>();

	UCameraCalibrationMetadata* Metadata = NewObject<UCameraCalibrationMetadata>();
	Metadata->GenerationTimecode = InGenerationTimecode;
	Metadata->GenerationFrameRate = InGenerationFrameRate;
	Metadata->ReprojectionRMSError = InReprojectionRMSError;

	Metadata->SelectedFrames.SetNum(InNumSelectedFrames);
	for (int32 Idx = 0; Idx < InNumSelectedFrames; ++Idx)
	{
		Metadata->SelectedFrames[Idx] = Idx;
	}

	UCameraCalibrationMetadata::SetCameraCalibrationMetadata(Calibration, Metadata);

	return Calibration;
}

/**
 * Calls OrderCalibrations on a new UMetaHumanTimecodeBasedSelector instance.
 */
TArray<UCameraCalibration*> RunSelector(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCalibrations)
{
	UMetaHumanTimecodeBasedSelector* Selector = NewObject<UMetaHumanTimecodeBasedSelector>();
	return Selector->OrderCalibrations(InCaptureData, InCalibrations);
}

} // namespace TimecodeBasedSelectorTestUtils

// ============================================================================
// Input Validation Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_NullCaptureData,
	"MetaHuman.Calibration.TimecodeBasedSelector.NullCaptureData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_NullCaptureData::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(nullptr, Calibrations);
	UTEST_TRUE("Result is empty when CaptureData is null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_EmptyImageSequences,
	"MetaHuman.Calibration.TimecodeBasedSelector.EmptyImageSequences",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_EmptyImageSequences::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when ImageSequences is empty", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_NullFirstImageSequence,
	"MetaHuman.Calibration.TimecodeBasedSelector.NullFirstImageSequence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_NullFirstImageSequence::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();
	CaptureData->ImageSequences.Add(nullptr);

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when first ImageSequence is null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_InvalidTimecode,
	"MetaHuman.Calibration.TimecodeBasedSelector.InvalidTimecode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_InvalidTimecode::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	// FTimecode::IsValid() checks |Minutes| <= 59, |Seconds| <= 59, Subframe >= 0.
	// Setting Minutes to 60 makes IsValid() return false.
	FTimecode InvalidTimecode;
	InvalidTimecode.Minutes = 60;

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(InvalidTimecode, FFrameRate(30, 1));

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when Timecode is invalid", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_InvalidFrameRate,
	"MetaHuman.Calibration.TimecodeBasedSelector.InvalidFrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_InvalidFrameRate::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode ValidTimecode(1, 0, 0, 0, false);

	// Setting Denominator to 0 makes IsValid () return false.
	FFrameRate InvalidFrameRate(0, 0);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(ValidTimecode, InvalidFrameRate);

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when FrameRate denominator is zero", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_ZeroFrameRate,
	"MetaHuman.Calibration.TimecodeBasedSelector.ZeroFrameRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_ZeroFrameRate::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode ValidTimecode(1, 0, 0, 0, false);
	FFrameRate ZeroFrameRate(0, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(ValidTimecode, ZeroFrameRate);

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when FrameRate numerator is zero", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_EmptyCalibrations,
	"MetaHuman.Calibration.TimecodeBasedSelector.EmptyCalibrations",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_EmptyCalibrations::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when calibrations array is empty", Result.IsEmpty());
	return true;
}

// ============================================================================
// Calibration Filtering Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_NullCalibrationSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.NullCalibrationSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_NullCalibrationSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	TArray<UCameraCalibration*> Calibrations;
	Calibrations.Add(nullptr);
	Calibrations.Add(nullptr);

	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when all calibrations are null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_MissingMetadataSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.MissingMetadataSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_MissingMetadataSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	// Create calibration without attaching any metadata
	UCameraCalibration* Calibration = NewObject<UCameraCalibration>();

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when calibration has no metadata", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_ZeroReprojectionRMSSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.ZeroReprojectionRMSSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_ZeroReprojectionRMSSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 0, 0, false), FFrameRate(30, 1),
		0.0,  // Zero RMS error → should be filtered
		10);

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when ReprojectionRMSError is zero", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_HighReprojectionRMSSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.HighReprojectionRMSSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_HighReprojectionRMSSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 0, 0, false), FFrameRate(30, 1),
		1.5,  // RMS error > 1.0 → should be filtered
		10);

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when ReprojectionRMSError exceeds 1.0", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_TooFewFramesSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.TooFewFramesSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_TooFewFramesSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 0, 0, false), FFrameRate(30, 1),
		0.5,
		4);  // Fewer than 5 frames → should be filtered

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when SelectedFrames count is below 5", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_InvalidGenerationTimecodeSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.InvalidGenerationTimecodeSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_InvalidGenerationTimecodeSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	// Create calibration with invalid generation timecode (Minutes out of range makes IsValid() return false)
	FTimecode InvalidTimecode;
	InvalidTimecode.Minutes = 60;

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		InvalidTimecode, FFrameRate(30, 1),
		0.5,
		10);

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when calibration has invalid generation timecode", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_InvalidGenerationFrameRateSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.InvalidGenerationFrameRateSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_InvalidGenerationFrameRateSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	FTimecode GenerationTimecode(1, 0, 0, 0, false);

	// Create calibration with invalid generation frame rate (0 Denominator makes IsValid() return false)
	FFrameRate InvalidFrameRate(0, 0);

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		GenerationTimecode, InvalidFrameRate,
		0.5,
		10);

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when calibration generation FrameRate denominator is zero", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_ZeroGenerationFrameRateSkipped,
	"MetaHuman.Calibration.TimecodeBasedSelector.ZeroGenerationFrameRateSkipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_ZeroGenerationFrameRateSkipped::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	FTimecode GenerationTimecode(1, 0, 0, 0, false);
	FFrameRate ZeroFrameRate(0, 1);

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		GenerationTimecode, ZeroFrameRate,
		0.5,
		10);

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when calibration generation FrameRate numerator is zero", Result.IsEmpty());
	return true;
}

// ============================================================================
// Ordering Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_SingleValidCalibration,
	"MetaHuman.Calibration.TimecodeBasedSelector.SingleValidCalibration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_SingleValidCalibration::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	UCameraCalibration* Calibration = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 5, 0, false), FFrameRate(30, 1),
		0.5,
		10);

	TArray<UCameraCalibration*> Calibrations = { Calibration };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);
	UTEST_EQUAL("Result contains exactly one calibration", Result.Num(), 1);
	UTEST_EQUAL("Result contains the expected calibration", Result[0], Calibration);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_OrderedByTimecodeProximity,
	"MetaHuman.Calibration.TimecodeBasedSelector.OrderedByTimecodeProximity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_OrderedByTimecodeProximity::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	// Footage timecode at 01:00:00:00
	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	// Calibration far from footage: 02:00:00:00 (1 hour away)
	UCameraCalibration* CalibrationFar = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(2, 0, 0, 0, false), FFrameRate(30, 1),
		0.5,
		10);

	// Calibration close to footage: 01:00:10:00 (10 seconds away)
	UCameraCalibration* CalibrationClose = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 10, 0, false), FFrameRate(30, 1),
		0.5,
		10);

	// Calibration closest to footage: 01:00:01:00 (1 second away)
	UCameraCalibration* CalibrationClosest = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 1, 0, false), FFrameRate(30, 1),
		0.5,
		10);

	// Pass in non-sorted order to verify the selector sorts them
	TArray<UCameraCalibration*> Calibrations = { CalibrationFar, CalibrationClose, CalibrationClosest };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);

	UTEST_EQUAL("Result contains all three calibrations", Result.Num(), 3);
	UTEST_EQUAL("First result is closest calibration", Result[0], CalibrationClosest);
	UTEST_EQUAL("Second result is close calibration", Result[1], CalibrationClose);
	UTEST_EQUAL("Third result is far calibration", Result[2], CalibrationFar);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTimecodeBasedSelectorTest_MixedValidAndInvalid,
	"MetaHuman.Calibration.TimecodeBasedSelector.MixedValidAndInvalid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FTimecodeBasedSelectorTest_MixedValidAndInvalid::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	// Footage timecode at 01:00:00:00
	FTimecode FootageTimecode(1, 0, 0, 0, false);
	FFrameRate FrameRate(30, 1);

	UFootageCaptureData* CaptureData = TimecodeBasedSelectorTestUtils::CreateFootageCaptureData(FootageTimecode, FrameRate);

	// Valid calibration, far: 02:00:00:00
	UCameraCalibration* ValidFar = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(2, 0, 0, 0, false), FFrameRate(30, 1),
		0.5,
		10);

	// Invalid: high RMS error
	UCameraCalibration* InvalidHighRMS = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 0, 5, false), FFrameRate(30, 1),
		2.0,  // Too high
		10);

	// Valid calibration, close: 01:00:05:00
	UCameraCalibration* ValidClose = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 5, 0, false), FFrameRate(30, 1),
		0.3,
		8);

	// Invalid: too few frames
	UCameraCalibration* InvalidFewFrames = TimecodeBasedSelectorTestUtils::CreateCalibrationWithMetadata(
		FTimecode(1, 0, 0, 1, false), FFrameRate(30, 1),
		0.5,
		3);  // Too few

	// null entry
	UCameraCalibration* NullEntry = nullptr;

	// No metadata
	UCameraCalibration* NoMetadata = NewObject<UCameraCalibration>();

	TArray<UCameraCalibration*> Calibrations = { ValidFar, InvalidHighRMS, ValidClose, InvalidFewFrames, NullEntry, NoMetadata };
	TArray<UCameraCalibration*> Result = TimecodeBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations);

	UTEST_EQUAL("Result contains only the two valid calibrations", Result.Num(), 2);
	UTEST_EQUAL("First result is the closer valid calibration", Result[0], ValidClose);
	UTEST_EQUAL("Second result is the farther valid calibration", Result[1], ValidFar);
	return true;
}

} // namespace UE::MetaHuman::Private

#endif // WITH_DEV_AUTOMATION_TESTS
