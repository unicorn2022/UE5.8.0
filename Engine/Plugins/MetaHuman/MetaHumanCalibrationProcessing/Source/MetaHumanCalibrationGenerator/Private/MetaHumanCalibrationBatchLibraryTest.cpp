// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationBatchLibrary.h"
#include "MetaHumanCalibrationGeneratorConfig.h"
#include "MetaHumanCalibrationGeneratorOptions.h"
#include "CaptureData.h"
#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "IImageWrapperModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS

namespace UE::MetaHuman::CalibrationBatch::Tests
{

/**
 * Test coverage:
 *
 * These tests cover input validation, error handling, and ConstructDefaultOptionsForCaptureData
 * property derivation  - including AOI construction from real image files on disk.
 *
 * Tests that use on-disk images write minimal synthetic PNGs to FPaths::AutomationTransientDir(),
 * which the automation controller clears at the start of each test run. Individual tests also
 * clean up after themselves via ON_SCOPE_EXIT.
 *
 * NOT covered (requires valid stereo chessboard footage):
 * - Successful calibration (happy path): bSuccess=true, RMSError, CalibrationAssetPath
 * - Auto frame selection with real footage
 * - Generator Init/Process failure propagation
 * - Cancellation via FScopedSlowTask
 */

// ============================================================
// Helpers
// ============================================================

static UMetaHumanCalibrationGeneratorConfig* MakeDefaultBoardConfig()
{
	UMetaHumanCalibrationGeneratorConfig* Config = NewObject<UMetaHumanCalibrationGeneratorConfig>();
	Config->BoardPatternWidth = 11;
	Config->BoardPatternHeight = 16;
	Config->BoardSquareSize = 0.75f;
	return Config;
}

static UMetaHumanCalibrationGeneratorOptions* MakeDefaultOptions()
{
	UMetaHumanCalibrationGeneratorOptions* Options = NewObject<UMetaHumanCalibrationGeneratorOptions>();
	Options->PackagePath.Path = TEXT("/Game/Test/BatchLibTest");
	Options->AssetName = TEXT("CC_Test");
	Options->bAutoSaveAssets = false;
	return Options;
}

static UFootageCaptureData* MakeEmptyCaptureData()
{
	return NewObject<UFootageCaptureData>();
}

/** Write a minimal solid-gray PNG to disk. Returns true on success. */
static bool WriteSyntheticPNG(const FString& FilePath, int32 Width, int32 Height)
{
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	// Create a solid mid-gray image (pixel value doesn't matter  - we only need valid dimensions).
	FImage Image;
	Image.Init(Width, Height, ERawImageFormat::G8, EGammaSpace::sRGB);
	FMemory::Memset(Image.RawData.GetData(), 128, Image.RawData.Num());

	TArray64<uint8> CompressedPNG;
	if (!ImageWrapperModule.CompressImage(CompressedPNG, EImageFormat::PNG, Image))
	{
		return false;
	}

	return FFileHelper::SaveArrayToFile(
		TArrayView64<const uint8>(CompressedPNG.GetData(), CompressedPNG.Num()), *FilePath);
}

/**
 * Create a UImgMediaSource backed by synthetic PNG files on disk.
 *
 * Writes NumFrames PNG files at the given resolution into a subdirectory of BaseDir.
 * The returned UImgMediaSource has its SequencePath set to the absolute directory path
 * and FrameRateOverride set to 30fps.
 *
 * @param BaseDir     Parent directory (should be under AutomationTransientDir).
 * @param SubDirName  Name of the subdirectory (e.g. "CameraA").
 * @param Width       Image width in pixels.
 * @param Height      Image height in pixels.
 * @param NumFrames   Number of PNG files to write.
 * @return The configured UImgMediaSource, or nullptr on failure.
 */
static UImgMediaSource* MakeSyntheticImageSource(
	const FString& BaseDir, const FString& SubDirName,
	int32 Width, int32 Height, int32 NumFrames = 3)
{
	const FString SequenceDir = BaseDir / SubDirName;
	IFileManager::Get().MakeDirectory(*SequenceDir, true);

	for (int32 Index = 0; Index < NumFrames; ++Index)
	{
		const FString FileName = FString::Printf(TEXT("frame_%04d.png"), Index);
		if (!WriteSyntheticPNG(SequenceDir / FileName, Width, Height))
		{
			return nullptr;
		}
	}

	UImgMediaSource* Source = NewObject<UImgMediaSource>();
	Source->SetSequencePath(SequenceDir);
	Source->FrameRateOverride = FFrameRate(30, 1);
	return Source;
}

// ============================================================
// 1. Input Validation  - BatchGenerateCalibration (Simple API)
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchSimple_EmptyInput,
	"MetaHuman.Calibration.Batch.Simple.EmptyInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchSimple_EmptyInput::RunTest(const FString& Parameters)
{
	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibration(
			{}, MakeDefaultBoardConfig());

	TestEqual(TEXT("Empty input returns empty results"), Results.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchSimple_NullConfig,
	"MetaHuman.Calibration.Batch.Simple.NullConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchSimple_NullConfig::RunTest(const FString& Parameters)
{
	TArray<UFootageCaptureData*> Assets = { MakeEmptyCaptureData() };

	AddExpectedError(TEXT("InBoardConfig must not be null"), EAutomationExpectedErrorFlags::Contains);

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibration(
			Assets, nullptr);

	TestEqual(TEXT("Null config returns empty results"), Results.Num(), 0);
	return true;
}

// ============================================================
// 2. Input Validation  - BatchGenerateCalibrationWithOptions
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_EmptyInput,
	"MetaHuman.Calibration.Batch.WithOptions.EmptyInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_EmptyInput::RunTest(const FString& Parameters)
{
	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			{}, MakeDefaultBoardConfig(), {});

	TestEqual(TEXT("Empty input returns empty results"), Results.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_NullConfig,
	"MetaHuman.Calibration.Batch.WithOptions.NullConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_NullConfig::RunTest(const FString& Parameters)
{
	TArray<UFootageCaptureData*> Assets = { MakeEmptyCaptureData() };
	TArray<UMetaHumanCalibrationGeneratorOptions*> Options = { MakeDefaultOptions() };

	AddExpectedError(TEXT("InBoardConfig must not be null"), EAutomationExpectedErrorFlags::Contains);

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			Assets, nullptr, Options);

	TestEqual(TEXT("Null config returns empty results"), Results.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_MismatchedArrayLengths,
	"MetaHuman.Calibration.Batch.WithOptions.MismatchedArrayLengths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_MismatchedArrayLengths::RunTest(const FString& Parameters)
{
	TArray<UFootageCaptureData*> Assets = {
		MakeEmptyCaptureData(),
		MakeEmptyCaptureData(),
		MakeEmptyCaptureData()
	};
	TArray<UMetaHumanCalibrationGeneratorOptions*> Options = {
		MakeDefaultOptions(),
		MakeDefaultOptions()
	};

	AddExpectedError(TEXT("must be the same length"), EAutomationExpectedErrorFlags::Contains);

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			Assets, MakeDefaultBoardConfig(), Options);

	TestEqual(TEXT("Mismatched arrays return empty results"), Results.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_NullCaptureData,
	"MetaHuman.Calibration.Batch.WithOptions.NullCaptureData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_NullCaptureData::RunTest(const FString& Parameters)
{
	TArray<UFootageCaptureData*> Assets = { nullptr };
	TArray<UMetaHumanCalibrationGeneratorOptions*> Options = { MakeDefaultOptions() };

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			Assets, MakeDefaultBoardConfig(), Options);

	TestEqual(TEXT("Returns one result"), Results.Num(), 1);
	TestFalse(TEXT("Result is not success"), Results[0].bSuccess);
	TestTrue(TEXT("Error message mentions null"),
		Results[0].ErrorMessage.Contains(TEXT("null")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_NullOptions,
	"MetaHuman.Calibration.Batch.WithOptions.NullOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_NullOptions::RunTest(const FString& Parameters)
{
	TArray<UFootageCaptureData*> Assets = { MakeEmptyCaptureData() };
	TArray<UMetaHumanCalibrationGeneratorOptions*> Options = { nullptr };

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			Assets, MakeDefaultBoardConfig(), Options);

	TestEqual(TEXT("Returns one result"), Results.Num(), 1);
	TestFalse(TEXT("Result is not success"), Results[0].bSuccess);
	TestTrue(TEXT("Error message mentions null"),
		Results[0].ErrorMessage.Contains(TEXT("null")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_NullAmongValid,
	"MetaHuman.Calibration.Batch.WithOptions.NullAmongValidAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_NullAmongValid::RunTest(const FString& Parameters)
{
	// First and third are valid (but empty  - they'll fail at frame selection, not null check).
	// Second is null. Verifies the loop continues past null entries.
	TArray<UFootageCaptureData*> Assets = {
		MakeEmptyCaptureData(),
		nullptr,
		MakeEmptyCaptureData()
	};
	TArray<UMetaHumanCalibrationGeneratorOptions*> Options = {
		MakeDefaultOptions(),
		MakeDefaultOptions(),
		MakeDefaultOptions()
	};

	// The two non-null entries will fail at auto frame selection (no image sequences).
	AddExpectedError(TEXT("expects 2 cameras"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("No frames selected"), EAutomationExpectedErrorFlags::Contains, 2);

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			Assets, MakeDefaultBoardConfig(), Options);

	TestEqual(TEXT("Returns three results"), Results.Num(), 3);

	// Second entry (null) should have null error
	TestFalse(TEXT("Null entry is not success"), Results[1].bSuccess);
	TestTrue(TEXT("Null entry error mentions null"),
		Results[1].ErrorMessage.Contains(TEXT("null")));

	// First and third should have results (will fail at frame selection since no image sequences,
	// but they should NOT be skipped  - they should have their own error messages).
	TestFalse(TEXT("First entry is not success (no image sequences)"), Results[0].bSuccess);
	TestFalse(TEXT("Third entry is not success (no image sequences)"), Results[2].bSuccess);
	TestFalse(TEXT("First entry error is not about null"),
		Results[0].ErrorMessage.Contains(TEXT("null")));
	TestFalse(TEXT("Third entry error is not about null"),
		Results[2].ErrorMessage.Contains(TEXT("null")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchWithOptions_InvalidBoardConfig,
	"MetaHuman.Calibration.Batch.WithOptions.InvalidBoardConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBatchWithOptions_InvalidBoardConfig::RunTest(const FString& Parameters)
{
	UFootageCaptureData* CD = MakeEmptyCaptureData();
	UMetaHumanCalibrationGeneratorOptions* Opts = MakeDefaultOptions();

	// Board config with invalid dimensions (width=0).
	UMetaHumanCalibrationGeneratorConfig* BadConfig = MakeDefaultBoardConfig();
	BadConfig->BoardPatternWidth = 0;

	AddExpectedError(TEXT("Board config is invalid"), EAutomationExpectedErrorFlags::Contains);

	TArray<FMetaHumanCalibrationBatchResult> Results =
		UMetaHumanCalibrationBatchLibrary::BatchGenerateCalibrationWithOptions(
			{ CD }, BadConfig, { Opts });

	TestEqual(TEXT("Invalid config returns empty results"), Results.Num(), 0);
	return true;
}

// ============================================================
// 3. ConstructDefaultOptionsForCaptureData
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_NullInput,
	"MetaHuman.Calibration.Batch.ConstructOptions.NullInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_NullInput::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("InCaptureData is null"), EAutomationExpectedErrorFlags::Contains);

	UMetaHumanCalibrationGeneratorOptions* Result =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(nullptr);

	TestNull(TEXT("Null input returns nullptr"), Result);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_AssetNameDerived,
	"MetaHuman.Calibration.Batch.ConstructOptions.AssetNameDerived",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_AssetNameDerived::RunTest(const FString& Parameters)
{
	UFootageCaptureData* CD = MakeEmptyCaptureData();

	UMetaHumanCalibrationGeneratorOptions* Options =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD);

	TestNotNull(TEXT("Options created"), Options);
	TestTrue(TEXT("AssetName starts with CC_ prefix"),
		Options->AssetName.StartsWith(TEXT("CC_")));
	TestTrue(TEXT("AssetName contains the CaptureData object name"),
		Options->AssetName.Contains(CD->GetName()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_PackagePathDerived,
	"MetaHuman.Calibration.Batch.ConstructOptions.PackagePathDerived",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_PackagePathDerived::RunTest(const FString& Parameters)
{
	UFootageCaptureData* CD = MakeEmptyCaptureData();

	UMetaHumanCalibrationGeneratorOptions* Options =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD);

	TestNotNull(TEXT("Options created"), Options);
	// PackagePath should be the parent directory of the CaptureData asset (no subfolder).
	const FString ExpectedPath = FPackageName::GetLongPackagePath(CD->GetPackage()->GetName());
	TestEqual(TEXT("PackagePath matches CaptureData parent directory"),
		Options->PackagePath.Path, ExpectedPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_MultipleAssets,
	"MetaHuman.Calibration.Batch.ConstructOptions.MultipleAssetsIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_MultipleAssets::RunTest(const FString& Parameters)
{
	UFootageCaptureData* CD_A = MakeEmptyCaptureData();
	UFootageCaptureData* CD_B = MakeEmptyCaptureData();

	UMetaHumanCalibrationGeneratorOptions* Opts_A =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD_A);
	UMetaHumanCalibrationGeneratorOptions* Opts_B =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD_B);

	TestNotNull(TEXT("Options A created"), Opts_A);
	TestNotNull(TEXT("Options B created"), Opts_B);

	// Each asset gets independently derived names.
	TestTrue(TEXT("A AssetName contains CD_A name"), Opts_A->AssetName.Contains(CD_A->GetName()));
	TestTrue(TEXT("B AssetName contains CD_B name"), Opts_B->AssetName.Contains(CD_B->GetName()));

	// Not aliased  - different objects with different AssetNames.
	TestFalse(TEXT("AssetNames are different"), Opts_A->AssetName == Opts_B->AssetName);

	// Both share the same PackagePath (same parent directory for transient objects)
	// but have different AssetNames, so they won't collide.
	TestTrue(TEXT("Not aliased  - different UObject pointers"), Opts_A != Opts_B);

	return true;
}

// ============================================================
// 4. ConstructDefaultOptionsForCaptureData  - AOI from image files
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_AOI_MatchesImageDimensions,
	"MetaHuman.Calibration.Batch.ConstructOptions.AOI.MatchesImageDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_AOI_MatchesImageDimensions::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::AutomationTransientDir() / TEXT("MHCalibBatchTest_AOIDimensions");
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestDir, false, true); };

	UImgMediaSource* SourceA = MakeSyntheticImageSource(TestDir, TEXT("CameraA"), 1920, 1080);
	UImgMediaSource* SourceB = MakeSyntheticImageSource(TestDir, TEXT("CameraB"), 1920, 1080);
	UTEST_NOT_NULL(TEXT("SourceA created"), SourceA);
	UTEST_NOT_NULL(TEXT("SourceB created"), SourceB);

	UFootageCaptureData* CD = MakeEmptyCaptureData();
	CD->ImageSequences.Add(SourceA);
	CD->ImageSequences.Add(SourceB);

	UMetaHumanCalibrationGeneratorOptions* Options =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD);

	UTEST_NOT_NULL(TEXT("Options created"), Options);
	UTEST_EQUAL(TEXT("AOI count matches camera count"), Options->AreaOfInterestsForCameras.Num(), 2);

	// Both cameras are 1920x1080  - AOI should span the full frame.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FMetaHumanAreaOfInterest& AOI = Options->AreaOfInterestsForCameras[Index];
		TestEqual(FString::Printf(TEXT("Camera %d AOI TopLeft.X"), Index), AOI.TopLeft.X, 0.0);
		TestEqual(FString::Printf(TEXT("Camera %d AOI TopLeft.Y"), Index), AOI.TopLeft.Y, 0.0);
		TestEqual(FString::Printf(TEXT("Camera %d AOI BottomRight.X"), Index), AOI.BottomRight.X, 1920.0);
		TestEqual(FString::Printf(TEXT("Camera %d AOI BottomRight.Y"), Index), AOI.BottomRight.Y, 1080.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_AOI_DifferentResolutions,
	"MetaHuman.Calibration.Batch.ConstructOptions.AOI.DifferentResolutions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_AOI_DifferentResolutions::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::AutomationTransientDir() / TEXT("MHCalibBatchTest_AOIDiffRes");
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestDir, false, true); };

	UImgMediaSource* SourceA = MakeSyntheticImageSource(TestDir, TEXT("CameraA"), 1920, 1080);
	UImgMediaSource* SourceB = MakeSyntheticImageSource(TestDir, TEXT("CameraB"), 1280, 720);
	UTEST_NOT_NULL(TEXT("SourceA created"), SourceA);
	UTEST_NOT_NULL(TEXT("SourceB created"), SourceB);

	UFootageCaptureData* CD = MakeEmptyCaptureData();
	CD->ImageSequences.Add(SourceA);
	CD->ImageSequences.Add(SourceB);

	UMetaHumanCalibrationGeneratorOptions* Options =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD);

	UTEST_NOT_NULL(TEXT("Options created"), Options);
	UTEST_EQUAL(TEXT("AOI count matches camera count"), Options->AreaOfInterestsForCameras.Num(), 2);

	// Camera A: 1920x1080
	TestEqual(TEXT("CamA AOI BottomRight.X"), Options->AreaOfInterestsForCameras[0].BottomRight.X, 1920.0);
	TestEqual(TEXT("CamA AOI BottomRight.Y"), Options->AreaOfInterestsForCameras[0].BottomRight.Y, 1080.0);

	// Camera B: 1280x720
	TestEqual(TEXT("CamB AOI BottomRight.X"), Options->AreaOfInterestsForCameras[1].BottomRight.X, 1280.0);
	TestEqual(TEXT("CamB AOI BottomRight.Y"), Options->AreaOfInterestsForCameras[1].BottomRight.Y, 720.0);

	// Both should have zero TopLeft.
	TestEqual(TEXT("CamA AOI TopLeft.X"), Options->AreaOfInterestsForCameras[0].TopLeft.X, 0.0);
	TestEqual(TEXT("CamA AOI TopLeft.Y"), Options->AreaOfInterestsForCameras[0].TopLeft.Y, 0.0);
	TestEqual(TEXT("CamB AOI TopLeft.X"), Options->AreaOfInterestsForCameras[1].TopLeft.X, 0.0);
	TestEqual(TEXT("CamB AOI TopLeft.Y"), Options->AreaOfInterestsForCameras[1].TopLeft.Y, 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_AOI_NullImageSequenceAmongValid,
	"MetaHuman.Calibration.Batch.ConstructOptions.AOI.NullImageSequenceAmongValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_AOI_NullImageSequenceAmongValid::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::AutomationTransientDir() / TEXT("MHCalibBatchTest_AOINullAmong");
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestDir, false, true); };

	UImgMediaSource* SourceA = MakeSyntheticImageSource(TestDir, TEXT("CameraA"), 640, 480);
	UImgMediaSource* SourceC = MakeSyntheticImageSource(TestDir, TEXT("CameraC"), 800, 600);
	UTEST_NOT_NULL(TEXT("SourceA created"), SourceA);
	UTEST_NOT_NULL(TEXT("SourceC created"), SourceC);

	UFootageCaptureData* CD = MakeEmptyCaptureData();
	CD->ImageSequences.Add(SourceA);
	CD->ImageSequences.Add(nullptr);  // null entry in the middle
	CD->ImageSequences.Add(SourceC);

	// The null ImageSequence entry should produce a warning.
	AddExpectedError(TEXT("ImageSequence entry is null or invalid"), EAutomationExpectedErrorFlags::Contains);

	UMetaHumanCalibrationGeneratorOptions* Options =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD);

	UTEST_NOT_NULL(TEXT("Options created"), Options);
	UTEST_EQUAL(TEXT("AOI count matches ImageSequences count"), Options->AreaOfInterestsForCameras.Num(), 3);

	// First camera: 640x480
	TestEqual(TEXT("Cam0 AOI BottomRight.X"), Options->AreaOfInterestsForCameras[0].BottomRight.X, 640.0);
	TestEqual(TEXT("Cam0 AOI BottomRight.Y"), Options->AreaOfInterestsForCameras[0].BottomRight.Y, 480.0);

	// Second camera: null  - AOI should default to zero bounds.
	TestEqual(TEXT("Null cam AOI TopLeft.X"), Options->AreaOfInterestsForCameras[1].TopLeft.X, 0.0);
	TestEqual(TEXT("Null cam AOI TopLeft.Y"), Options->AreaOfInterestsForCameras[1].TopLeft.Y, 0.0);
	TestEqual(TEXT("Null cam AOI BottomRight.X"), Options->AreaOfInterestsForCameras[1].BottomRight.X, 0.0);
	TestEqual(TEXT("Null cam AOI BottomRight.Y"), Options->AreaOfInterestsForCameras[1].BottomRight.Y, 0.0);

	// Third camera: 800x600
	TestEqual(TEXT("Cam2 AOI BottomRight.X"), Options->AreaOfInterestsForCameras[2].BottomRight.X, 800.0);
	TestEqual(TEXT("Cam2 AOI BottomRight.Y"), Options->AreaOfInterestsForCameras[2].BottomRight.Y, 600.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOptions_AOI_EmptyImageSequenceDir,
	"MetaHuman.Calibration.Batch.ConstructOptions.AOI.EmptyImageSequenceDir",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConstructOptions_AOI_EmptyImageSequenceDir::RunTest(const FString& Parameters)
{
	const FString TestDir = FPaths::AutomationTransientDir() / TEXT("MHCalibBatchTest_AOIEmptyDir");
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestDir, false, true); };

	// Create the directory but don't put any image files in it.
	const FString EmptyDir = TestDir / TEXT("EmptyCamera");
	IFileManager::Get().MakeDirectory(*EmptyDir, true);

	UImgMediaSource* Source = NewObject<UImgMediaSource>();
	Source->SetSequencePath(EmptyDir);
	Source->FrameRateOverride = FFrameRate(30, 1);

	UFootageCaptureData* CD = MakeEmptyCaptureData();
	CD->ImageSequences.Add(Source);

	// GetImageSequenceInfoFromAsset will fail  - expect a warning.
	AddExpectedError(TEXT("Failed to get image sequence info"), EAutomationExpectedErrorFlags::Contains);

	UMetaHumanCalibrationGeneratorOptions* Options =
		UMetaHumanCalibrationBatchLibrary::ConstructDefaultOptionsForCaptureData(CD);

	UTEST_NOT_NULL(TEXT("Options created"), Options);
	UTEST_EQUAL(TEXT("AOI count is 1"), Options->AreaOfInterestsForCameras.Num(), 1);

	// Empty directory  - AOI should default to zero bounds.
	TestEqual(TEXT("AOI TopLeft.X"), Options->AreaOfInterestsForCameras[0].TopLeft.X, 0.0);
	TestEqual(TEXT("AOI TopLeft.Y"), Options->AreaOfInterestsForCameras[0].TopLeft.Y, 0.0);
	TestEqual(TEXT("AOI BottomRight.X"), Options->AreaOfInterestsForCameras[0].BottomRight.X, 0.0);
	TestEqual(TEXT("AOI BottomRight.Y"), Options->AreaOfInterestsForCameras[0].BottomRight.Y, 0.0);

	return true;
}

} // namespace UE::MetaHuman::CalibrationBatch::Tests

#endif // WITH_AUTOMATION_TESTS
