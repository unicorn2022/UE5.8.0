// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreDependencyViewer.h"
#include "SIoStoreDependencyViewer.h"
#include "RequiredProgramMainCPPInclude.h"
#include "StandaloneRenderer.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY(LogIoStoreDependencyViewer);

IMPLEMENT_APPLICATION(IoStoreDependencyViewer, "IoStoreDependencyViewer");

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FTaskTagScope Scope(ETaskTag::EGameThread);

	// Initialize core
	int32 PreInitResult = GEngineLoop.PreInit(ArgC, ArgV);
	if (PreInitResult != 0)
	{
		// PreInit failed - abort before using subsystems that assume successful initialization
		return PreInitResult;
	}

	// Track exit code for test modes
	int32 ExitCode = 0;
	bool bRunGuiMode = true;

	// Parse command line for initial path and test mode
	FString InitialPath;
	FParse::Value(FCommandLine::Get(), TEXT("Path="), InitialPath);

	// Check for test mode
	if (FParse::Param(FCommandLine::Get(), TEXT("TestOnDemandToc")))
	{
		FString TestPath = InitialPath.IsEmpty() ? TEXT("D:\\Fortnite4\\CookedBuild\\WindowsClient\\FortniteGame\\Content\\Paks") : InitialPath;
		TestOnDemandTocLoading(TestPath);
		// Note: Test function logs results; assume success if it returns without crashing
		ExitCode = 0;
		bRunGuiMode = false;
	}
	// Check for cloud download test mode
	else if (FParse::Param(FCommandLine::Get(), TEXT("TestCloudDownload")))
	{
		FString DownloadPath = InitialPath.IsEmpty() ? FPaths::Combine(FPaths::ProjectDir(), TEXT("CookedBuild/IoStoreDependencyViewer/TestDownload")) : InitialPath;
		TestCloudDownload(DownloadPath);
		// Note: Test function logs results; assume success if it returns without crashing
		ExitCode = 0;
		bRunGuiMode = false;
	}
	// Check for full cloud download test mode with specific parameters
	else if (FParse::Param(FCommandLine::Get(), TEXT("DownloadBuild")))
	{
		FString Namespace, Bucket, BuildId, DownloadPath;
		FParse::Value(FCommandLine::Get(), TEXT("namespace="), Namespace);
		FParse::Value(FCommandLine::Get(), TEXT("bucket="), Bucket);
		FParse::Value(FCommandLine::Get(), TEXT("build-id="), BuildId);
		FParse::Value(FCommandLine::Get(), TEXT("download-path="), DownloadPath);

		if (Namespace.IsEmpty() || Bucket.IsEmpty() || BuildId.IsEmpty())
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "DownloadBuild requires: -namespace, -bucket, -build-id");
			UE_LOGF(LogIoStoreDependencyViewer, Error, "Example: -DownloadBuild -namespace=fortnite.oplog -bucket=fortnitegame.staged-build.fortnite-dev-fn-40.windows-client -build-id=09ad702765a4b9f4cbd7c521");
			ExitCode = 1;
		}
		else
		{
			if (DownloadPath.IsEmpty())
			{
				DownloadPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("CookedBuild/IoStoreDependencyViewer/TestDownload"));
			}
			TestCloudDownloadSpecific(Namespace, Bucket, BuildId, DownloadPath);
			// Note: Test function logs results; assume success if it returns without crashing
			ExitCode = 0;
		}
		bRunGuiMode = false;
	}
	// Check for TOC to CSV test mode
	else if (FParse::Param(FCommandLine::Get(), TEXT("TestTocToCSV")))
	{
		TestTocToCSVFromCommandLine();
		// Note: Test function logs results; assume success if it returns without crashing
		ExitCode = 0;
		bRunGuiMode = false;
	}
	// Check for baseline UCAS reading test
	else if (FParse::Param(FCommandLine::Get(), TEXT("TestBaselineUcas")))
	{
		bool bSuccess = RunTestBaselineUcas();
		ExitCode = bSuccess ? 0 : 1;
		bRunGuiMode = false;
	}
	// Check for cloud partial download UCAS reading test
	else if (FParse::Param(FCommandLine::Get(), TEXT("TestCloudPartialDownloadUCas")))
	{
		bool bSuccess = RunTestCloudPartialDownloadUCas();
		ExitCode = bSuccess ? 0 : 1;
		bRunGuiMode = false;
	}
	// Check for zen reader integration test
	else if (FParse::Param(FCommandLine::Get(), TEXT("TestZenReaderIntegration")))
	{
		bool bSuccess = RunTestZenReaderIntegration();
		ExitCode = bSuccess ? 0 : 1;
		bRunGuiMode = false;
	}
	// Check for partial download test
	else if (FParse::Param(FCommandLine::Get(), TEXT("TestPartialDownload")))
	{
		TestPartialDownload();
		ExitCode = 0;
		bRunGuiMode = false;
	}

	// Run GUI mode if not in test mode
	if (bRunGuiMode)
	{
		// Initialize Slate application
		FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());

		// Create main window as a TSharedPtr so we can reset it later
		TSharedPtr<SWindow> MainWindow = SNew(SWindow)
			.Title(FText::FromString(TEXT("IoStore Dependency Viewer")))
			.ClientSize(FVector2D(1600, 900))
			.SupportsMaximize(true)
			.SupportsMinimize(true);

		// Create the dependency viewer widget
		MainWindow->SetContent(SNew(SIoStoreDependencyViewer)
			.InitialPath(InitialPath));

		// Show the window
		FSlateApplication::Get().AddWindow(MainWindow.ToSharedRef());

		// Run the main loop
		while (!IsEngineExitRequested())
		{
			BeginExitIfRequested();

			FSlateApplication::Get().PumpMessages();
			FSlateApplication::Get().Tick();
			FPlatformProcess::Sleep(0.01f);
		}

		// CRITICAL: Destroy widgets BEFORE Slate shutdown to prevent ICU crash
		// Clear the window content first
		if (MainWindow.IsValid())
		{
			MainWindow->SetContent(SNullWidget::NullWidget);

			// Request destroy of the main window
			FSlateApplication::Get().RequestDestroyWindow(MainWindow.ToSharedRef());
		}

		// Pump messages multiple times to ensure full destruction
		for (int32 i = 0; i < 5; ++i)
		{
			FSlateApplication::Get().PumpMessages();
			FSlateApplication::Get().Tick();
			FPlatformProcess::Sleep(0.01f);
		}

		// Clear the window reference to force destruction
		MainWindow.Reset();

		// One final pump to clean up
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();

		// Shutdown Slate
		FSlateApplication::Shutdown();
	}

	// Clean exit - ensure proper shutdown sequence for GUI and test modes
	// NOTE: PreInit failure path (line 22) intentionally skips this shutdown block,
	// as calling shutdown functions after failed initialization is unsafe
	FEngineLoop::AppPreExit();
	FModuleManager::Get().UnloadModulesAtShutdown();
	FEngineLoop::AppExit();

	return ExitCode;
}
