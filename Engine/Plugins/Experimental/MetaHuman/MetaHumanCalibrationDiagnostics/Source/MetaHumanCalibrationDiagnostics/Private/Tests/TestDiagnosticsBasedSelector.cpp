// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selectors/MetaHumanDiagnosticsBasedSelector.h"
#include "Selectors/CalibrationArraySplitter.h"

#include "CaptureData.h"
#include "CameraCalibration.h"
#include "ImgMediaSource.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::MetaHuman::Private
{

namespace DiagnosticsBasedSelectorTestUtils
{

/**
 * Creates a UFootageCaptureData with two UImgMediaSource entries (the selector checks [0] and [1]).
 */
UFootageCaptureData* CreateFootageCaptureData()
{
	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();

	UImgMediaSource* ImageSequence0 = NewObject<UImgMediaSource>(CaptureData);
	UImgMediaSource* ImageSequence1 = NewObject<UImgMediaSource>(CaptureData);

	CaptureData->ImageSequences.Add(ImageSequence0);
	CaptureData->ImageSequences.Add(ImageSequence1);

	return CaptureData;
}

/**
 * Creates a UMetaHumanDiagnosticsBasedSelector with settings and a manual frame provider wired up.
 */
UMetaHumanDiagnosticsBasedSelector* CreateSelectorWithSettings(const TArray<int32>& InSelectedFrames)
{
	UMetaHumanDiagnosticsBasedSelector* Selector = NewObject<UMetaHumanDiagnosticsBasedSelector>();

	UMetaHumanDiagnosticsBasedSelectorSettings* Settings = NewObject<UMetaHumanDiagnosticsBasedSelectorSettings>();

	UMetaHumanManualFrameProvider* FrameProvider = NewObject<UMetaHumanManualFrameProvider>();
	FrameProvider->SelectedFrames = InSelectedFrames;
	Settings->FrameProvider = FrameProvider;

	Selector->SetSettings(Settings);

	return Selector;
}

/**
 * Calls OrderCalibrations via a selector that has no settings attached.
 */
TArray<UCameraCalibration*> RunSelectorWithoutSettings(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCalibrations)
{
	UMetaHumanDiagnosticsBasedSelector* Selector = NewObject<UMetaHumanDiagnosticsBasedSelector>();
	return Selector->OrderCalibrations(InCaptureData, InCalibrations);
}

/**
 * Calls OrderCalibrations with a fully configured selector.
 */
TArray<UCameraCalibration*> RunSelector(UFootageCaptureData* InCaptureData, const TArray<UCameraCalibration*>& InCalibrations, const TArray<int32>& InSelectedFrames)
{
	UMetaHumanDiagnosticsBasedSelector* Selector = CreateSelectorWithSettings(InSelectedFrames);
	return Selector->OrderCalibrations(InCaptureData, InCalibrations);
}

} // namespace DiagnosticsBasedSelectorTestUtils

// ============================================================================
// SplitCalibrationArrayToViews Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_SplitEven,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.SplitArrayToViews.EvenSplit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_SplitEven::RunTest(const FString& InParameters)
{
	TArray<UCameraCalibration*> Source;
	for (int32 i = 0; i < 6; ++i)
	{
		Source.Add(NewObject<UCameraCalibration>());
	}

	TArray<TArrayView<UCameraCalibration* const>> Views = UE::MetaHuman::SplitCalibrationArrayToViews(Source, 3);

	UTEST_EQUAL("Three views created", Views.Num(), 3);
	UTEST_EQUAL("View 0 has 2 elements", Views[0].Num(), 2);
	UTEST_EQUAL("View 1 has 2 elements", Views[1].Num(), 2);
	UTEST_EQUAL("View 2 has 2 elements", Views[2].Num(), 2);

	// Verify views point to the correct elements
	UTEST_EQUAL("View 0 starts at element 0", Views[0][0], Source[0]);
	UTEST_EQUAL("View 1 starts at element 2", Views[1][0], Source[2]);
	UTEST_EQUAL("View 2 starts at element 4", Views[2][0], Source[4]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_SplitUneven,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.SplitArrayToViews.UnevenSplit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_SplitUneven::RunTest(const FString& InParameters)
{
	TArray<UCameraCalibration*> Source;
	for (int32 i = 0; i < 7; ++i)
	{
		Source.Add(NewObject<UCameraCalibration>());
	}

	TArray<TArrayView<UCameraCalibration* const>> Views = UE::MetaHuman::SplitCalibrationArrayToViews(Source, 3);

	UTEST_EQUAL("Three views created", Views.Num(), 3);
	// 7 / 3 = 2 remainder 1, so first chunk gets the extra element
	UTEST_EQUAL("View 0 has 3 elements (gets remainder)", Views[0].Num(), 3);
	UTEST_EQUAL("View 1 has 2 elements", Views[1].Num(), 2);
	UTEST_EQUAL("View 2 has 2 elements", Views[2].Num(), 2);

	// Verify contiguous coverage
	int32 TotalElements = Views[0].Num() + Views[1].Num() + Views[2].Num();
	UTEST_EQUAL("All elements accounted for", TotalElements, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_SplitMoreChunksThanElements,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.SplitArrayToViews.MoreChunksThanElements",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_SplitMoreChunksThanElements::RunTest(const FString& InParameters)
{
	TArray<UCameraCalibration*> Source;
	Source.Add(NewObject<UCameraCalibration>());
	Source.Add(NewObject<UCameraCalibration>());

	TArray<TArrayView<UCameraCalibration* const>> Views = UE::MetaHuman::SplitCalibrationArrayToViews(Source, 5);

	// Can't have more chunks than elements, so clamped to 2
	UTEST_EQUAL("Two views created (clamped)", Views.Num(), 2);
	UTEST_EQUAL("View 0 has 1 element", Views[0].Num(), 1);
	UTEST_EQUAL("View 1 has 1 element", Views[1].Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_SplitSingleElement,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.SplitArrayToViews.SingleElement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_SplitSingleElement::RunTest(const FString& InParameters)
{
	TArray<UCameraCalibration*> Source;
	Source.Add(NewObject<UCameraCalibration>());

	TArray<TArrayView<UCameraCalibration* const>> Views = UE::MetaHuman::SplitCalibrationArrayToViews(Source, 3);

	UTEST_EQUAL("One view created", Views.Num(), 1);
	UTEST_EQUAL("View 0 has 1 element", Views[0].Num(), 1);
	UTEST_EQUAL("View 0 contains the source element", Views[0][0], Source[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_SplitSingleChunk,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.SplitArrayToViews.SingleChunk",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_SplitSingleChunk::RunTest(const FString& InParameters)
{
	TArray<UCameraCalibration*> Source;
	for (int32 i = 0; i < 5; ++i)
	{
		Source.Add(NewObject<UCameraCalibration>());
	}

	TArray<TArrayView<UCameraCalibration* const>> Views = UE::MetaHuman::SplitCalibrationArrayToViews(Source, 1);

	UTEST_EQUAL("One view created", Views.Num(), 1);
	UTEST_EQUAL("View 0 has all 5 elements", Views[0].Num(), 5);
	return true;
}

// ============================================================================
// Input Validation Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_NullCaptureData,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.NullCaptureData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_NullCaptureData::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelector(nullptr, Calibrations, { 0, 1, 2 });
	UTEST_TRUE("Result is empty when CaptureData is null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_EmptyImageSequences,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.EmptyImageSequences",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_EmptyImageSequences::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations, { 0, 1, 2 });
	UTEST_TRUE("Result is empty when ImageSequences is empty", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_SingleImageSequence,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.SingleImageSequence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_SingleImageSequence::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();
	CaptureData->ImageSequences.Add(NewObject<UImgMediaSource>(CaptureData));

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations, { 0, 1, 2 });
	UTEST_TRUE("Result is empty when only one ImageSequence is provided", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_NullFirstImageSequence,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.NullFirstImageSequence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_NullFirstImageSequence::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();
	CaptureData->ImageSequences.Add(nullptr);
	CaptureData->ImageSequences.Add(NewObject<UImgMediaSource>(CaptureData));

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations, { 0, 1, 2 });
	UTEST_TRUE("Result is empty when first ImageSequence is null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_NullSecondImageSequence,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.NullSecondImageSequence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_NullSecondImageSequence::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = NewObject<UFootageCaptureData>();
	CaptureData->ImageSequences.Add(NewObject<UImgMediaSource>(CaptureData));
	CaptureData->ImageSequences.Add(nullptr);

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations, { 0, 1, 2 });
	UTEST_TRUE("Result is empty when second ImageSequence is null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_NullSettings,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.NullSettings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_NullSettings::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = DiagnosticsBasedSelectorTestUtils::CreateFootageCaptureData();

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelectorWithoutSettings(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when settings are not set", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_NullFrameProvider,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.NullFrameProvider",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_NullFrameProvider::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = DiagnosticsBasedSelectorTestUtils::CreateFootageCaptureData();

	UMetaHumanDiagnosticsBasedSelector* Selector = NewObject<UMetaHumanDiagnosticsBasedSelector>();
	UMetaHumanDiagnosticsBasedSelectorSettings* Settings = NewObject<UMetaHumanDiagnosticsBasedSelectorSettings>();
	// Settings->FrameProvider deliberately left null
	Selector->SetSettings(Settings);

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = Selector->OrderCalibrations(CaptureData, Calibrations);
	UTEST_TRUE("Result is empty when FrameProvider is null", Result.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_EmptySelectedFrames,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.EmptySelectedFrames",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_EmptySelectedFrames::RunTest(const FString& InParameters)
{
	bSuppressLogs = true;

	UFootageCaptureData* CaptureData = DiagnosticsBasedSelectorTestUtils::CreateFootageCaptureData();

	TArray<UCameraCalibration*> Calibrations;
	TArray<UCameraCalibration*> Result = DiagnosticsBasedSelectorTestUtils::RunSelector(CaptureData, Calibrations, {});
	UTEST_TRUE("Result is empty when SelectedFrames is empty", Result.IsEmpty());
	return true;
}

// ============================================================================
// ManualFrameProvider Tests
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_ManualFrameProviderReturnsFrames,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.ManualFrameProvider.ReturnsSetFrames",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_ManualFrameProviderReturnsFrames::RunTest(const FString& InParameters)
{
	UMetaHumanManualFrameProvider* Provider = NewObject<UMetaHumanManualFrameProvider>();
	Provider->SelectedFrames = { 5, 10, 15, 20 };

	TArray<int32> Result = Provider->GetSelectedFrames();

	UTEST_EQUAL("Returns correct number of frames", Result.Num(), 4);
	UTEST_EQUAL("Frame 0 is 5", Result[0], 5);
	UTEST_EQUAL("Frame 1 is 10", Result[1], 10);
	UTEST_EQUAL("Frame 2 is 15", Result[2], 15);
	UTEST_EQUAL("Frame 3 is 20", Result[3], 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_ManualFrameProviderEmptyByDefault,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.ManualFrameProvider.EmptyByDefault",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_ManualFrameProviderEmptyByDefault::RunTest(const FString& InParameters)
{
	UMetaHumanManualFrameProvider* Provider = NewObject<UMetaHumanManualFrameProvider>();

	TArray<int32> Result = Provider->GetSelectedFrames();

	UTEST_TRUE("Default-constructed provider returns empty frames", Result.IsEmpty());
	return true;
}

// ============================================================================
// GetSettingsClass Test
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDiagnosticsBasedSelectorTest_GetSettingsClass,
	"MetaHuman.Calibration.DiagnosticsBasedSelector.GetSettingsClass",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FDiagnosticsBasedSelectorTest_GetSettingsClass::RunTest(const FString& InParameters)
{
	UMetaHumanDiagnosticsBasedSelector* Selector = NewObject<UMetaHumanDiagnosticsBasedSelector>();
	TSubclassOf<UMetaHumanCalibrationSelectorSettings> SettingsClass = Selector->GetSettingsClass();

	UTEST_TRUE("Settings class is not null", SettingsClass != nullptr);
	UTEST_EQUAL("Settings class is UMetaHumanDiagnosticsBasedSelectorSettings", SettingsClass.Get(), UMetaHumanDiagnosticsBasedSelectorSettings::StaticClass());
	return true;
}

} // namespace UE::MetaHuman::Private

#endif // WITH_DEV_AUTOMATION_TESTS
