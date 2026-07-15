// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreDependencyViewer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "IO/IoStore.h"
#include "IO/IoDispatcher.h"
#include "IoStoreConfigHelpers.h"

int32 RunZenCommand(const FString& ZenExePath, const FString& Command, FString& OutResult)
{
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Running zen.exe: %ls", *Command);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to create pipe for zen.exe process (platform limitation or resource exhaustion)");
		return -1;
	}

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*ZenExePath,
		*Command,
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
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to launch zen.exe");
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return -1;
	}

	// Close parent's copy of write pipe - child has its own copy
	// This ensures proper EOF detection when child closes its write handle
	FPlatformProcess::ClosePipe(0, WritePipe);

	// Track progress
	double StartTime = FPlatformTime::Seconds();
	double LastProgressTime = StartTime;
	int32 ProgressCounter = 0;
	int32 LastKnownPercentage = 0;

	// Timeout for zen.exe operations (2 hours for large cloud downloads)
	const double TimeoutSeconds = 2.0 * 60.0 * 60.0;  // 2 hours

	// Wait for process to complete (with timeout to prevent indefinite hangs)
	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		FPlatformProcess::Sleep(0.1f);

		// Check for timeout to prevent indefinite hangs
		double CurrentTime = FPlatformTime::Seconds();
		double ElapsedTime = CurrentTime - StartTime;
		if (ElapsedTime > TimeoutSeconds)
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "zen.exe operation timed out after %.0f seconds (%.1f hours)",
				ElapsedTime, ElapsedTime / 3600.0);
			UE_LOGF(LogIoStoreDependencyViewer, Error, "Terminating zen.exe process to prevent indefinite hang");

			// Terminate the process
			FPlatformProcess::TerminateProc(ProcHandle, true);
			FPlatformProcess::ClosePipe(ReadPipe, 0);  // WritePipe already closed
			FPlatformProcess::CloseProc(ProcHandle);

			return -1;  // Return error code
		}

		// Read output
		FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
		if (!NewOutput.IsEmpty())
		{
			OutResult += NewOutput;
			// Limit OutResult size to prevent OOM on very verbose/long-running commands
			// Keep only the last 100KB of output (approximately 50,000 characters)
			const int32 MaxOutputSize = 50000;
			if (OutResult.Len() > MaxOutputSize)
			{
				OutResult = OutResult.Right(MaxOutputSize);
			}
			// Use Display level so output is visible
			UE_LOGF(LogIoStoreDependencyViewer, Display, "%ls", *NewOutput.TrimStartAndEnd());
			LastProgressTime = CurrentTime;
			ProgressCounter = 0;

			// Try to parse percentage from zen.exe output
			// Format: "Writing chunks    27% (40.3s): ..."
			int32 PercentIndex = NewOutput.Find(TEXT("%"));
			if (PercentIndex != INDEX_NONE && PercentIndex > 0)
			{
				// Search backwards for the percentage number
				int32 StartIndex = PercentIndex - 1;
				while (StartIndex > 0 && (FChar::IsDigit(NewOutput[StartIndex]) || FChar::IsWhitespace(NewOutput[StartIndex])))
				{
					StartIndex--;
				}
				StartIndex++; // Move back to first digit

				FString PercentStr = NewOutput.Mid(StartIndex, PercentIndex - StartIndex).TrimStartAndEnd();
				if (!PercentStr.IsEmpty() && PercentStr.IsNumeric())
				{
					LastKnownPercentage = FCString::Atoi(*PercentStr);
				}
			}
		}
		else
		{
			// Show progress indicator if no output for a while
			// CurrentTime already declared above for timeout check
			if (CurrentTime - LastProgressTime > 5.0)
			{
				int32 ElapsedSeconds = (int32)(CurrentTime - StartTime);
				int32 Minutes = ElapsedSeconds / 60;
				int32 Seconds = ElapsedSeconds % 60;

				const TCHAR* AnimChars[] = { TEXT("|"), TEXT("/"), TEXT("-"), TEXT("\\") };
				if (LastKnownPercentage > 0)
				{
					UE_LOGF(LogIoStoreDependencyViewer, Display, "  %ls %d%% - Still working... %02d:%02d elapsed",
						AnimChars[ProgressCounter % 4], LastKnownPercentage, Minutes, Seconds);
				}
				else
				{
					UE_LOGF(LogIoStoreDependencyViewer, Display, "  %ls Still working... %02d:%02d elapsed",
						AnimChars[ProgressCounter % 4], Minutes, Seconds);
				}

				LastProgressTime = CurrentTime;
				ProgressCounter++;
			}
		}
	}

	// Read any remaining output
	FString FinalOutput = FPlatformProcess::ReadPipe(ReadPipe);
	if (!FinalOutput.IsEmpty())
	{
		OutResult += FinalOutput;
		// Apply same size limit to final output
		const int32 MaxOutputSize = 50000;
		if (OutResult.Len() > MaxOutputSize)
		{
			OutResult = OutResult.Right(MaxOutputSize);
		}
		UE_LOGF(LogIoStoreDependencyViewer, Display, "%ls", *FinalOutput.TrimStartAndEnd());
	}

	int32 ReturnCode = 0;
	FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);

	double TotalTime = FPlatformTime::Seconds() - StartTime;
	int32 TotalMinutes = (int32)TotalTime / 60;
	int32 TotalSeconds = (int32)TotalTime % 60;
	UE_LOGF(LogIoStoreDependencyViewer, Display, "zen.exe completed in %02d:%02d (exit code: %d)",
		TotalMinutes, TotalSeconds, ReturnCode);

	FPlatformProcess::CloseProc(ProcHandle);
	FPlatformProcess::ClosePipe(ReadPipe, 0);  // WritePipe already closed

	return ReturnCode;
}

void TestCloudDownload(const FString& DownloadPath)
{
	UE_LOGF(LogIoStoreDependencyViewer, Display, "=== Testing Cloud Download ===");
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Download Path: %ls", *DownloadPath);

	// Get zen.exe and OidcToken.exe paths
	FString ZenExePath;
	FString OidcExePath;
	IoStoreConfig::FindZenExePath(ZenExePath);
	IoStoreConfig::FindOidcTokenExePath(OidcExePath, ZenExePath);

	// Load Engine.ini to get host URL from [StorageServers] Cloud setting
	FString ProxyUrl;
	// Read Cloud setting from [StorageServers] section
	FString CloudSetting;
	if (GConfig && GConfig->GetString(TEXT("StorageServers"), TEXT("Cloud"), CloudSetting, GEngineIni))
	{
		// Parse the Host= field from the Cloud setting
		int32 HostStartIdx = CloudSetting.Find(TEXT("Host=\""));
		if (HostStartIdx != INDEX_NONE)
		{
			HostStartIdx += 6; // Skip 'Host="'
			int32 HostEndIdx = CloudSetting.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, HostStartIdx);
			if (HostEndIdx != INDEX_NONE)
			{
				FString HostUrls = CloudSetting.Mid(HostStartIdx, HostEndIdx - HostStartIdx);
				// Split by semicolon and use first URL
				TArray<FString> Urls;
				HostUrls.ParseIntoArray(Urls, TEXT(";"));
				if (Urls.Num() > 0)
				{
					ProxyUrl = Urls[0];
				}
			}
		}
	}

	// Fallback to default if not found
	if (ProxyUrl.IsEmpty())
	{
		ProxyUrl = TEXT("https://jupiter.devtools.epicgames.com");
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Settings:");
	UE_LOGF(LogIoStoreDependencyViewer, Display, "  ZenExe: %ls", *ZenExePath);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "  OidcExe: %ls", *OidcExePath);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "  Host: %ls", *ProxyUrl);

	// Validate zen.exe exists
	if (!FPaths::FileExists(ZenExePath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "zen.exe not found at: %ls", *ZenExePath);
		return;
	}

	if (!FPaths::FileExists(OidcExePath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "OidcToken.exe not found at: %ls", *OidcExePath);
		return;
	}

	// Ensure intermediate directory exists before creating temp file paths
	FString IntermediateDir = FPaths::ProjectIntermediateDir();
	if (!IFileManager::Get().DirectoryExists(*IntermediateDir))
	{
		IFileManager::Get().MakeDirectory(*IntermediateDir, true);
	}

	// Query namespaces from zen.exe
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Querying namespaces...");
	FString NamespacesResultPath = FPaths::CreateTempFilename(*IntermediateDir, TEXT("namespaces_"), TEXT(".json"));
	FString NamespacesCommand = FString::Printf(
		TEXT("builds list-namespaces --recursive --host \"%s\" --oidctoken-exe-path \"%s\" --result-path \"%s\""),
		*IoStoreConfig::EscapeCommandLineArgument(ProxyUrl),
		*IoStoreConfig::EscapeCommandLineArgument(OidcExePath),
		*IoStoreConfig::EscapeCommandLineArgument(NamespacesResultPath)
	);

	FString NamespacesResult;
	int32 NamespacesExitCode = RunZenCommand(ZenExePath, NamespacesCommand, NamespacesResult);

	if (NamespacesExitCode != 0 || !FPaths::FileExists(NamespacesResultPath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to query namespaces. Exit code: %d", NamespacesExitCode);
		IFileManager::Get().Delete(*NamespacesResultPath);
		return;
	}

	// Parse namespaces
	FString NamespacesJsonString;
	if (!FFileHelper::LoadFileToString(NamespacesJsonString, *NamespacesResultPath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to read namespaces result file");
		IFileManager::Get().Delete(*NamespacesResultPath);
		return;
	}

	TSharedPtr<FJsonObject> NamespacesJsonObject;
	TSharedRef<TJsonReader<>> NamespacesReader = TJsonReaderFactory<>::Create(NamespacesJsonString);

	TArray<FString> Namespaces;
	if (FJsonSerializer::Deserialize(NamespacesReader, NamespacesJsonObject) && NamespacesJsonObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* NamespaceResults;
		if (NamespacesJsonObject->TryGetArrayField(TEXT("results"), NamespaceResults))
		{
			for (const TSharedPtr<FJsonValue>& ResultValue : *NamespaceResults)
			{
				const TSharedPtr<FJsonObject>* ResultObj;
				if (ResultValue->TryGetObject(ResultObj))
				{
					FString NamespaceName;
					if ((*ResultObj)->TryGetStringField(TEXT("name"), NamespaceName))
					{
						Namespaces.Add(NamespaceName);
					}
				}
			}
		}
	}

	// Cleanup
	IFileManager::Get().Delete(*NamespacesResultPath);

	if (Namespaces.Num() == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "No namespaces found");
		return;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d namespaces", Namespaces.Num());

	// Use first namespace
	FString Namespace = Namespaces[0];
	FString BuildType = TEXT("staged-build");

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Using namespace: %ls, build type: %ls", *Namespace, *BuildType);

	// Query builds
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Querying available builds...");

	// Create query JSON
	FString BucketRegex = FString::Printf(TEXT("fortnitegame.%s.*"), *BuildType.ToLower());

	TSharedPtr<FJsonObject> QueryObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> CreatedAtQuery = MakeShared<FJsonObject>();

	// Query builds from last 7 days
	FDateTime WeekAgo = FDateTime::UtcNow() - FTimespan::FromDays(7);
	CreatedAtQuery->SetStringField(TEXT("$gte"), WeekAgo.ToIso8601());

	TSharedPtr<FJsonObject> RootQuery = MakeShared<FJsonObject>();
	// Use camelCase field name to match backend JSON schema and result parsing (line 437)
	RootQuery->SetObjectField(TEXT("createdAt"), CreatedAtQuery);

	QueryObject->SetObjectField(TEXT("query"), RootQuery);
	QueryObject->SetStringField(TEXT("bucketRegex"), BucketRegex);

	TSharedPtr<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetNumberField(TEXT("max"), 50000);
	Options->SetNumberField(TEXT("limit"), 100);  // Limit to 100 for test
	QueryObject->SetObjectField(TEXT("options"), Options);

	FString QueryJsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&QueryJsonString);
	FJsonSerializer::Serialize(QueryObject.ToSharedRef(), Writer);

	// Write query to temp file
	FString QueryPath = FPaths::CreateTempFilename(*IntermediateDir, TEXT("query_"), TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(QueryJsonString, *QueryPath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to write query file to: %ls (check permissions, disk space, or sandbox restrictions)", *QueryPath);
		return;
	}

	// Run zen.exe to query builds
	FString TempResultPath = FPaths::CreateTempFilename(*IntermediateDir, TEXT("builds_"), TEXT(".json"));
	FString Command = FString::Printf(
		TEXT("builds list --namespace=\"%s\" --query-path \"%s\" --host \"%s\" --oidctoken-exe-path \"%s\" --result-path \"%s\""),
		*IoStoreConfig::EscapeCommandLineArgument(Namespace),
		*IoStoreConfig::EscapeCommandLineArgument(QueryPath),
		*IoStoreConfig::EscapeCommandLineArgument(ProxyUrl),
		*IoStoreConfig::EscapeCommandLineArgument(OidcExePath),
		*IoStoreConfig::EscapeCommandLineArgument(TempResultPath)
	);

	FString Result;
	int32 ExitCode = RunZenCommand(ZenExePath, Command, Result);

	// Cleanup query file
	IFileManager::Get().Delete(*QueryPath);

	if (ExitCode != 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to query builds. Exit code: %d", ExitCode);
		IFileManager::Get().Delete(*TempResultPath);
		return;
	}

	// Parse results
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *TempResultPath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to read build result file");
		IFileManager::Get().Delete(*TempResultPath);
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to parse JSON result");
		IFileManager::Get().Delete(*TempResultPath);
		return;
	}

	// Find latest build
	FString LatestBuildId, LatestBucketId, LatestBuildName;
	FDateTime LatestDate = FDateTime::MinValue();

	TMap<FString, TArray<FString>> BuildGroups; // BuildGroup -> list of bucket IDs

	const TArray<TSharedPtr<FJsonValue>>* Results;
	if (JsonObject->TryGetArrayField(TEXT("results"), Results))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d build entries", Results->Num());

		for (const TSharedPtr<FJsonValue>& ResultValue : *Results)
		{
			const TSharedPtr<FJsonObject>* ResultObj;
			if (ResultValue->TryGetObject(ResultObj))
			{
				FString BuildId, BucketId;
				(*ResultObj)->TryGetStringField(TEXT("buildId"), BuildId);
				(*ResultObj)->TryGetStringField(TEXT("bucketId"), BucketId);

				const TSharedPtr<FJsonObject>* MetadataObj;
				if ((*ResultObj)->TryGetObjectField(TEXT("metadata"), MetadataObj))
				{
					FString BuildGroup;
					(*MetadataObj)->TryGetStringField(TEXT("buildgroup"), BuildGroup);

					FString CreatedAtStr;
					(*MetadataObj)->TryGetStringField(TEXT("createdAt"), CreatedAtStr);
					FDateTime CreatedAt;
					FDateTime::ParseIso8601(*CreatedAtStr, CreatedAt);

					if (!BuildGroup.IsEmpty() && CreatedAt > LatestDate)
					{
						LatestDate = CreatedAt;
						LatestBuildId = BuildId;
						LatestBucketId = BucketId;
						LatestBuildName = BuildGroup;
					}
				}
			}
		}
	}

	// Cleanup
	IFileManager::Get().Delete(*TempResultPath);

	if (LatestBuildId.IsEmpty())
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "No builds found");
		return;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Latest build: %ls (ID: %ls, Date: %ls)",
		*LatestBuildName, *LatestBuildId, *LatestDate.ToString());

	// Download the build
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Downloading build to: %ls", *DownloadPath);

	// Create download directory
	if (!IFileManager::Get().DirectoryExists(*DownloadPath))
	{
		if (!IFileManager::Get().MakeDirectory(*DownloadPath, true))
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to create download directory: %ls (check permissions, disk space, or path validity)", *DownloadPath);
			return;
		}
	}

	// Build download command - download .utoc, .ucas, .uondemandtoc, and .umeta files
	// .ucas files contain the actual chunk data that .utoc files reference
	Command = FString::Printf(
		TEXT("builds download --namespace=\"%s\" --bucket=\"%s\" --build-id=\"%s\" --local-path=\"%s\" --host \"%s\" --oidctoken-exe-path \"%s\" --wildcard=\"*.utoc;*.ucas;*.uondemandtoc;*.umeta\" --clean --verbose"),
		*IoStoreConfig::EscapeCommandLineArgument(Namespace),
		*IoStoreConfig::EscapeCommandLineArgument(LatestBucketId),
		*IoStoreConfig::EscapeCommandLineArgument(LatestBuildId),
		*IoStoreConfig::EscapeCommandLineArgument(DownloadPath),
		*IoStoreConfig::EscapeCommandLineArgument(ProxyUrl),
		*IoStoreConfig::EscapeCommandLineArgument(OidcExePath)
	);

	Result.Empty();
	ExitCode = RunZenCommand(ZenExePath, Command, Result);

	if (ExitCode != 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Download failed. Exit code: %d", ExitCode);
		return;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "=== Cloud Download Test Completed Successfully ===");
}

// Test function that exercises the SAME loading code path as SIoStoreDependencyViewer
// This uses the same APIs in the same order to catch any issues
bool TestLoadFromDirectory(const FString& DirectoryPath)
{
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Testing container loading from: %ls", *DirectoryPath);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Step 1: Load .umeta files (same as SIoStoreDependencyViewer::LoadMetaData)
	TArray<FString> MetaFiles;
	PlatformFile.FindFilesRecursively(MetaFiles, *DirectoryPath, TEXT(".umeta"));
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d .umeta files", MetaFiles.Num());

	// Step 2: Load .utoc files (same as SIoStoreDependencyViewer::LoadAllContainersWithProgress)
	TArray<FString> TocFiles;
	PlatformFile.FindFilesRecursively(TocFiles, *DirectoryPath, TEXT(".utoc"));

	if (TocFiles.Num() == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Warning, "No .utoc files found");
		return false;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d .utoc files", TocFiles.Num());

	// Load each container using the SAME code path
	int32 TotalChunks = 0;
	for (const FString& TocFile : TocFiles)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading container: %ls", *TocFile);

		// SAME version validation as SIoStoreDependencyViewer (line 537)
		FIoStoreTocResource TocResource;
		FIoStatus TocStatus = FIoStoreTocResource::Read(*TocFile, EIoStoreTocReadOptions::Default, TocResource);

		if (!TocStatus.IsOk())
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to read TOC header from %ls: %ls",
				*TocFile, *TocStatus.ToString());
			continue;
		}

		// SAME version check as SIoStoreDependencyViewer (lines 678-692)
		const uint8 TocVersion = TocResource.Header.Version;
		const uint8 LatestVersion = static_cast<uint8>(EIoStoreTocVersion::Latest);
		const uint8 InitialVersion = static_cast<uint8>(EIoStoreTocVersion::Initial);

		if (TocVersion > LatestVersion)
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error,
				"Incompatible IoStore version in %ls: Container version %d > Latest supported %d",
				*TocFile, TocVersion, LatestVersion);
			continue;
		}
		else if (TocVersion < InitialVersion)
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error,
				"Incompatible IoStore version in %ls: Container version %d < Initial supported %d",
				*TocFile, TocVersion, InitialVersion);
			continue;
		}

		UE_LOGF(LogIoStoreDependencyViewer, Display, "  TOC Version: %d (Latest: %d)", TocVersion, LatestVersion);

		// SAME reader initialization (line 569)
		TUniquePtr<FIoStoreReader> Reader = MakeUnique<FIoStoreReader>();
		FString ContainerPath = FPaths::ChangeExtension(TocFile, TEXT(""));
		FIoStatus Status = Reader->Initialize(*ContainerPath, TMap<FGuid, FAES::FAESKey>());

		if (!Status.IsOk())
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to initialize reader for %ls: %ls",
				*TocFile, *Status.ToString());
			continue;
		}

		int32 ChunkCount = Reader->GetChunkCount();
		TotalChunks += ChunkCount;
		UE_LOGF(LogIoStoreDependencyViewer, Display, "  Container loaded successfully: %d chunks", ChunkCount);

		// SAME container header read that was crashing (line 633)
		FIoChunkId ContainerHeaderChunkId = CreateIoChunkId(Reader->GetContainerId().Value(), 0, EIoChunkType::ContainerHeader);
		UE_LOGF(LogIoStoreDependencyViewer, Display, "  Attempting to read container header...");

		TIoStatusOr<FIoBuffer> HeaderBuffer = Reader->Read(ContainerHeaderChunkId, FIoReadOptions());
		if (HeaderBuffer.IsOk())
		{
			UE_LOGF(LogIoStoreDependencyViewer, Display, "  Container header read successfully: %llu bytes", HeaderBuffer.ValueOrDie().GetSize());
		}
		else
		{
			UE_LOGF(LogIoStoreDependencyViewer, Warning, "  Failed to read container header: %ls", *HeaderBuffer.Status().ToString());
		}
	}

	// Step 3: Load .uondemandtoc files (same as SIoStoreDependencyViewer::LoadOnDemandContainers)
	TArray<FString> OnDemandTocFiles;
	PlatformFile.FindFilesRecursively(OnDemandTocFiles, *DirectoryPath, TEXT(".uondemandtoc"));
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d .uondemandtoc files", OnDemandTocFiles.Num());

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Test load completed: %d total chunks from %d containers",
		TotalChunks, TocFiles.Num());

	return TotalChunks > 0;
}

void TestCloudDownloadSpecific(const FString& Namespace, const FString& Bucket, const FString& BuildId, const FString& DownloadPath)
{
	UE_LOGF(LogIoStoreDependencyViewer, Display, "=== Testing Cloud Download with Specific Build ===");
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Namespace: %ls", *Namespace);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Bucket: %ls", *Bucket);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Build ID: %ls", *BuildId);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Download Path: %ls", *DownloadPath);

	// Create a mock cloud build structure - same as used in SCloudDownloadDialog
	struct FTestCloudBuild
	{
		FString BuildName;
		FString BuildId;
		FString Changelist;
		FString BucketId;
		FString Namespace;
	};

	FTestCloudBuild Build;
	Build.BuildId = BuildId;
	Build.BucketId = Bucket;
	Build.Namespace = Namespace;
	Build.BuildName = TEXT("CommandlineTest");
	Build.Changelist = TEXT("Unknown");

	// Use the SAME download logic as SCloudDownloadDialog::DownloadBuild
	// Get zen.exe and OidcToken.exe paths
	FString ZenExePath;
	FString OidcExePath;
	IoStoreConfig::FindZenExePath(ZenExePath);
	IoStoreConfig::FindOidcTokenExePath(OidcExePath, ZenExePath);

	// Load proxy URL - SAME as SCloudDownloadDialog::LoadCloudSettings
	FString ProxyUrl;
	FString CloudSetting;
	if (GConfig && GConfig->GetString(TEXT("StorageServers"), TEXT("Cloud"), CloudSetting, GEngineIni))
	{
		int32 HostStartIdx = CloudSetting.Find(TEXT("Host=\""));
		if (HostStartIdx != INDEX_NONE)
		{
			HostStartIdx += 6;
			int32 HostEndIdx = CloudSetting.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, HostStartIdx);
			if (HostEndIdx != INDEX_NONE)
			{
				FString HostUrls = CloudSetting.Mid(HostStartIdx, HostEndIdx - HostStartIdx);
				TArray<FString> Urls;
				HostUrls.ParseIntoArray(Urls, TEXT(";"));
				if (Urls.Num() > 0)
				{
					ProxyUrl = Urls[0];
				}
			}
		}
	}
	if (ProxyUrl.IsEmpty())
	{
		ProxyUrl = TEXT("https://jupiter.devtools.epicgames.com");
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Settings:");
	UE_LOGF(LogIoStoreDependencyViewer, Display, "  ZenExe: %ls", *ZenExePath);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "  OidcExe: %ls", *OidcExePath);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "  Host: %ls", *ProxyUrl);

	if (!FPaths::FileExists(ZenExePath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "zen.exe not found at: %ls", *ZenExePath);
		return;
	}
	if (!FPaths::FileExists(OidcExePath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "OidcToken.exe not found at: %ls", *OidcExePath);
		return;
	}

	// Create download directory
	if (!IFileManager::Get().DirectoryExists(*DownloadPath))
	{
		if (!IFileManager::Get().MakeDirectory(*DownloadPath, true))
		{
			UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to create download directory: %ls (check permissions, disk space, or path validity)", *DownloadPath);
			return;
		}
	}

	// EXACT SAME download command as SCloudDownloadDialog::DownloadBuild (line 606)
	// Added --verbose for progress visibility
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Starting download...");

	// Pre-escape arguments for format string validator
	FString EscapedNamespace = IoStoreConfig::EscapeCommandLineArgument(Build.Namespace);
	FString EscapedBucket = IoStoreConfig::EscapeCommandLineArgument(Build.BucketId);
	FString EscapedBuildId = IoStoreConfig::EscapeCommandLineArgument(Build.BuildId);
	FString EscapedDownloadPath = IoStoreConfig::EscapeCommandLineArgument(DownloadPath);
	FString EscapedProxyUrl = IoStoreConfig::EscapeCommandLineArgument(ProxyUrl);
	FString EscapedOidcExePath = IoStoreConfig::EscapeCommandLineArgument(OidcExePath);

	FString Command = FString::Printf(
		TEXT("builds download --namespace=\"%s\" --bucket=\"%s\" --build-id=\"%s\" --local-path=\"%s\" --host \"%s\" --oidctoken-exe-path \"%s\" --wildcard=\"*.utoc;*.ucas;*.uondemandtoc;*.umeta\" --clean --enable-scavenge --verbose"),
		*EscapedNamespace,
		*EscapedBucket,
		*EscapedBuildId,
		*EscapedDownloadPath,
		*EscapedProxyUrl,
		*EscapedOidcExePath
	);

	FString Result;
	int32 ExitCode = RunZenCommand(ZenExePath, Command, Result);

	if (ExitCode != 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Download failed. Exit code: %d", ExitCode);
		return;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Download completed successfully!");

	// Find the actual directory containing .utoc files (may be in subdirectory)
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TArray<FString> TocFiles;
	PlatformFile.FindFilesRecursively(TocFiles, *DownloadPath, TEXT(".utoc"));

	if (TocFiles.Num() == 0)
	{
		// Try .uondemandtoc files
		TArray<FString> OnDemandTocFiles;
		PlatformFile.FindFilesRecursively(OnDemandTocFiles, *DownloadPath, TEXT(".uondemandtoc"));
		if (OnDemandTocFiles.Num() > 0)
		{
			TocFiles = OnDemandTocFiles;
		}
	}

	if (TocFiles.Num() == 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "No .utoc or .uondemandtoc files found in download directory: %ls", *DownloadPath);
		return;
	}

	// Get the directory of the first TOC file found
	FString ActualDir = FPaths::GetPath(TocFiles[0]);
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d TOC files in: %ls", TocFiles.Num(), *ActualDir);

	// Now load the directory using the SAME code path as the GUI
	UE_LOGF(LogIoStoreDependencyViewer, Display, "Loading downloaded containers...");

	if (!TestLoadFromDirectory(ActualDir))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to load containers from downloaded directory");
		return;
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "=== Test Completed Successfully ===");
}

void TestTocToCSVFromCommandLine()
{
	UE_LOGF(LogIoStoreDependencyViewer, Display, "=== TOC to CSV Test ===");

	// Parse command-line parameters
	FString Namespace, Bucket, BuildId;
	int32 MaxContainers = 5;

	if (!FParse::Value(FCommandLine::Get(), TEXT("namespace="), Namespace))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Missing required parameter: -namespace=");
		return;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("bucket="), Bucket))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Missing required parameter: -bucket=");
		return;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("build-id="), BuildId))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Missing required parameter: -build-id=");
		return;
	}

	FParse::Value(FCommandLine::Get(), TEXT("max-containers="), MaxContainers);

	// Setup paths
	FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("IoStoreDependencyViewer"), TEXT("TocCsvTest"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	// Download TOC files - get zen.exe and OidcToken.exe paths
	FString ZenExePath;
	FString OidcExePath;
	IoStoreConfig::FindZenExePath(ZenExePath);
	IoStoreConfig::FindOidcTokenExePath(OidcExePath, ZenExePath);

	// Load Engine.ini to get host URL from [StorageServers] Cloud setting
	FString ProxyUrl;
	// Read Cloud setting from [StorageServers] section
	FString CloudSetting;
	if (GConfig && GConfig->GetString(TEXT("StorageServers"), TEXT("Cloud"), CloudSetting, GEngineIni))
	{
		// Parse the Host= field from the Cloud setting
		int32 HostStartIdx = CloudSetting.Find(TEXT("Host=\""));
		if (HostStartIdx != INDEX_NONE)
		{
			HostStartIdx += 6; // Skip 'Host="'
			int32 HostEndIdx = CloudSetting.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, HostStartIdx);
			if (HostEndIdx != INDEX_NONE)
			{
				FString HostUrls = CloudSetting.Mid(HostStartIdx, HostEndIdx - HostStartIdx);
				// Split by semicolon and use first URL
				TArray<FString> Urls;
				HostUrls.ParseIntoArray(Urls, TEXT(";"));
				if (Urls.Num() > 0)
				{
					ProxyUrl = Urls[0];
				}
			}
		}
	}

	// Fallback to default if not found
	if (ProxyUrl.IsEmpty())
	{
		ProxyUrl = TEXT("https://jupiter.devtools.epicgames.com");
	}

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Downloading TOC files...");

	// Pre-escape arguments for format string validator
	FString EscapedNamespace2 = IoStoreConfig::EscapeCommandLineArgument(Namespace);
	FString EscapedBucket2 = IoStoreConfig::EscapeCommandLineArgument(Bucket);
	FString EscapedBuildId2 = IoStoreConfig::EscapeCommandLineArgument(BuildId);
	FString EscapedTempDir = IoStoreConfig::EscapeCommandLineArgument(TempDir);
	FString EscapedProxyUrl2 = IoStoreConfig::EscapeCommandLineArgument(ProxyUrl);
	FString EscapedOidcExePath2 = IoStoreConfig::EscapeCommandLineArgument(OidcExePath);

	FString ZenCommand = FString::Printf(
		TEXT("builds download --namespace=\"%s\" --bucket=\"%s\" --build-id=\"%s\" --local-path=\"%s\" --wildcard=\"*.utoc;*.ucas\" --host \"%s\" --oidctoken-exe-path \"%s\""),
		*EscapedNamespace2, *EscapedBucket2, *EscapedBuildId2,
		*EscapedTempDir, *EscapedProxyUrl2, *EscapedOidcExePath2);

	FString ZenOutput;
	int32 ZenResult = RunZenCommand(ZenExePath, ZenCommand, ZenOutput);

	if (ZenResult != 0)
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Download failed");
		return;
	}

	// Find TOC files
	TArray<FString> TocFiles;
	IFileManager::Get().FindFilesRecursive(TocFiles, *TempDir, TEXT("*.utoc"), true, false);

	UE_LOGF(LogIoStoreDependencyViewer, Display, "Found %d TOC files", TocFiles.Num());

	// Create CSV
	FString CsvContent = TEXT("ContainerName,TocPath,TocSizeBytes,HasUcas,UcasSizeBytes\n");

	int64 TotalTocSize = 0;
	int64 TotalUcasSize = 0;
	int32 ProcessCount = FMath::Min(TocFiles.Num(), MaxContainers);

	for (int32 i = 0; i < ProcessCount; ++i)
	{
		FString TocPath = TocFiles[i];
		FString BaseName = FPaths::GetBaseFilename(TocPath);
		int64 TocSize = IFileManager::Get().FileSize(*TocPath);

		// Use FPaths::ChangeExtension instead of Replace to avoid path corruption
		// if the path contains ".utoc" in directory names
		FString UcasPath = FPaths::ChangeExtension(TocPath, TEXT(".ucas"));
		bool bHasUcas = FPaths::FileExists(UcasPath);
		int64 UcasSize = bHasUcas ? IFileManager::Get().FileSize(*UcasPath) : 0;

		CsvContent += FString::Printf(TEXT("%s,\"%s\",%lld,%s,%lld\n"),
			*BaseName, *TocPath, TocSize, bHasUcas ? TEXT("Yes") : TEXT("No"), UcasSize);

		TotalTocSize += TocSize;
		TotalUcasSize += UcasSize;
	}

	CsvContent += FString::Printf(TEXT("\nSUMMARY\n"));
	CsvContent += FString::Printf(TEXT("Containers,%d\n"), ProcessCount);
	CsvContent += FString::Printf(TEXT("Total TOC (MB),%.2f\n"), TotalTocSize / (1024.0 * 1024.0));
	CsvContent += FString::Printf(TEXT("Total UCAS (MB),%.2f\n"), TotalUcasSize / (1024.0 * 1024.0));

	FString CsvPath = FPaths::Combine(TempDir, TEXT("ContainerInfo.csv"));
	if (FFileHelper::SaveStringToFile(CsvContent, *CsvPath))
	{
		UE_LOGF(LogIoStoreDependencyViewer, Display, "\nSUCCESS! CSV created: %ls", *CsvPath);
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Containers: %d", ProcessCount);
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Total TOC: %.2f MB", TotalTocSize / (1024.0 * 1024.0));
		UE_LOGF(LogIoStoreDependencyViewer, Display, "Total UCAS: %.2f MB", TotalUcasSize / (1024.0 * 1024.0));
	}
	else
	{
		UE_LOGF(LogIoStoreDependencyViewer, Error, "Failed to write CSV");
	}
}
