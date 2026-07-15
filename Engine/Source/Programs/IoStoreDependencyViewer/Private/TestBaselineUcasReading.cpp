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
#include "IoStoreConfigHelpers.h"

/**
 * Baseline UCAS Reading Test
 *
 * This test replicates the current GUI code path:
 * 1. Downloads full .utoc + .ucas files from cloud
 * 2. Parses TOC using FIoStoreTocResource::Read()
 * 3. Creates FIoStoreReader to access .ucas data
 * 4. Enumerates all chunks
 * 5. Reads container header chunk
 * 6. Extracts package dependencies
 * 7. Exports all data to CSV
 *
 * Command Line:
 * IoStoreDependencyViewer.exe -TestBaselineUcas
 *     -namespace=fortnite.oplog
 *     -bucket=fortnitegame.staged-build.fortnite-dev-fn-40.android-client
 *     -build-id=09c0db3ab12fdf4801dd4cb2
 *     -file=pakchunk30-Android_ASTCClient.utoc
 *     -output=baseline_assets.csv
 *     [-local-path=D:\TestFiles]  (optional - skip download if files exist locally)
 */

DEFINE_LOG_CATEGORY_STATIC(LogBaselineUcasTest, Log, All);

namespace BaselineUcasTest
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

	/**
	 * Escapes a string for safe use as a command-line argument value on Windows.
	 * Implements proper Windows/CRT CommandLineToArgvW escaping rules:
	 * - Sequences of backslashes before quotes must be doubled, then the quote escaped
	 * - Trailing backslashes (before closing quote) must be doubled
	 * - Other backslashes are literal
	 *
	 * This prevents argument injection via embedded quotes and handles edge cases like:
	 * - C:\temp\ → C:\temp\\ (trailing backslash doubled)
	 * - C:\test"file → C:\test\"file (quote escaped)
	 * - C:\path\"name → C:\path\\\"name (backslash before quote doubled + quote escaped)
	 */
	FString EscapeCommandLineArgument(const FString& Argument)
	{
		FString Result;
		Result.Reserve(Argument.Len() * 2);

		for (int32 i = 0; i < Argument.Len(); ++i)
		{
			int32 NumBackslashes = 0;

			// Count consecutive backslashes
			while (i < Argument.Len() && Argument[i] == TEXT('\\'))
			{
				++NumBackslashes;
				++i;
			}

			if (i == Argument.Len())
			{
				// Trailing backslashes before the closing quote - must be doubled
				for (int32 j = 0; j < NumBackslashes * 2; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				break;
			}
			else if (Argument[i] == TEXT('"'))
			{
				// Backslashes before a quote - double them and escape the quote
				// (numBackslashes * 2 + 1) backslashes, then the quote
				for (int32 j = 0; j < NumBackslashes * 2 + 1; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				Result.AppendChar(TEXT('"'));
			}
			else
			{
				// Normal character - backslashes are literal
				for (int32 j = 0; j < NumBackslashes; ++j)
				{
					Result.AppendChar(TEXT('\\'));
				}
				Result.AppendChar(Argument[i]);
			}
		}

		return Result;
	}

	/** Download build using zen.exe (like the GUI does) */
	bool DownloadBuild(const FString& Host, const FString& Namespace, const FString& Bucket, const FString& BuildId,
		const FString& LocalPath)
	{
		UE_LOGF(LogBaselineUcasTest, Display, "Downloading build from cloud using zen.exe...");
		UE_LOGF(LogBaselineUcasTest, Display, "  Host: %ls", *Host);
		UE_LOGF(LogBaselineUcasTest, Display, "  Namespace: %ls", *Namespace);
		UE_LOGF(LogBaselineUcasTest, Display, "  Bucket: %ls", *Bucket);
		UE_LOGF(LogBaselineUcasTest, Display, "  BuildId: %ls", *BuildId);
		UE_LOGF(LogBaselineUcasTest, Display, "  LocalPath: %ls", *LocalPath);

		// Create local directory
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CreateDirectoryTree(*LocalPath))
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Failed to create directory: %ls", *LocalPath);
			return false;
		}

		// Find OidcToken.exe for authentication
		FString OidcExePath = FPaths::Combine(
			FPlatformProcess::BaseDir(), TEXT(".."), TEXT("DotNET"), TEXT("OidcToken"), TEXT("win-x64"), TEXT("OidcToken.exe"));

		if (!FPaths::FileExists(OidcExePath))
		{
			UE_LOGF(LogBaselineUcasTest, Warning, "OidcToken.exe not found at: %ls", *OidcExePath);
			UE_LOGF(LogBaselineUcasTest, Warning, "Authentication may fail. Continuing anyway...");
		}

		// Build zen.exe command to download full build part
		// zen.exe builds download --host=X --namespace=X --bucket=Y --build-id=Z --local-path=D:\Test --oidctoken-exe-path=...
		FString ZenExePath;
		IoStoreConfig::FindZenExePath(ZenExePath);
		if (!IoStoreConfig::ValidateZenExe(ZenExePath, false))
		{
			return false;
		}
		// All arguments must be both escaped AND quoted for proper Windows command-line parsing
		// Escaping alone doesn't prevent whitespace from splitting values into multiple tokens

		// Pre-escape arguments for format string validator
		FString EscapedHost = EscapeCommandLineArgument(Host);
		FString EscapedNamespace = EscapeCommandLineArgument(Namespace);
		FString EscapedBucket = EscapeCommandLineArgument(Bucket);
		FString EscapedBuildId = EscapeCommandLineArgument(BuildId);
		FString EscapedLocalPath = EscapeCommandLineArgument(LocalPath);
		FString EscapedOidcExePath = EscapeCommandLineArgument(OidcExePath);

		FString Arguments = FString::Printf(TEXT("builds download --host=\"%s\" --namespace=\"%s\" --bucket=\"%s\" --build-id=\"%s\" --local-path=\"%s\" --oidctoken-exe-path=\"%s\""),
			*EscapedHost, *EscapedNamespace, *EscapedBucket,
			*EscapedBuildId, *EscapedLocalPath, *EscapedOidcExePath);

		UE_LOGF(LogBaselineUcasTest, Display, "Executing: %ls %ls", *ZenExePath, *Arguments);
		UE_LOGF(LogBaselineUcasTest, Display, "This may take several minutes for large builds...");

		// Execute zen.exe with timeout protection (replaced ExecProcess with CreateProc + polling)
		// ExecProcess blocks indefinitely if zen.exe hangs - use CreateProc with timeout instead
		FString FullCommand = FString::Printf(TEXT("\"%s\" %s"), *ZenExePath, *Arguments);

		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Failed to create pipe for zen.exe process");
			return false;
		}

		FProcHandle ProcHandle = FPlatformProcess::CreateProc(*ZenExePath, *Arguments, false, false, false, nullptr, 0, nullptr, WritePipe);
		if (!ProcHandle.IsValid())
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Failed to start zen.exe process");
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
			return false;
		}

		// Close parent's copy of write pipe - child has its own copy
		// This ensures proper EOF detection when child closes its write handle
		FPlatformProcess::ClosePipe(0, WritePipe);

		// Wait for process with 2-hour timeout
		FString StdOut;
		const double TimeoutSeconds = 2.0 * 60.0 * 60.0;
		double StartTime = FPlatformTime::Seconds();

		while (FPlatformProcess::IsProcRunning(ProcHandle))
		{
			// Check for timeout
			double CurrentTime = FPlatformTime::Seconds();
			double ElapsedTime = CurrentTime - StartTime;
			if (ElapsedTime > TimeoutSeconds)
			{
				UE_LOGF(LogBaselineUcasTest, Error, "zen.exe operation timed out after %.1f hours", ElapsedTime / 3600.0);
				FPlatformProcess::TerminateProc(ProcHandle, true);
				FPlatformProcess::ClosePipe(ReadPipe, 0);  // WritePipe already closed
				FPlatformProcess::CloseProc(ProcHandle);
				return false;
			}

			FPlatformProcess::Sleep(0.1f);

			// Read output
			FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
			if (!NewOutput.IsEmpty())
			{
				StdOut += NewOutput;
				// Limit StdOut size to prevent OOM on very verbose/long-running commands
				// Keep only the last 100KB of output (approximately 50,000 characters)
				const int32 MaxOutputSize = 50000;
				if (StdOut.Len() > MaxOutputSize)
				{
					StdOut = StdOut.Right(MaxOutputSize);
				}
				UE_LOGF(LogBaselineUcasTest, Display, "%ls", *NewOutput.TrimStartAndEnd());
			}
		}

		// Read any remaining output
		FString RemainingOutput = FPlatformProcess::ReadPipe(ReadPipe);
		if (!RemainingOutput.IsEmpty())
		{
			StdOut += RemainingOutput;
			// Apply same size limit to final output
			const int32 MaxOutputSize = 50000;
			if (StdOut.Len() > MaxOutputSize)
			{
				StdOut = StdOut.Right(MaxOutputSize);
			}
			UE_LOGF(LogBaselineUcasTest, Display, "%ls", *RemainingOutput.TrimStartAndEnd());
		}

		int32 ReturnCode = 0;
		FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);
		FPlatformProcess::ClosePipe(ReadPipe, 0);  // WritePipe already closed
		FPlatformProcess::CloseProc(ProcHandle);

		if (ReturnCode != 0)
		{
			UE_LOGF(LogBaselineUcasTest, Error, "zen.exe failed with code %d", ReturnCode);
			UE_LOGF(LogBaselineUcasTest, Error, "Output: %ls", *StdOut);
			return false;
		}

		UE_LOGF(LogBaselineUcasTest, Display, "Build download complete");
		return true;
	}

	/** Parse container header and extract dependencies */
	bool ProcessContainerHeader(const FIoContainerHeader& Header, const FString& ContainerName,
		TMap<FIoChunkId, FAssetInfo>& Assets)
	{
		UE_LOGF(LogBaselineUcasTest, Display, "Processing container header for %ls", *ContainerName);
		UE_LOGF(LogBaselineUcasTest, Display, "  Packages: %d", Header.PackageIds.Num());

		// Build package ID to chunk ID mapping and extract hard dependencies
		for (int32 PackageIndex = 0; PackageIndex < Header.PackageIds.Num(); ++PackageIndex)
		{
			FPackageId PackageId = Header.PackageIds[PackageIndex];

			// Create chunk ID for this package
			FIoChunkId PackageChunkId = CreatePackageDataChunkId(PackageId);

			// Find corresponding asset
			FAssetInfo* AssetPtr = Assets.Find(PackageChunkId);
			if (AssetPtr)
			{
				// Store package ID and name
				AssetPtr->PackageId = PackageId;
				AssetPtr->PackageName = LexToString(PackageId);

				// Extract hard dependencies (imported packages) from store entries
				// Bounds check: ensure entire FFilePackageStoreEntry fits in buffer, not just the start offset
				// Avoid integer overflow by checking bounds before multiplication
				const int32 EntrySize = sizeof(FFilePackageStoreEntry);
				if (PackageIndex < INT32_MAX / EntrySize &&
					Header.StoreEntries.Num() >= (PackageIndex + 1) * EntrySize)
				{
					// FFilePackageStoreEntry uses self-relative pointers (TFilePackageStoreEntryCArrayView)
					// so it MUST be accessed in-place via pointer cast, not copied with memcpy
					// The IoStore reader ensures this buffer is properly structured and aligned
					const uint8* EntryPtr = Header.StoreEntries.GetData() + PackageIndex * EntrySize;

					// Verify alignment (FFilePackageStoreEntry requires proper alignment for safe reinterpret_cast)
					// Skip this entry if alignment is incorrect to avoid UB (works in all build configurations)
					if (!IsAligned(EntryPtr, alignof(FFilePackageStoreEntry)))
					{
						UE_LOGF(LogIoStoreDependencyViewer, Warning,
							"Skipping misaligned FFilePackageStoreEntry at %p (expected %d-byte alignment) for package index %d",
							EntryPtr, alignof(FFilePackageStoreEntry), PackageIndex);
						continue;
					}

					const FFilePackageStoreEntry* StoreEntry = reinterpret_cast<const FFilePackageStoreEntry*>(EntryPtr);

					// Add imported packages as hard dependencies
					for (const FPackageId& ImportedPackage : StoreEntry->ImportedPackages)
					{
						AssetPtr->HardDependencies.Add(ImportedPackage);
					}
				}
			}
		}

		// Extract soft dependencies from soft package references
		if (Header.SoftPackageReferences.bContainsSoftPackageReferences && Header.SoftPackageReferences.PackageIds.Num() > 0)
		{
			UE_LOGF(LogBaselineUcasTest, Display, "  Soft package references: %d", Header.SoftPackageReferences.PackageIds.Num());

			// The PackageIndices array contains indices into PackageIds for each package
			const uint8* IndicesData = Header.SoftPackageReferences.PackageIndices.GetData();
			int32 CurrentOffset = 0;

			for (int32 PackageIndex = 0; PackageIndex < Header.PackageIds.Num() && CurrentOffset < Header.SoftPackageReferences.PackageIndices.Num(); ++PackageIndex)
			{
				FPackageId PackageId = Header.PackageIds[PackageIndex];
				FIoChunkId PackageChunkId = CreatePackageDataChunkId(PackageId);

				// Read the array view header (num + offset) for this package
				// CRITICAL: Must advance CurrentOffset for EVERY package, not just packages we have assets for
				// Otherwise offset becomes desynchronized and we read wrong data for subsequent packages
				// Check for integer overflow before addition (same fix as Bug #41 in SIoStoreDependencyViewer.cpp)
				const int32 HeaderSize = sizeof(uint32) * 2;
				if (CurrentOffset <= Header.SoftPackageReferences.PackageIndices.Num() - HeaderSize)
				{
					// Use memcpy to avoid alignment/aliasing undefined behavior
					uint32 ArrayHeader[2];
					FMemory::Memcpy(ArrayHeader, IndicesData + CurrentOffset, sizeof(uint32) * 2);
					uint32 NumSoftRefs = ArrayHeader[0];
					uint32 OffsetToData = ArrayHeader[1];

					// Only process soft refs if we have an asset for this package
					FAssetInfo* AssetPtr = Assets.Find(PackageChunkId);
					if (AssetPtr)
					{
						// Check for integer overflow before addition (same fix as Bug #42 in SIoStoreDependencyViewer.cpp)
						// Compute in int64 to prevent overflow, then check
						const int64 RequiredSize = (int64)OffsetToData + (int64)NumSoftRefs * sizeof(uint32);
						if (NumSoftRefs > 0 && RequiredSize >= 0 && CurrentOffset <= Header.SoftPackageReferences.PackageIndices.Num() - RequiredSize)
						{
							// Read soft ref indices using memcpy for alignment safety
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

					// CRITICAL: Always advance offset, even if we don't have an asset for this package
					// Each package has a header in the packed data that must be skipped
					CurrentOffset += sizeof(uint32) * 2;
				}
			}
		}

		return true;
	}

	/** Export assets to CSV */
	bool ExportToCSV(const TArray<FAssetInfo>& Assets, const FString& OutputPath)
	{
		UE_LOGF(LogBaselineUcasTest, Display, "Exporting %d assets to: %ls", Assets.Num(), *OutputPath);

		// Stream CSV to file to avoid high memory usage for large asset sets
		// Opening file once and writing incrementally uses constant memory instead of O(n)
		TUniquePtr<IFileHandle> FileHandle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*OutputPath));
		if (!FileHandle)
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Failed to open CSV file for writing: %ls", *OutputPath);
			return false;
		}

		// Write header
		FString Header = TEXT("ChunkId,ChunkType,PackageId,PackageName,FileName,ContainerName,Size,CompressedSize,IsCompressed,PartitionIndex,HardDepCount,SoftDepCount,HardDeps,SoftDeps\n");
		FTCHARToUTF8 HeaderUTF8(*Header);
		if (!FileHandle->Write((const uint8*)HeaderUTF8.Get(), HeaderUTF8.Length()))
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Failed to write CSV header");
			return false;
		}

		// Write rows incrementally
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
				UE_LOGF(LogBaselineUcasTest, Error, "Failed to write CSV row for asset: %ls", *Asset.PackageName);
				return false;
			}
		}

		// Close file (automatically done by TUniquePtr destructor, but flush explicitly)
		FileHandle->Flush();
		FileHandle.Reset();

		UE_LOGF(LogBaselineUcasTest, Display, "CSV export complete");
		return true;
	}

} // namespace BaselineUcasTest

bool RunTestBaselineUcas()
{
	using namespace BaselineUcasTest;

	UE_LOGF(LogBaselineUcasTest, Display, "========================================");
	UE_LOGF(LogBaselineUcasTest, Display, "Baseline UCAS Reading Test");
	UE_LOGF(LogBaselineUcasTest, Display, "========================================");

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
		// Default to Jupiter if not specified
		Host = TEXT("https://jupiter.devtools.epicgames.com");
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("namespace="), Namespace))
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Missing required parameter: -namespace=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("bucket="), Bucket))
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Missing required parameter: -bucket=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("build-id="), BuildId))
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Missing required parameter: -build-id=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("file="), FileName))
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Missing required parameter: -file=");
		return false;
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("output="), OutputPath))
	{
		OutputPath = TEXT("baseline_assets.csv");
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("local-path="), LocalPath))
	{
		LocalPath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("BaselineTest"));
	}

	// Convert to absolute paths
	LocalPath = FPaths::ConvertRelativePathToFull(LocalPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	// Ensure output directory exists
	FString OutputDir = FPaths::GetPath(OutputPath);
	if (!OutputDir.IsEmpty())
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*OutputDir);
	}

	UE_LOGF(LogBaselineUcasTest, Display, "Test Configuration:");
	UE_LOGF(LogBaselineUcasTest, Display, "  Host: %ls", *Host);
	UE_LOGF(LogBaselineUcasTest, Display, "  Namespace: %ls", *Namespace);
	UE_LOGF(LogBaselineUcasTest, Display, "  Bucket: %ls", *Bucket);
	UE_LOGF(LogBaselineUcasTest, Display, "  BuildId: %ls", *BuildId);
	UE_LOGF(LogBaselineUcasTest, Display, "  File: %ls", *FileName);
	UE_LOGF(LogBaselineUcasTest, Display, "  LocalPath: %ls", *LocalPath);
	UE_LOGF(LogBaselineUcasTest, Display, "  Output: %ls", *OutputPath);
	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Prepare file search parameters
	FString TocFileName = FileName;
	FString UcasFileName = FPaths::GetBaseFilename(FileName) + TEXT(".ucas");
	FString TocFilePath;
	FString UcasFilePath;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Check if files already exist locally (implements documented -local-path skip behavior)
	bool bNeedDownload = true;
	TArray<FString> FoundTocFiles;
	// FindFilesRecursively 3rd parameter is an extension filter (e.g., TEXT(".utoc")), not a full filename
	PlatformFile.FindFilesRecursively(FoundTocFiles, *LocalPath, TEXT(".utoc"));

	// Filter to find the specific file requested by -file= parameter
	if (FoundTocFiles.Num() > 0)
	{
		// Find the file that matches the requested TocFileName
		FString* MatchingFile = FoundTocFiles.FindByPredicate([&TocFileName](const FString& Path) {
			return FPaths::GetCleanFilename(Path).Equals(TocFileName, ESearchCase::IgnoreCase);
		});

		if (MatchingFile)
		{
			TocFilePath = *MatchingFile;
			UcasFilePath = FPaths::ChangeExtension(TocFilePath, TEXT(".ucas"));

			if (PlatformFile.FileExists(*TocFilePath) && PlatformFile.FileExists(*UcasFilePath))
			{
				bNeedDownload = false;
				UE_LOGF(LogBaselineUcasTest, Display, "Files found locally - skipping download");
				UE_LOGF(LogBaselineUcasTest, Display, "  TOC: %ls", *TocFilePath);
				UE_LOGF(LogBaselineUcasTest, Display, "  UCAS: %ls", *UcasFilePath);
				UE_LOGF(LogBaselineUcasTest, Display, "");
			}
		}
	}

	// Step 1: Download build using zen.exe if needed (replicates GUI behavior)
	if (bNeedDownload)
	{
		UE_LOGF(LogBaselineUcasTest, Display, "Files not found locally - downloading from cloud");
		if (!DownloadBuild(Host, Namespace, Bucket, BuildId, LocalPath))
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Failed to download build");
			return false;
		}
		UE_LOGF(LogBaselineUcasTest, Display, "");

		// Step 2: Find the downloaded files
		// zen.exe downloads to a subdirectory structure, so we need to search for the files
		FoundTocFiles.Empty();
		// FindFilesRecursively 3rd parameter is an extension filter (e.g., TEXT(".utoc")), not a full filename
		PlatformFile.FindFilesRecursively(FoundTocFiles, *LocalPath, TEXT(".utoc"));

		if (FoundTocFiles.Num() == 0)
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Could not find %ls in downloaded build at: %ls", *TocFileName, *LocalPath);
			return false;
		}

		// Find the file that matches the requested TocFileName
		FString* MatchingFile = FoundTocFiles.FindByPredicate([&TocFileName](const FString& Path) {
			return FPaths::GetCleanFilename(Path).Equals(TocFileName, ESearchCase::IgnoreCase);
		});

		if (!MatchingFile)
		{
			UE_LOGF(LogBaselineUcasTest, Error, "Could not find requested file %ls in downloaded build. Found %d .utoc files but none matched.", *TocFileName, FoundTocFiles.Num());
			UE_LOGF(LogBaselineUcasTest, Error, "Search path: %ls", *LocalPath);
			return false;
		}

		TocFilePath = *MatchingFile;
		UcasFilePath = FPaths::ChangeExtension(TocFilePath, TEXT(".ucas"));

		if (!PlatformFile.FileExists(*TocFilePath))
		{
			UE_LOGF(LogBaselineUcasTest, Error, "TOC file not found at: %ls", *TocFilePath);
			return false;
		}

		if (!PlatformFile.FileExists(*UcasFilePath))
		{
			UE_LOGF(LogBaselineUcasTest, Error, "UCAS file not found at: %ls", *UcasFilePath);
			return false;
		}

		UE_LOGF(LogBaselineUcasTest, Display, "Files found successfully");
		UE_LOGF(LogBaselineUcasTest, Display, "  TOC: %ls", *TocFilePath);
		UE_LOGF(LogBaselineUcasTest, Display, "  UCAS: %ls", *UcasFilePath);
		UE_LOGF(LogBaselineUcasTest, Display, "");
	}

	// Step 3: Parse TOC file
	UE_LOGF(LogBaselineUcasTest, Display, "Parsing TOC file...");

	FIoStoreTocResource TocResource;
	FIoStatus TocStatus = FIoStoreTocResource::Read(*TocFilePath, EIoStoreTocReadOptions::Default, TocResource);

	if (!TocStatus.IsOk())
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Failed to read TOC file: %ls", *TocStatus.ToString());
		return false;
	}

	UE_LOGF(LogBaselineUcasTest, Display, "TOC parsed successfully");
	UE_LOGF(LogBaselineUcasTest, Display, "  Version: %d", TocResource.Header.Version);
	UE_LOGF(LogBaselineUcasTest, Display, "  ChunkIds: %d", TocResource.ChunkIds.Num());
	UE_LOGF(LogBaselineUcasTest, Display, "  ContainerId: %ls", *LexToString(TocResource.Header.ContainerId));
	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Step 4: Create FIoStoreReader to access .ucas file
	UE_LOGF(LogBaselineUcasTest, Display, "Creating IoStoreReader...");

	TUniquePtr<FIoStoreReader> Reader = MakeUnique<FIoStoreReader>();

	FString ContainerPath = FPaths::ChangeExtension(TocFilePath, TEXT(""));
	FIoStatus Status = Reader->Initialize(*ContainerPath, TMap<FGuid, FAES::FAESKey>());

	if (!Status.IsOk())
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Failed to initialize IoStoreReader: %ls", *Status.ToString());
		return false;
	}

	FString ContainerName = FPaths::GetBaseFilename(TocFilePath);
	UE_LOGF(LogBaselineUcasTest, Display, "IoStoreReader initialized");
	UE_LOGF(LogBaselineUcasTest, Display, "  Container: %ls", *ContainerName);
	UE_LOGF(LogBaselineUcasTest, Display, "  Chunks: %d", Reader->GetChunkCount());
	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Step 5: Enumerate all chunks
	UE_LOGF(LogBaselineUcasTest, Display, "Enumerating chunks...");

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

		return true; // Continue enumeration
	});

	UE_LOGF(LogBaselineUcasTest, Display, "Enumerated %d chunks", AssetMap.Num());
	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Step 6: Read container header chunk
	UE_LOGF(LogBaselineUcasTest, Display, "Reading container header...");

	FIoChunkId ContainerHeaderChunkId = CreateIoChunkId(Reader->GetContainerId().Value(), 0, EIoChunkType::ContainerHeader);
	TIoStatusOr<FIoBuffer> HeaderBuffer = Reader->Read(ContainerHeaderChunkId, FIoReadOptions());

	if (!HeaderBuffer.IsOk())
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Failed to read container header: %ls", *HeaderBuffer.Status().ToString());
		return false;
	}

	UE_LOGF(LogBaselineUcasTest, Display, "Container header read successfully");
	UE_LOGF(LogBaselineUcasTest, Display, "  Size: %llu bytes", HeaderBuffer.ValueOrDie().GetSize());
	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Step 7: Deserialize container header
	UE_LOGF(LogBaselineUcasTest, Display, "Deserializing container header...");

	FIoContainerHeader Header;
	FMemoryReaderView HeaderReader(HeaderBuffer.ValueOrDie().GetView());
	HeaderReader << Header;

	// Load soft package references if they exist
	if (!HeaderReader.IsError() && !HeaderReader.IsCriticalError() && Header.SoftPackageReferencesSerialInfo.Size > 0)
	{
		// Check for integer overflow in offset + size calculation (same fix as Bugs #43-44 in SIoStoreDependencyViewer.cpp)
		const int64 Offset64 = Header.SoftPackageReferencesSerialInfo.Offset;
		const int64 Size64 = Header.SoftPackageReferencesSerialInfo.Size;
		const int64 EndOffset = Offset64 + Size64;
		if (Offset64 >= 0 && Size64 > 0 && EndOffset >= Offset64 && EndOffset <= HeaderReader.TotalSize())
		{
			HeaderReader.Seek(Header.SoftPackageReferencesSerialInfo.Offset);
			HeaderReader << Header.SoftPackageReferences;

			if (!HeaderReader.IsError() && !HeaderReader.IsCriticalError())
			{
				UE_LOGF(LogBaselineUcasTest, Display, "  Loaded soft package references: %d packages",
					Header.SoftPackageReferences.PackageIds.Num());
			}
		}
	}

	if (HeaderReader.IsError() || HeaderReader.IsCriticalError())
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Failed to deserialize container header");
		return false;
	}

	UE_LOGF(LogBaselineUcasTest, Display, "Container header deserialized successfully");
	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Step 8: Process container header to extract dependencies
	if (!ProcessContainerHeader(Header, ContainerName, AssetMap))
	{
		UE_LOGF(LogBaselineUcasTest, Error, "Failed to process container header");
		return false;
	}

	UE_LOGF(LogBaselineUcasTest, Display, "");

	// Step 9: Convert map to array for export
	TArray<FAssetInfo> Assets;
	Assets.Reserve(AssetMap.Num());
	for (const auto& Pair : AssetMap)
	{
		Assets.Add(Pair.Value);
	}

	// Sort by ChunkId for consistent ordering
	Assets.Sort([](const FAssetInfo& A, const FAssetInfo& B)
	{
		return LexToString(A.ChunkId) < LexToString(B.ChunkId);
	});

	// Step 10: Export to CSV
	if (!ExportToCSV(Assets, OutputPath))
	{
		return false;
	}

	UE_LOGF(LogBaselineUcasTest, Display, "");
	UE_LOGF(LogBaselineUcasTest, Display, "========================================");
	UE_LOGF(LogBaselineUcasTest, Display, "BASELINE TEST COMPLETE");
	UE_LOGF(LogBaselineUcasTest, Display, "========================================");
	UE_LOGF(LogBaselineUcasTest, Display, "Assets exported: %d", Assets.Num());
	UE_LOGF(LogBaselineUcasTest, Display, "Output file: %ls", *OutputPath);
	UE_LOGF(LogBaselineUcasTest, Display, "");

	return true;
}
