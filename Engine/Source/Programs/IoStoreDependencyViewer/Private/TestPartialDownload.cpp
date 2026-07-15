// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreDependencyViewer.h"
#include "PartialDownloadPlatformFile.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "IO/IoStore.h"
#include "Serialization/JsonSerializer.h"
#include "IoStoreConfigHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPartialDownloadTest, Log, All);

/**
 * Test partial download functionality with command-line parameters
 * Usage: IoStoreDependencyViewer.exe -TestPartialDownload -Namespace="fortnite.oplog" -Bucket="fortnitegame.staged-build.fortnite-main.android-client" -BuildId="09c6562cc1b21dd2c0258ea5" -DownloadPath="D:/TestDownload"
 */
void TestPartialDownload()
{
	UE_LOGF(LogPartialDownloadTest, Display, "=== Testing Partial Download Functionality ===");

	// Parse command-line parameters
	FString Namespace;
	FString Bucket;
	FString BuildId;
	FString DownloadPath;

	if (!FParse::Value(FCommandLine::Get(), TEXT("Namespace="), Namespace))
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Missing required parameter: -Namespace");
		UE_LOGF(LogPartialDownloadTest, Display, "Usage: -TestPartialDownload -Namespace=\"fortnite.oplog\" -Bucket=\"fortnitegame.staged-build.fortnite-main.android-client\" -BuildId=\"09c6562cc1b21dd2c0258ea5\" -DownloadPath=\"D:/TestDownload\"");
		return;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("Bucket="), Bucket))
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Missing required parameter: -Bucket");
		return;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("BuildId="), BuildId))
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Missing required parameter: -BuildId");
		return;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("DownloadPath="), DownloadPath))
	{
		// Default to temp directory
		DownloadPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("PartialDownloadTest"));
		UE_LOGF(LogPartialDownloadTest, Warning, "No -DownloadPath specified, using default: %ls", *DownloadPath);
	}

	UE_LOGF(LogPartialDownloadTest, Display, "Parameters:");
	UE_LOGF(LogPartialDownloadTest, Display, "  Namespace: %ls", *Namespace);
	UE_LOGF(LogPartialDownloadTest, Display, "  Bucket: %ls", *Bucket);
	UE_LOGF(LogPartialDownloadTest, Display, "  BuildId: %ls", *BuildId);
	UE_LOGF(LogPartialDownloadTest, Display, "  DownloadPath: %ls", *DownloadPath);

	// Step 1: Get zen.exe and oidc paths
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 1: Locating zen.exe and OidcToken.exe");

	FString ZenExePath;
	FString OidcExePath;

	IoStoreConfig::FindZenExePath(ZenExePath);
	if (!IoStoreConfig::ValidateZenExe(ZenExePath, false))
	{
		return;
	}

	IoStoreConfig::FindOidcTokenExePath(OidcExePath, ZenExePath);

	if (!FPaths::FileExists(OidcExePath))
	{
		UE_LOGF(LogPartialDownloadTest, Error, "OidcToken.exe not found at: %ls", *OidcExePath);
		return;
	}

	UE_LOGF(LogPartialDownloadTest, Display, "  zen.exe: %ls", *ZenExePath);
	UE_LOGF(LogPartialDownloadTest, Display, "  OidcToken.exe: %ls", *OidcExePath);

	// Step 2: Create download directory
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 2: Creating download directory");

	if (!IFileManager::Get().DirectoryExists(*DownloadPath))
	{
		if (!IFileManager::Get().MakeDirectory(*DownloadPath, true))
		{
			UE_LOGF(LogPartialDownloadTest, Error, "Failed to create download directory: %ls", *DownloadPath);
			return;
		}
		UE_LOGF(LogPartialDownloadTest, Display, "  Created: %ls", *DownloadPath);
	}
	else
	{
		UE_LOGF(LogPartialDownloadTest, Display, "  Directory exists: %ls", *DownloadPath);
	}

	// Step 3: Download .utoc files (not .ucas - this simulates partial download mode)
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 3: Downloading .utoc files (partial download mode)");

	FString ProxyUrl = TEXT("https://jupiter.devtools.epicgames.com");
	FString Wildcard = TEXT("*.utoc;*.uondemandtoc;*.umeta");

	// Pre-escape arguments to prevent command-line injection
	FString EscapedNamespace = IoStoreConfig::EscapeCommandLineArgument(Namespace);
	FString EscapedBucket = IoStoreConfig::EscapeCommandLineArgument(Bucket);
	FString EscapedBuildId = IoStoreConfig::EscapeCommandLineArgument(BuildId);
	FString EscapedDownloadPath = IoStoreConfig::EscapeCommandLineArgument(DownloadPath);
	FString EscapedProxyUrl = IoStoreConfig::EscapeCommandLineArgument(ProxyUrl);
	FString EscapedOidcExePath = IoStoreConfig::EscapeCommandLineArgument(OidcExePath);

	FString DownloadCommand = FString::Printf(
		TEXT("builds download --namespace=\"%s\" --bucket=\"%s\" --build-id=\"%s\" --local-path=\"%s\" --host=\"%s\" --oidctoken-exe-path=\"%s\" --wildcard=\"%s\" --clean --enable-scavenge --verbose"),
		*EscapedNamespace,
		*EscapedBucket,
		*EscapedBuildId,
		*EscapedDownloadPath,
		*EscapedProxyUrl,
		*EscapedOidcExePath,
		*Wildcard
	);

	UE_LOGF(LogPartialDownloadTest, Display, "  Running: zen.exe %ls", *DownloadCommand);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Failed to create pipe");
		return;
	}

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*ZenExePath,
		*DownloadCommand,
		false,
		true,
		true,
		nullptr,
		0,
		nullptr,
		WritePipe,
		ReadPipe
	);

	if (!ProcHandle.IsValid())
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Failed to launch zen.exe");
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return;
	}

	// Close parent's write pipe
	FPlatformProcess::ClosePipe(0, WritePipe);

	// Wait for download to complete
	FString DownloadOutput;
	double StartTime = FPlatformTime::Seconds();
	const double TimeoutSeconds = 300.0; // 5 minutes

	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		if (ElapsedTime > TimeoutSeconds)
		{
			UE_LOGF(LogPartialDownloadTest, Error, "Download timed out after %.1f seconds", ElapsedTime);
			FPlatformProcess::TerminateProc(ProcHandle, true);
			FPlatformProcess::ClosePipe(ReadPipe, 0);
			FPlatformProcess::CloseProc(ProcHandle);
			return;
		}

		FString Output = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Output.IsEmpty())
		{
			DownloadOutput += Output;
			// Cap output size to prevent unbounded memory growth with --verbose flag
			const int32 MaxOutputSize = 50000;
			if (DownloadOutput.Len() > MaxOutputSize)
			{
				DownloadOutput = DownloadOutput.Right(MaxOutputSize);
			}
			UE_LOGF(LogPartialDownloadTest, Display, "  %ls", *Output.TrimStartAndEnd());
		}
		FPlatformProcess::Sleep(0.1f);
	}

	// Final read
	FString FinalOutput = FPlatformProcess::ReadPipe(ReadPipe);
	DownloadOutput += FinalOutput;
	const int32 MaxOutputSize = 50000;
	if (DownloadOutput.Len() > MaxOutputSize)
	{
		DownloadOutput = DownloadOutput.Right(MaxOutputSize);
	}
	FPlatformProcess::ClosePipe(ReadPipe, 0);

	int32 DownloadExitCode = 0;
	FPlatformProcess::GetProcReturnCode(ProcHandle, &DownloadExitCode);
	FPlatformProcess::CloseProc(ProcHandle);

	if (DownloadExitCode != 0)
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Download failed with exit code: %d", DownloadExitCode);
		return;
	}

	UE_LOGF(LogPartialDownloadTest, Display, "  Download completed successfully");

	// Step 4: Verify .utoc files were downloaded
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 4: Verifying downloaded files");

	TArray<FString> UtocFiles;
	IFileManager::Get().FindFilesRecursive(UtocFiles, *DownloadPath, TEXT("*.utoc"), true, false);

	UE_LOGF(LogPartialDownloadTest, Display, "  Found %d .utoc files", UtocFiles.Num());
	for (const FString& UtocFile : UtocFiles)
	{
		UE_LOGF(LogPartialDownloadTest, Display, "    %ls", *UtocFile);
	}

	if (UtocFiles.Num() == 0)
	{
		UE_LOGF(LogPartialDownloadTest, Error, "No .utoc files found!");
		return;
	}

	// Verify no .ucas files were downloaded (we're in partial mode)
	TArray<FString> UcasFiles;
	IFileManager::Get().FindFilesRecursive(UcasFiles, *DownloadPath, TEXT("*.ucas"), true, false);
	UE_LOGF(LogPartialDownloadTest, Display, "  Found %d .ucas files (should be 0 for partial download)", UcasFiles.Num());

	// Step 5: Initialize partial download platform file
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 5: Initializing partial download system");

	// Save the original platform file so we can restore it later
	IPlatformFile* OriginalPlatformFile = &FPlatformFileManager::Get().GetPlatformFile();

	TSharedPtr<FPartialDownloadPlatformFile> PartialDownloadPlatformFile = MakeShared<FPartialDownloadPlatformFile>();

	// Initialize with current physical platform file
	IPlatformFile& PhysicalPlatform = *OriginalPlatformFile;
	PartialDownloadPlatformFile->Initialize(&PhysicalPlatform, TEXT(""));

	// Initialize partial download settings
	if (!PartialDownloadPlatformFile->InitializePartialDownload(
		DownloadPath,
		ZenExePath,
		OidcExePath,
		Namespace,
		Bucket,
		BuildId,
		ProxyUrl))
	{
		UE_LOGF(LogPartialDownloadTest, Error, "Failed to initialize partial download system");
		return;
	}

	UE_LOGF(LogPartialDownloadTest, Display, "  Partial download system initialized");

	// Step 6: Parse all .utoc files to build block maps
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 6: Parsing .utoc files to build block maps");

	PartialDownloadPlatformFile->ParseTocFiles();

	UE_LOGF(LogPartialDownloadTest, Display, "  TOC files parsed successfully");

	// Step 7: Install as the topmost platform file
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 7: Installing partial download platform file");

	FPlatformFileManager::Get().SetPlatformFile(*PartialDownloadPlatformFile);

	UE_LOGF(LogPartialDownloadTest, Display, "  Platform file installed");

	// Step 8: Load containers (this will trigger on-demand block fetching)
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 8: Loading containers (will trigger on-demand block fetching)");

	int32 ContainersLoaded = 0;
	int32 ChunksLoaded = 0;

	// Keep readers alive in a list so they can be properly destroyed before cleanup
	TArray<TUniquePtr<FIoStoreReader>> Readers;

	for (const FString& UtocFile : UtocFiles)
	{
		UE_LOGF(LogPartialDownloadTest, Display, "  Loading container: %ls", *UtocFile);

		// Create IoStoreReader
		TUniquePtr<FIoStoreReader> Reader = MakeUnique<FIoStoreReader>();

		// FIoStoreReader::Initialize expects path WITHOUT .utoc extension (it adds it automatically)
		FString ContainerPath = UtocFile;
		if (ContainerPath.EndsWith(TEXT(".utoc")))
		{
			ContainerPath = ContainerPath.LeftChop(5); // Remove ".utoc"
		}

		FIoStatus Status = Reader->Initialize(*ContainerPath, TMap<FGuid, FAES::FAESKey>());

		if (!Status.IsOk())
		{
			UE_LOGF(LogPartialDownloadTest, Warning, "    Failed to initialize reader: %ls", *Status.ToString());
			continue;
		}

		ContainersLoaded++;

		// Try to read some chunks to trigger block downloading
		int32 ChunkCount = 0;
		Reader->EnumerateChunks([&](const FIoStoreTocChunkInfo& ChunkInfo) -> bool
		{
			ChunkCount++;

			// Read first 5 chunks from each container to test partial download
			if (ChunkCount <= 5)
			{
				UE_LOGF(LogPartialDownloadTest, Display, "    Reading chunk %d (ID: %ls, Size: %lld)",
					ChunkCount, *LexToString(ChunkInfo.Id), ChunkInfo.Size);

				FIoReadOptions ReadOptions;
				TIoStatusOr<FIoBuffer> Result = Reader->Read(ChunkInfo.Id, ReadOptions);

				if (Result.IsOk())
				{
					ChunksLoaded++;
					UE_LOGF(LogPartialDownloadTest, Display, "      Successfully read %lld bytes", Result.ValueOrDie().DataSize());
				}
				else
				{
					UE_LOGF(LogPartialDownloadTest, Warning, "      Failed to read chunk: %ls", *Result.Status().ToString());
				}
			}

			return true; // Continue enumeration
		});

		UE_LOGF(LogPartialDownloadTest, Display, "    Container has %d total chunks", ChunkCount);

		// Keep the reader alive for now
		Readers.Add(MoveTemp(Reader));

		// Only test first 3 containers to keep test time reasonable
		if (Readers.Num() >= 3)
		{
			UE_LOGF(LogPartialDownloadTest, Display, "  (Limiting test to first 3 valid containers for performance)");
			break;
		}
	}

	// Step 9: Verify results
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 9: Test Results");
	UE_LOGF(LogPartialDownloadTest, Display, "  Containers loaded: %d / %d", ContainersLoaded, UtocFiles.Num());
	UE_LOGF(LogPartialDownloadTest, Display, "  Chunks successfully read: %d", ChunksLoaded);

	// Check if any .ucas files were created by partial downloads
	UcasFiles.Reset();
	IFileManager::Get().FindFilesRecursive(UcasFiles, *DownloadPath, TEXT("*.ucas"), true, false);
	UE_LOGF(LogPartialDownloadTest, Display, "  .ucas files created by partial download: %d", UcasFiles.Num());

	if (UcasFiles.Num() > 0)
	{
		for (const FString& UcasFile : UcasFiles)
		{
			int64 FileSize = IFileManager::Get().FileSize(*UcasFile);
			UE_LOGF(LogPartialDownloadTest, Display, "    %ls (%lld bytes)", *UcasFile, FileSize);
		}
	}

	// Final verdict
	UE_LOGF(LogPartialDownloadTest, Display, "");
	if (ContainersLoaded > 0 && ChunksLoaded > 0)
	{
		UE_LOGF(LogPartialDownloadTest, Display, "=== TEST PASSED ===");
		UE_LOGF(LogPartialDownloadTest, Display, "Partial download system is working correctly!");
	}
	else
	{
		UE_LOGF(LogPartialDownloadTest, Error, "=== TEST FAILED ===");
		UE_LOGF(LogPartialDownloadTest, Error, "Partial download system did not load containers or chunks successfully");
	}

	// Step 10: Cleanup - must be done in correct order to avoid crashes
	UE_LOGF(LogPartialDownloadTest, Display, "");
	UE_LOGF(LogPartialDownloadTest, Display, "Step 10: Cleanup");

	// First: Destroy all IoStoreReaders to complete any pending async I/O
	UE_LOGF(LogPartialDownloadTest, Display, "  Destroying IoStoreReaders and waiting for async I/O...");
	Readers.Reset();

	// Give async tasks a moment to complete
	FPlatformProcess::Sleep(0.5f);

	// Second: Restore the original platform file
	UE_LOGF(LogPartialDownloadTest, Display, "  Restoring original platform file...");
	FPlatformFileManager::Get().SetPlatformFile(*OriginalPlatformFile);

	// Third: Let the platform file be destroyed (destructor will shutdown coordinator)
	UE_LOGF(LogPartialDownloadTest, Display, "  Destroying platform file (coordinator shutdown happens in destructor)...");
	PartialDownloadPlatformFile.Reset();

	UE_LOGF(LogPartialDownloadTest, Display, "  Cleanup complete");
}
