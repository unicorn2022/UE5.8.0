// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Settings/CaptureManagerEditorSettings.h"
#include "Tests/AutomationCommon.h"
#include "Modules/ModuleManager.h"
#include "ContentBrowserModule.h"

BEGIN_DEFINE_SPEC(
	FCaptureManagerEditorSettingsSpec,
	"CaptureManager.Settings.EditorSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

UCaptureManagerEditorSettings* TestSettings;
FString OriginalImportPath;
FString OriginalMediaPath;

END_DEFINE_SPEC(FCaptureManagerEditorSettingsSpec)


void FCaptureManagerEditorSettingsSpec::Define()
{
	Describe("CaptureManagerEditorSettings", [this]
	{
		BeforeEach([this]
		{
			TestSettings = GetMutableDefault<UCaptureManagerEditorSettings>();

			// Fail the test immediately if settings object cannot be retrieved
			if (!TestSettings)
			{
				AddError(TEXT("Failed to retrieve UCaptureManagerEditorSettings - module may not be loaded"));
					return;
			}

			OriginalImportPath = TestSettings->ImportDirectory.Path;
			OriginalMediaPath = TestSettings->MediaDirectory.Path;
			
		});

		AfterEach([this]
		{
			if (TestSettings)
			{
				TestSettings->ImportDirectory.Path = OriginalImportPath;
				TestSettings->MediaDirectory.Path = OriginalMediaPath;
			}
		});

		Describe("SetImportDirectory", [this]
		{
			It("should accept valid /Game paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("/Game/CaptureManager/ValidTest");

				TestSettings->SetImportDirectory(ValidPath);

				TestEqual(TEXT("Import directory should be set to valid path"),
					TestSettings->ImportDirectory.Path,
					ValidPath.Path);
			});

			It("should accept valid /VerseDevices paths (UEFN)", [this]
			{
				// Skip this test if /VerseDevices is not a valid package root
				if (!FPackageName::IsValidLongPackageName(TEXT("/VerseDevices/Test"), false))
				{
					AddInfo(TEXT("Skipping test: /VerseDevices is not a valid package root in this environment(requires UEFN)"));
					return;
				}

				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("/VerseDevices/CaptureManager/ValidTest");

				TestSettings->SetImportDirectory(ValidPath);

				TestEqual(TEXT("Import directory should accept UEFN content root"),
					TestSettings->ImportDirectory.Path,
					ValidPath.Path);
			});

			It("should reject paths with trailing slashes", [this]
			{
				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("/Game/CaptureManager/Test/");

				FString OriginalPath = TestSettings->ImportDirectory.Path;

				AddExpectedError(TEXT("Import Directory path cannot end with a trailing slash"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject empty paths", [this]
			{
				FDirectoryPath EmptyPath;
				EmptyPath.Path = TEXT("");

				FString OriginalPath = TestSettings->ImportDirectory.Path;

				AddExpectedError(TEXT("Import Directory cannot be empty"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(EmptyPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject read-only content roots like /Engine", [this]
			{
				FString OriginalPath = TestSettings->ImportDirectory.Path;

				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("/Engine/Content/Test");

				AddExpectedError(TEXT("Import Directory cannot be in a read-only content root"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject read-only content roots like /Script", [this]
			{
				FString OriginalPath = TestSettings->ImportDirectory.Path;

				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("/Script/SomeModule");

				AddExpectedError(TEXT("Import Directory"), EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject read-only content roots like /Temp", [this]
			{
				FString OriginalPath = TestSettings->ImportDirectory.Path;

				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("/Temp/TempFolder");

				AddExpectedError(TEXT("Import Directory"), EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject invalid package paths (filesystem paths)", [this]
			{
				FString OriginalPath = TestSettings->ImportDirectory.Path;
					
				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("C:/SomeFolder/Test");

				AddExpectedError(TEXT("Import Directory must be a valid package path"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject invalid package paths (relative paths)", [this]
			{
				FString OriginalPath = TestSettings->ImportDirectory.Path;

				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("../Content/Test");

				AddExpectedError(TEXT("Import Directory must be a valid package path"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should reject Unix filesystem paths", [this]
			{
				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("/usr/local/test");

				AddExpectedError(TEXT("Import Directory must be a valid package path"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid Unix path"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});

			It("should accept plugin content roots", [this]
			{
				// Check if a test plugin path is valid in this environment
				FString TestPluginPath = TEXT("/CaptureManagerCore/Content/Test");

				if (!FPackageName::IsValidLongPackageName(TestPluginPath, false))
				{
					AddInfo(TEXT("Skipping test: Test plugin path is not available in this environment"));
					return;
				}

				FDirectoryPath ValidPath;
				ValidPath.Path = TestPluginPath;

				TestSettings->SetImportDirectory(ValidPath);

				TestEqual(TEXT("Import directory should accept plugin paths"),
					TestSettings->ImportDirectory.Path,
					ValidPath.Path);
			});

			It("should persist changes correctly", [this]
			{
				FDirectoryPath NewPath;
				NewPath.Path = TEXT("/Game/TestPersistence");

				TestSettings->SetImportDirectory(NewPath);

				TestEqual(TEXT("Import directory should be persisted"),
					TestSettings->ImportDirectory.Path,
					NewPath.Path);

				FDirectoryPath AnotherPath;
				AnotherPath.Path = TEXT("/Game/AnotherLocation");

				TestSettings->SetImportDirectory(AnotherPath);

				TestEqual(TEXT("Import directory should update to new value"),
					TestSettings->ImportDirectory.Path,
					AnotherPath.Path);
			});
		});

		Describe("SetMediaDirectory", [this]
		{
			It("should accept relative paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("../Saved/Captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept relative paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});

			It("should accept empty paths", [this]
			{
				FDirectoryPath EmptyPath;
				EmptyPath.Path = TEXT("");

				TestSettings->SetMediaDirectory(EmptyPath);

				TestEqual(TEXT("Media directory should accept empty paths"),
					TestSettings->MediaDirectory.Path,
					EmptyPath.Path);
			});

			It("should persist changes correctly", [this]
			{
				FDirectoryPath NewPath;
				NewPath.Path = TEXT("../Saved/NewMediaLocation");

				TestSettings->SetMediaDirectory(NewPath);

				TestEqual(TEXT("Media directory should be persisted"),
					TestSettings->MediaDirectory.Path,
					NewPath.Path);

				FDirectoryPath AnotherPath;
				AnotherPath.Path = TEXT("../Saved/AnotherMediaLocation");

				TestSettings->SetMediaDirectory(AnotherPath);

				TestEqual(TEXT("Media directory should update to new value"),
					TestSettings->MediaDirectory.Path,
					AnotherPath.Path);
			});

#if PLATFORM_WINDOWS
			It("should accept Windows absolute paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("C:/MediaStorage/Captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept Windows absolute paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});

			It("should accept Windows UNC network paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("\\\\NetworkShare\\Captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept Windows UNC network paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});
#endif

#if PLATFORM_MAC
			It("should accept Mac absolute paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("/Users/username/Captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept Mac absolute paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});

			It("should accept Mac external drive paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("/Volumes/ExternalDrive/Captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept Mac external drive paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});
#endif

#if PLATFORM_LINUX
			It("should accept Linux absolute paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("/home/user/captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept Linux absolute paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});

			It("should accept Linux mount paths", [this]
			{
				FDirectoryPath ValidPath;
				ValidPath.Path = TEXT("/mnt/storage/captures");

				TestSettings->SetMediaDirectory(ValidPath);

				TestEqual(TEXT("Media directory should accept Linux mount paths"),
					TestSettings->MediaDirectory.Path,
					ValidPath.Path);
			});
#endif
		});

		Describe("GetCaptureManagerEditorSettings", [this]
		{
			It("should return a valid settings object", [this]
			{
				UCaptureManagerEditorSettings* Settings =
					UCaptureManagerEditorSettings::GetCaptureManagerEditorSettings();

				TestNotNull(TEXT("GetCaptureManagerEditorSettings should return valid object"),
					Settings);
			});

			It("should return the same instance on multiple calls (singleton)", [this]
			{
				UCaptureManagerEditorSettings* Settings1 =
					UCaptureManagerEditorSettings::GetCaptureManagerEditorSettings();
				UCaptureManagerEditorSettings* Settings2 =
					UCaptureManagerEditorSettings::GetCaptureManagerEditorSettings();

				TestEqual(TEXT("GetCaptureManagerEditorSettings should return singleton"),
					Settings1,
					Settings2);
			});

			It("should return same instance as GetMutableDefault", [this]
			{
				UCaptureManagerEditorSettings* StaticGetterSettings =
					UCaptureManagerEditorSettings::GetCaptureManagerEditorSettings();
				UCaptureManagerEditorSettings* MutableDefaultSettings =
					GetMutableDefault<UCaptureManagerEditorSettings>();

				TestEqual(TEXT("Static getter should return same instance as GetMutableDefault"),
					StaticGetterSettings,
					MutableDefaultSettings);
			});

			It("should return same instance as used in tests", [this]
			{
				UCaptureManagerEditorSettings* StaticGetterSettings =
					UCaptureManagerEditorSettings::GetCaptureManagerEditorSettings();

				TestEqual(TEXT("Static getter should return same instance as TestSettings"),
					StaticGetterSettings,
					TestSettings);
			});
		});

		Describe("Edge Cases", [this]
		{
			It("should handle rapid successive changes to ImportDirectory", [this]
			{
				// Rapidly change ImportDirectory multiple times
				for (int32 i = 0; i < 10; i++)
				{
					FDirectoryPath NewPath;
					NewPath.Path = FString::Printf(TEXT("/Game/RapidChange_%d"), i);
					TestSettings->SetImportDirectory(NewPath);
				}

				TestEqual(TEXT("Last rapid change should be persisted"),
					TestSettings->ImportDirectory.Path,
					TEXT("/Game/RapidChange_9"));
			});

			It("should handle rapid successive changes to MediaDirectory", [this]
			{
				// Rapidly change MediaDirectory multiple times
				for (int32 i = 0; i < 10; i++)
				{
					FDirectoryPath NewPath;
					NewPath.Path = FString::Printf(TEXT("../Saved/RapidChange_%d"), i);
					TestSettings->SetMediaDirectory(NewPath);
				}

				TestEqual(TEXT("Last rapid media change should be persisted"),
					TestSettings->MediaDirectory.Path,
					TEXT("../Saved/RapidChange_9"));
			});

			It("should handle very long ImportDirectory paths", [this]
			{
				FString LongPath = TEXT("/Game");
				for (int32 i = 0; i < 20; i++)
				{
					LongPath += TEXT("/VeryLongFolderName");
				}

				FDirectoryPath ValidPath;
				ValidPath.Path = LongPath;

				TestSettings->SetImportDirectory(ValidPath);

				TestEqual(TEXT("Very long path should be accepted"),
					TestSettings->ImportDirectory.Path,
					LongPath);
			});

			It("should handle paths with special characters in ImportDirectory", [this]
			{
				FDirectoryPath PathWithUnderscore;
				PathWithUnderscore.Path = TEXT("/Game/Folder_With_Underscores");

				TestSettings->SetImportDirectory(PathWithUnderscore);

				TestEqual(TEXT("Path with underscores should be accepted"),
					TestSettings->ImportDirectory.Path,
					PathWithUnderscore.Path);
			});

			It("should handle paths with numbers in ImportDirectory", [this]
			{
				FDirectoryPath PathWithNumbers;
				PathWithNumbers.Path = TEXT("/Game/Folder123/Test456");

				TestSettings->SetImportDirectory(PathWithNumbers);

				TestEqual(TEXT("Path with numbers should be accepted"),
					TestSettings->ImportDirectory.Path,
					PathWithNumbers.Path);
			});

			It("should reject ImportDirectory paths with invalid characters", [this]
			{
				FDirectoryPath InvalidPath;
				InvalidPath.Path = TEXT("/Game/Invalid*Path");

				FString OriginalPath = TestSettings->ImportDirectory.Path;

				AddExpectedError(TEXT("Import Directory must be a valid package path"),
					EAutomationExpectedErrorFlags::Contains, 1);

				TestSettings->SetImportDirectory(InvalidPath);

				TestFalse(TEXT("Import directory should not be empty after reset"),
					TestSettings->ImportDirectory.Path.IsEmpty());

				TestTrue(TEXT("Import directory should be reset to default after invalid input"),
					TestSettings->ImportDirectory.Path.Contains(TEXT("CaptureManager/Imports")));
			});
		});
	});
}

