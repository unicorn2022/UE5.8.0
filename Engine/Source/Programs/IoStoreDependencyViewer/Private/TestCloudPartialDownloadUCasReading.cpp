// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreDependencyViewer.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Serialization/MemoryReader.h"
#include "IO/IoStore.h"
#include "IO/IoDispatcher.h"
#include "IO/IoContainerHeader.h"
#include "Containers/StringConv.h"
#include "PartialDownloadPlatformFile.h"
#include "IoStoreConfigHelpers.h"

/**
 * Cloud Partial Download UCAS Reading Test
 *
 * This test validates the partial download system by:
 * 1. Downloading only .utoc metadata files from cloud (skipping .ucas data)
 * 2. Installing FPartialDownloadPlatformFile wrapper
 * 3. Parsing TOC using FIoStoreTocResource::Read()
 * 4. Creating FIoStoreReader to access .ucas data (triggers on-demand fetching)
 * 5. Enumerating all chunks
 * 6. Reading container header chunk (triggers block fetching)
 * 7. Extracting package dependencies
 * 8. Exporting all data to CSV (should match baseline output exactly)
 *
 * This test uses the SAME functions the GUI uses for partial downloads.
 *
 * Command Line:
 * IoStoreDependencyViewer.exe -TestCloudPartialDownloadUCas
 *     -namespace=fortnite.oplog
 *     -bucket=fortnitegame.staged-build.fortnite-dev-fn-40.android-client
 *     -build-id=09c0db3ab12fdf4801dd4cb2
 *     -file=pakchunk30-Android_ASTCClient.utoc
 *     -output=partial_download_assets.csv
 *     [-local-path=D:\TestFiles]  (optional - defaults to temp directory)
 */

DEFINE_LOG_CATEGORY_STATIC(LogCloudPartialDownloadTest, Log, All);

namespace CloudPartialDownloadTest
{
	/** Asset information to export */
	struct FAssetInfo
	{
		FIoChunkId ChunkId;
		FString ChunkType;
		FPackageId PackageId;
		FString PackageName;
		FString FileName;
		FString ContainerName;
		uint64 Size = 0;
		uint64 CompressedSize = 0;
		bool bIsCompressed = false;
		int32 PartitionIndex = 0;
		TArray<FPackageId> HardDependencies;
		TArray<FPackageId> SoftDependencies;
	};

	/** Get chunk type as string */
	FString GetChunkTypeString(const FIoChunkId& ChunkId)
	{
		EIoChunkType ChunkType = ChunkId.GetChunkType();
		switch (ChunkType)
		{
			case EIoChunkType::ExportBundleData: return TEXT("PackageData");
			case EIoChunkType::BulkData: return TEXT("BulkData");
			case EIoChunkType::OptionalBulkData: return TEXT("OptionalBulkData");
			case EIoChunkType::MemoryMappedBulkData: return TEXT("MemoryMappedBulkData");
			case EIoChunkType::ContainerHeader: return TEXT("ContainerHeader");
			default: return FString::Printf(TEXT("Unknown(%d)"), (int)ChunkType);
		}
	}

	/** Format dependency list as semicolon-separated string */
	FString FormatDependencyList(const TArray<FPackageId>& Dependencies)
	{
		if (Dependencies.Num() == 0)
		{
			return TEXT("");
		}

		TArray<FString> DepStrings;
		DepStrings.Reserve(Dependencies.Num());
		for (const FPackageId& Dep : Dependencies)
		{
			DepStrings.Add(LexToString(Dep));
		}
		return FString::Join(DepStrings, TEXT(";"));
	}

	/** Escape CSV field if needed */
	FString EscapeCSV(const FString& Value)
	{
		if (Value.Contains(TEXT(",")) || Value.Contains(TEXT("\"")) || Value.Contains(TEXT("\n")))
		{
			return FString::Printf(TEXT("\"%s\""), *Value.Replace(TEXT("\""), TEXT("\"\"")));
		}
		return Value;
	}

	/** Escape command-line arguments */
	FString EscapeCommandLineArgument(const FString& Argument)
	{
		FString Result;
		Result.Reserve(Argument.Len() * 2);

		for (int32 i = 0; i < Argument.Len(); ++i)
		{
			int32 NumBackslashes = 0;

			while (i < Argument.Len() && Argument[i] == TEXT('\\'))
			{
				++NumBackslashes;
				++i;
			}

			if (i == Argument.Len())
			{
				for (int32 j = 0; j < NumBackslashes * 2; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				break;
			}
			else if (Argument[i] == TEXT('"'))
			{
				for (int32 j = 0; j < NumBackslashes * 2 + 1; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				Result.AppendChar(TEXT('"'));
			}
			else
			{
				for (int32 j = 0; j < NumBackslashes; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				Result.AppendChar(Argument[i]);
			}
		}

		return Result;
	}

	/** Download only metadata files using zen.exe (replicates GUI partial download behavior) */
	bool DownloadMetadataOnly(const FString& Host, const FString& Namespace, const FString& Bucket, const FString& BuildId,
		const FString& LocalPath)
	{
		UE_LOGF(LogCloudPartialDownloadTest, Display, "Downloading metadata files from cloud using zen.exe...");
		UE_LOGF(LogCloudPartialDownloadTest, Display, "  Host: %ls", *Host);
		UE_LOGF(LogCloudPartialDownloadTest, Display, "  Namespace: %ls", *Namespace);
		UE_LOGF(LogCloudPartialDownloadTest, Display, "  Bucket: %ls", *Bucket);
		UE_LOGF(LogCloudPartialDownloadTest, Display, "  BuildId: %ls", *BuildId);
		UE_LOGF(LogCloudPartialDownloadTest, Display, "  LocalPath: %ls", *LocalPath);

		// Create local directory
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CreateDirectoryTree(*LocalPath))
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to create directory: %ls", *LocalPath);
			return false;
		}

		// Find OidcToken.exe for authentication
		FString OidcExePath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPlatformProcess::BaseDir(), TEXT(".."), TEXT("DotNET"), TEXT("OidcToken"), TEXT("win-x64"), TEXT("OidcToken.exe")));

		if (!FPaths::FileExists(OidcExePath))
		{
			UE_LOGF(LogCloudPartialDownloadTest, Warning, "OidcToken.exe not found at: %ls", *OidcExePath);
			UE_LOGF(LogCloudPartialDownloadTest, Warning, "Authentication may fail. Continuing anyway...");
		}

		// Find zen.exe
		FString ZenExePath;
		IoStoreConfig::FindZenExePath(ZenExePath);
		if (!IoStoreConfig::ValidateZenExe(ZenExePath, false))
		{
			return false;
		}

		// Download with wildcard to exclude .ucas files (like GUI does for partial downloads)
		// Use --wildcard="*" --exclude-wildcard="*.ucas" to download all files except .ucas
		FString Arguments = FString::Printf(
			TEXT("builds download --host=\"%s\" --namespace=\"%s\" --bucket=\"%s\" --build-id=\"%s\" --local-path=\"%s\" --oidctoken-exe-path=\"%s\" --wildcard=\"*\" --exclude-wildcard=\"*.ucas\" --clean --enable-scavenge --verbose"),
			*EscapeCommandLineArgument(Host), *EscapeCommandLineArgument(Namespace), *EscapeCommandLineArgument(Bucket),
			*EscapeCommandLineArgument(BuildId), *EscapeCommandLineArgument(LocalPath), *EscapeCommandLineArgument(OidcExePath));

		UE_LOGF(LogCloudPartialDownloadTest, Display, "Executing: %ls %ls", *ZenExePath, *Arguments);
		UE_LOGF(LogCloudPartialDownloadTest, Display, "Downloading metadata only (.utoc, .uondemandtoc, .umeta) - skipping .ucas files");

		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to create pipe for zen.exe process");
			return false;
		}

		FProcHandle ProcHandle = FPlatformProcess::CreateProc(*ZenExePath, *Arguments, false, false, false, nullptr, 0, nullptr, WritePipe);
		if (!ProcHandle.IsValid())
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to start zen.exe process");
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
			return false;
		}

		FPlatformProcess::ClosePipe(0, WritePipe);

		FString StdOut;
		const double TimeoutSeconds = 2.0 * 60.0 * 60.0;
		double StartTime = FPlatformTime::Seconds();

		while (FPlatformProcess::IsProcRunning(ProcHandle))
		{
			double CurrentTime = FPlatformTime::Seconds();
			if (CurrentTime - StartTime > TimeoutSeconds)
			{
				UE_LOGF(LogCloudPartialDownloadTest, Error, "zen.exe operation timed out");
				FPlatformProcess::TerminateProc(ProcHandle, true);
				FPlatformProcess::ClosePipe(ReadPipe, 0);
				FPlatformProcess::CloseProc(ProcHandle);
				return false;
			}

			FPlatformProcess::Sleep(0.1f);

			FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
			if (!NewOutput.IsEmpty())
			{
				StdOut += NewOutput;
				const int32 MaxOutputSize = 50000;
				if (StdOut.Len() > MaxOutputSize)
				{
					StdOut = StdOut.Right(MaxOutputSize);
				}
				UE_LOGF(LogCloudPartialDownloadTest, Display, "%ls", *NewOutput.TrimStartAndEnd());
			}
		}

		FString RemainingOutput = FPlatformProcess::ReadPipe(ReadPipe);
		if (!RemainingOutput.IsEmpty())
		{
			StdOut += RemainingOutput;
			UE_LOGF(LogCloudPartialDownloadTest, Display, "%ls", *RemainingOutput.TrimStartAndEnd());
		}

		int32 ReturnCode = 0;
		FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
		FPlatformProcess::ClosePipe(ReadPipe, 0);
		FPlatformProcess::CloseProc(ProcHandle);

		if (ReturnCode != 0)
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "zen.exe failed with code %d", ReturnCode);
			return false;
		}

		// Verify .utoc files were downloaded
		TArray<FString> DownloadedUtocFiles;
		PlatformFile.FindFilesRecursively(DownloadedUtocFiles, *LocalPath, TEXT(".utoc"));
		UE_LOGF(LogCloudPartialDownloadTest, Display, "Found %d .utoc files after download", DownloadedUtocFiles.Num());

		if (DownloadedUtocFiles.Num() == 0)
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "No .utoc files downloaded - metadata download failed");
			return false;
		}

		UE_LOGF(LogCloudPartialDownloadTest, Display, "Metadata download complete");
		return true;
	}

	/** Process container header and extract dependencies */
	bool ProcessContainerHeader(const FIoContainerHeader& Header, const FString& ContainerName,
		TMap<FIoChunkId, FAssetInfo>& Assets)
	{
		UE_LOGF(LogCloudPartialDownloadTest, Display, "Processing container header for %ls", *ContainerName);
		UE_LOGF(LogCloudPartialDownloadTest, Display, "  Packages: %d", Header.PackageIds.Num());

		for (int32 PackageIndex = 0; PackageIndex < Header.PackageIds.Num(); ++PackageIndex)
		{
			FPackageId PackageId = Header.PackageIds[PackageIndex];
			FIoChunkId PackageChunkId = CreatePackageDataChunkId(PackageId);

			FAssetInfo* AssetPtr = Assets.Find(PackageChunkId);
			if (AssetPtr)
			{
				AssetPtr->PackageId = PackageId;
				AssetPtr->PackageName = LexToString(PackageId);

				const int32 EntrySize = sizeof(FFilePackageStoreEntry);
				if (PackageIndex < INT32_MAX / EntrySize &&
					Header.StoreEntries.Num() >= (PackageIndex + 1) * EntrySize)
				{
					const uint8* EntryPtr = Header.StoreEntries.GetData() + PackageIndex * EntrySize;

					if (!IsAligned(EntryPtr, alignof(FFilePackageStoreEntry)))
					{
						UE_LOGF(LogCloudPartialDownloadTest, Warning,
							"Skipping misaligned FFilePackageStoreEntry for package index %d", PackageIndex);
						continue;
					}

					const FFilePackageStoreEntry* StoreEntry = reinterpret_cast<const FFilePackageStoreEntry*>(EntryPtr);

					for (const FPackageId& ImportedPackage : StoreEntry->ImportedPackages)
					{
						AssetPtr->HardDependencies.Add(ImportedPackage);
					}
				}
			}
		}

		// Extract soft dependencies
		if (Header.SoftPackageReferences.bContainsSoftPackageReferences && Header.SoftPackageReferences.PackageIds.Num() > 0)
		{
			UE_LOGF(LogCloudPartialDownloadTest, Display, "  Soft package references: %d", Header.SoftPackageReferences.PackageIds.Num());

			const uint8* IndicesData = Header.SoftPackageReferences.PackageIndices.GetData();
			int32 CurrentOffset = 0;

			for (int32 PackageIndex = 0; PackageIndex < Header.PackageIds.Num() && CurrentOffset < Header.SoftPackageReferences.PackageIndices.Num(); ++PackageIndex)
			{
				FPackageId PackageId = Header.PackageIds[PackageIndex];
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(PackageId);

				const int32 HeaderSize = sizeof(uint32) * 2;
				if (CurrentOffset <= Header.SoftPackageReferences.PackageIndices.Num() - HeaderSize)
				{
					uint32 ArrayHeader[2];
					FMemory::Memcpy(ArrayHeader, IndicesData + CurrentOffset, sizeof(uint32) * 2);
					uint32 NumSoftRefs = ArrayHeader[0];
					uint32 OffsetToData = ArrayHeader[1];

					FAssetInfo* AssetPtr = Assets.Find(PackageChunkId);
					if (AssetPtr)
					{
						const int64 RequiredSize = (int64)OffsetToData + (int64)NumSoftRefs * sizeof(uint32);
						if (NumSoftRefs > 0 && RequiredSize >= 0 && CurrentOffset <= Header.SoftPackageReferences.PackageIndices.Num() - RequiredSize)
						{
							for (uint32 i = 0; i < NumSoftRefs; ++i)
							{
								uint32 RefIndex;
								FMemory::Memcpy(&RefIndex, IndicesData + CurrentOffset + OffsetToData + i * sizeof(uint32), sizeof(uint32));

								if (RefIndex < (uint32)Header.SoftPackageReferences.PackageIds.Num())
								{
									FPackageId SoftRefPackageId = Header.SoftPackageReferences.PackageIds[RefIndex];
									AssetPtr->SoftDependencies.Add(SoftRefPackageId);
								}
							}
						}
					}

					CurrentOffset += sizeof(uint32) * 2;
				}
			}
		}

		return true;
	}

	/** Export assets to CSV */
	bool ExportToCSV(const TArray<FAssetInfo>& Assets, const FString& OutputPath)
	{
		UE_LOGF(LogCloudPartialDownloadTest, Display, "Exporting %d assets to: %ls", Assets.Num(), *OutputPath);

		TUniquePtr<IFileHandle> FileHandle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*OutputPath));
		if (!FileHandle)
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to open CSV file for writing: %ls", *OutputPath);
			return false;
		}

		FString Header = TEXT("ChunkId,ChunkType,PackageId,PackageName,FileName,ContainerName,Size,CompressedSize,IsCompressed,PartitionIndex,HardDepCount,SoftDepCount,HardDeps,SoftDeps\n");
		FTCHARToUTF8 HeaderUTF8(*Header);
		if (!FileHandle->Write((const uint8*)HeaderUTF8.Get(), HeaderUTF8.Length()))
		{
			UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to write CSV header");
			return false;
		}

		for (const FAssetInfo& Asset : Assets)
		{
			FString Row = FString::Printf(TEXT("%s,%s,%s,%s,%s,%s,%llu,%llu,%s,%d,%d,%d,%s,%s\n"),
				*LexToString(Asset.ChunkId),
				*Asset.ChunkType,
				Asset.PackageId.IsValid() ? *LexToString(Asset.PackageId) : TEXT(""),
				*EscapeCSV(Asset.PackageName),
				*EscapeCSV(Asset.FileName),
				*EscapeCSV(Asset.ContainerName),
				Asset.Size,
				Asset.CompressedSize,
				Asset.bIsCompressed ? TEXT("true") : TEXT("false"),
				Asset.PartitionIndex,
				Asset.HardDependencies.Num(),
				Asset.SoftDependencies.Num(),
				*FormatDependencyList(Asset.HardDependencies),
				*FormatDependencyList(Asset.SoftDependencies)
			);

			FTCHARToUTF8 RowUTF8(*Row);
			if (!FileHandle->Write((const uint8*)RowUTF8.Get(), RowUTF8.Length()))
			{
				UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to write CSV row");
				return false;
			}
		}

		FileHandle->Flush();
		FileHandle.Reset();

		UE_LOGF(LogCloudPartialDownloadTest, Display, "CSV export complete");
		return true;
	}

} // namespace CloudPartialDownloadTest

bool RunTestCloudPartialDownloadUCas()
{
	using namespace CloudPartialDownloadTest;

	UE_LOGF(LogCloudPartialDownloadTest, Display, "========================================");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Cloud Partial Download UCAS Test");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "========================================");

	// Parse command line
	FString Host;
	FString Namespace;
	FString Bucket;
	FString BuildId;
	FString FileName;
	FString OutputPath;
	FString LocalPath;

	if (!FParse::Value(FCommandLine::Get(), TEXT("host="), Host))
	{
		Host = TEXT("https://jupiter.devtools.epicgames.com");
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("namespace="), Namespace))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Missing required parameter: -namespace=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("bucket="), Bucket))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Missing required parameter: -bucket=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("build-id="), BuildId))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Missing required parameter: -build-id=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("file="), FileName))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Missing required parameter: -file=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("output="), OutputPath))
	{
		OutputPath = TEXT("partial_download_assets.csv");
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("local-path="), LocalPath))
	{
		LocalPath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("PartialDownloadTest"));
	}

	LocalPath = FPaths::ConvertRelativePathToFull(LocalPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	FString OutputDir = FPaths::GetPath(OutputPath);
	if (!OutputDir.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	UE_LOGF(LogCloudPartialDownloadTest, Display, "Test Configuration:");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Host: %ls", *Host);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Namespace: %ls", *Namespace);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Bucket: %ls", *Bucket);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  BuildId: %ls", *BuildId);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  File: %ls", *FileName);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  LocalPath: %ls", *LocalPath);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Output: %ls", *OutputPath);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 1: Download metadata only (no .ucas files)
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 1: Download metadata files only");
	if (!DownloadMetadataOnly(Host, Namespace, Bucket, BuildId, LocalPath))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to download metadata");
		return false;
	}
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 2: Find the .utoc file
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 2: Locating .utoc file");
	FString TocFileName = FileName;
	FString TocFilePath;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TArray<FString> FoundTocFiles;
	PlatformFile.FindFilesRecursively(FoundTocFiles, *LocalPath, TEXT(".utoc"));

	FString* MatchingFile = FoundTocFiles.FindByPredicate([&TocFileName](const FString& Path) {
		return FPaths::GetCleanFilename(Path).Equals(TocFileName, ESearchCase::IgnoreCase);
	});

	if (!MatchingFile)
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Could not find %ls in downloaded files", *TocFileName);
		return false;
	}

	TocFilePath = *MatchingFile;
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  TOC file: %ls", *TocFilePath);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 3: Setup partial download platform file (like GUI does)
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 3: Installing partial download platform file wrapper");

	// Find zen.exe and OidcToken.exe
	FString ZenExePath;
	FString OidcExePath;
	IoStoreConfig::FindZenExePath(ZenExePath);
	IoStoreConfig::FindOidcTokenExePath(OidcExePath, ZenExePath);

	// Create and initialize the partial download platform file
	TSharedPtr<FPartialDownloadPlatformFile> PartialDownloadPlatformFile = MakeShared<FPartialDownloadPlatformFile>();

	IPlatformFile& PhysicalPlatform = FPlatformFileManager::Get().GetPlatformFile();
	PartialDownloadPlatformFile->Initialize(&PhysicalPlatform, TEXT(""));

	if (!PartialDownloadPlatformFile->InitializePartialDownload(
		LocalPath,
		ZenExePath,
		OidcExePath,
		Namespace,
		Bucket,
		BuildId,
		Host))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to initialize partial download system");
		return false;
	}

	// Parse TOC files to build block maps
	PartialDownloadPlatformFile->ParseTocFiles();

	// Install as the active platform file
	FPlatformFileManager::Get().SetPlatformFile(*PartialDownloadPlatformFile);

	// RAII guard to ensure platform file is always restored, even on early returns
	struct FPlatformFileRestorer
	{
		IPlatformFile& OriginalPlatformFile;

		FPlatformFileRestorer(IPlatformFile& InOriginal) : OriginalPlatformFile(InOriginal) {}

		~FPlatformFileRestorer()
		{
			FPlatformFileManager::Get().SetPlatformFile(OriginalPlatformFile);
		}
	} PlatformFileGuard(PhysicalPlatform);

	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Partial download platform file installed");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  All .ucas file access will trigger on-demand block fetching");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 4: Parse TOC file (reads metadata only)
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 4: Parsing TOC file");

	FIoStoreTocResource TocResource;
	FIoStatus TocStatus = FIoStoreTocResource::Read(*TocFilePath, EIoStoreTocReadOptions::Default, TocResource);

	if (!TocStatus.IsOk())
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to read TOC file: %ls", *TocStatus.ToString());
		return false;
	}

	UE_LOGF(LogCloudPartialDownloadTest, Display, "  TOC parsed successfully");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Version: %d", TocResource.Header.Version);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  ChunkIds: %d", TocResource.ChunkIds.Num());
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 5: Create FIoStoreReader (uses IPlatformFile to open .ucas)
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 5: Creating IoStoreReader");

	TUniquePtr<FIoStoreReader> Reader = MakeUnique<FIoStoreReader>();
	FString ContainerPath = FPaths::ChangeExtension(TocFilePath, TEXT(""));
	FIoStatus Status = Reader->Initialize(*ContainerPath, TMap<FGuid, FAES::FAESKey>());

	if (!Status.IsOk())
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to initialize IoStoreReader: %ls", *Status.ToString());
		return false;
	}

	FString ContainerName = FPaths::GetBaseFilename(TocFilePath);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  IoStoreReader initialized");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Container: %ls", *ContainerName);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 6: Enumerate all chunks
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 6: Enumerating chunks");

	TMap<FIoChunkId, FAssetInfo> AssetMap;
	AssetMap.Reserve(Reader->GetChunkCount());

	Reader->EnumerateChunks([&AssetMap, &ContainerName](FIoStoreTocChunkInfo&& ChunkInfo)
	{
		FAssetInfo Asset;
		Asset.ChunkId = ChunkInfo.Id;
		Asset.ChunkType = GetChunkTypeString(ChunkInfo.Id);
		Asset.ContainerName = ContainerName;
		Asset.Size = ChunkInfo.Size;
		Asset.CompressedSize = ChunkInfo.CompressedSize;
		Asset.bIsCompressed = ChunkInfo.bIsCompressed;
		Asset.PartitionIndex = ChunkInfo.PartitionIndex;
		Asset.FileName = ChunkInfo.FileName;

		if (Asset.FileName.IsEmpty())
		{
			Asset.FileName = FString::Printf(TEXT("Chunk_%s"), *LexToString(ChunkInfo.Id));
		}

		AssetMap.Add(Asset.ChunkId, Asset);
		return true;
	});

	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Enumerated %d chunks", AssetMap.Num());
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 7: Read container header chunk (THIS WILL TRIGGER PARTIAL DOWNLOAD!)
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 7: Reading container header (will trigger on-demand fetch)");

	FIoChunkId ContainerHeaderChunkId = CreateIoChunkId(Reader->GetContainerId().Value(), 0, EIoChunkType::ContainerHeader);
	TIoStatusOr<FIoBuffer> HeaderBuffer = Reader->Read(ContainerHeaderChunkId, FIoReadOptions());

	if (!HeaderBuffer.IsOk())
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to read container header: %ls", *HeaderBuffer.Status().ToString());
		UE_LOGF(LogCloudPartialDownloadTest, Error, "This likely means the partial download system failed");
		return false;
	}

	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Container header read successfully via partial download!");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Size: %llu bytes", HeaderBuffer.ValueOrDie().GetSize());
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 8: Deserialize container header
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 8: Deserializing container header");

	FIoContainerHeader Header;
	FMemoryReaderView HeaderReader(HeaderBuffer.ValueOrDie().GetView());
	HeaderReader << Header;

	if (!HeaderReader.IsError() && !HeaderReader.IsCriticalError() && Header.SoftPackageReferencesSerialInfo.Size > 0)
	{
		const int64 Offset64 = Header.SoftPackageReferencesSerialInfo.Offset;
		const int64 Size64 = Header.SoftPackageReferencesSerialInfo.Size;
		const int64 EndOffset = Offset64 + Size64;
		if (Offset64 >= 0 && Size64 > 0 && EndOffset >= Offset64 && EndOffset <= HeaderReader.TotalSize())
		{
			HeaderReader.Seek(Header.SoftPackageReferencesSerialInfo.Offset);
			HeaderReader << Header.SoftPackageReferences;

			if (!HeaderReader.IsError())
			{
				UE_LOGF(LogCloudPartialDownloadTest, Display, "  Loaded soft package references: %d packages",
					Header.SoftPackageReferences.PackageIds.Num());
			}
		}
	}

	if (HeaderReader.IsError() || HeaderReader.IsCriticalError())
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to deserialize container header");
		return false;
	}

	UE_LOGF(LogCloudPartialDownloadTest, Display, "  Container header deserialized successfully");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 9: Process container header to extract dependencies
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 9: Extracting package dependencies");
	if (!ProcessContainerHeader(Header, ContainerName, AssetMap))
	{
		UE_LOGF(LogCloudPartialDownloadTest, Error, "Failed to process container header");
		return false;
	}
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Step 10: Convert map to array and sort
	TArray<FAssetInfo> Assets;
	Assets.Reserve(AssetMap.Num());
	for (const auto& Pair : AssetMap)
	{
		Assets.Add(Pair.Value);
	}

	Assets.Sort([](const FAssetInfo& A, const FAssetInfo& B)
	{
		return LexToString(A.ChunkId) < LexToString(B.ChunkId);
	});

	// Step 11: Export to CSV
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Step 11: Exporting to CSV");
	if (!ExportToCSV(Assets, OutputPath))
	{
		return false;
	}

	UE_LOGF(LogCloudPartialDownloadTest, Display, "");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "========================================");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "CLOUD PARTIAL DOWNLOAD TEST COMPLETE");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "========================================");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Assets exported: %d", Assets.Num());
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Output file: %ls", *OutputPath);
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "Compare this CSV with the baseline to verify partial downloads work correctly:");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "  diff baseline_assets.csv partial_download_assets.csv");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "The files should be identical if partial downloads are working properly.");
	UE_LOGF(LogCloudPartialDownloadTest, Display, "");

	// Cleanup: Platform file will be automatically restored by PlatformFileGuard destructor

	return true;
}
