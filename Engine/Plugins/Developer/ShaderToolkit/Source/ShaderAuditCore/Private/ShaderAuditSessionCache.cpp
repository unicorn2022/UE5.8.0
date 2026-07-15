// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderAuditSession.h"

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "IShaderAuditExtension.h"
#include "NasSHKScanner.h"
#include "PipelineCacheUtilities.h"
#include "ShaderAuditProgress.h"
#include "ShaderAuditTypes.h"
#include "ShaderBytecodeDatabase.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogShaderAuditSessionCache, Log, All);

static FString ParseSessionName(const FString& SHKPath);

// ============================================================================
// Session File Caching (NAS -> local)
// ============================================================================

bool IsNetworkPath(const FString& Path)
{
#if PLATFORM_WINDOWS
	// Handle UNC paths (\\server\share\...)
	if (Path.StartsWith(TEXT("\\\\")))
	{
		return true;
	}

	// Handle mapped drives (P:\, Z:\, etc.) -- check if the drive is remote
	if (Path.Len() >= 2 && FChar::IsAlpha(Path[0]) && Path[1] == TEXT(':'))
	{
		TCHAR RootPath[4] = { Path[0], TEXT(':'), TEXT('\\'), TEXT('\0') };
		return ::GetDriveTypeW(RootPath) == DRIVE_REMOTE;
	}
#endif
	return false;
}

static void GatherFilesWithSizes(
	const FString& Dir,
	const TCHAR* Extension,
	const FString& SessionTag,
	TArray<FString>& OutFiles,
	int64& OutTotalBytes,
	TMap<FString, FFileStatData>& OutFileStats)
{
	IFileManager::Get().IterateDirectoryStat(*Dir, [&](const TCHAR* Filename, const FFileStatData& StatData) -> bool
	{
		if (StatData.bIsDirectory)
		{
			return true; // skip subdirectories
		}

		FString Name = FPaths::GetCleanFilename(Filename);
		if (!Name.EndsWith(Extension, ESearchCase::IgnoreCase))
		{
			return true;
		}

		if (!SessionTag.IsEmpty() && !Name.Contains(SessionTag, ESearchCase::IgnoreCase))
		{
			return true;
		}

		if (StatData.FileSize > 0)
		{
			FString FullPath(Filename);
			OutFileStats.Add(FullPath, StatData);
			OutFiles.Add(MoveTemp(FullPath));
			OutTotalBytes += StatData.FileSize;
		}
		
		return true;
	});
}
TArray<FString> FSessionFileInventory::FindSHKSiblings(const FString& SHKPath, bool bShowProgress)
{
	const FString Dir = FPaths::GetPath(SHKPath);
	const FString SessionName = ParseSessionName(SHKPath);

	if (SessionName.IsEmpty())
	{
		// Can't determine session name -- return just the input path.
		return { SHKPath };
	}

	FScopedProgressTask Progress(1.f,
		FText::Format(NSLOCTEXT("ShaderAudit", "FindingSiblings", "Discovering SHK siblings in:\n{0}"),
			FText::FromString(Dir)),
		!bShowProgress);

	const FString Suffix = TEXT("-") + SessionName + TEXT(".shk");

	TArray<FString> Result;
	TArray<FString> AllSHK;
	IFileManager::Get().FindFiles(AllSHK, *Dir, TEXT(".shk"));
	for (const FString& F : AllSHK)
	{
		if (F.EndsWith(Suffix, ESearchCase::IgnoreCase))
		{
			Result.Add(FPaths::Combine(Dir, F));
		}
	}

	// If nothing matched (unexpected), at least return the original.
	if (Result.Num() == 0)
	{
		Result.Add(SHKPath);
	}

	return Result;
}

FSessionFileInventory FSessionFileInventory::Gather(
	const TArrayView<const FString>& FilesToLoad,
	bool bShowProgress,
	const TMap<FString, FFileStatData>* PrecomputedStats)
{
	check(FilesToLoad.Num() > 0);

	const FString& PrimaryPath = FilesToLoad[0];

	const FString InputFilename = FPaths::GetCleanFilename(PrimaryPath);

	FNasSHKEntry ManifestEntry;

	FScopedProgressTask Progress(3.f,
		FText::Format(NSLOCTEXT("ShaderAudit", "GatherInventory", "Discovering session files in:\n{0}"),
			FText::FromString(FPaths::GetPath(PrimaryPath))),
		!bShowProgress);

	FSessionFileInventory Inv;

	Inv.SessionName = ParseSessionName(PrimaryPath);

	// --- SHK file discovery ---
	Progress.EnterProgressFrame(1.f,
		NSLOCTEXT("ShaderAudit", "GatherSHK", "Scanning SHK files..."));

	// Detect manifest paths: if the filename doesn't start with "ShaderStableInfo-",
	// it's a NAS manifest file ({Branch}-{Project}-{CL}-{TargetType}-{Lib}-{Format}.shk).
	const bool bIsManifestPath = !InputFilename.StartsWith(TEXT("ShaderStableInfo-"))
		&& FNasSHKEntry::Parse(InputFilename, ManifestEntry);

	// Build the relative cache subdirectory: {Branch}-CL-{CL}/{TargetType}/Metadata
	FString BuildMetadataDir;
	if (bIsManifestPath)
	{
		Inv.SessionName = ManifestEntry.FormatName;
		Inv.CacheSubDir = FNasSHKScanner::GetRelativeCacheSubDir(ManifestEntry);
		const FString BuildRoot = FNasSHKScanner::GetBuildRootFromConfig();
		BuildMetadataDir = BuildRoot.IsEmpty() ? FString() : FPaths::Combine(BuildRoot, Inv.CacheSubDir);
	}
	else
	{
		Inv.SessionName = ParseSessionName(PrimaryPath);
		// Try to extract from directory structure:
		// .../PipelineCaches -> Metadata -> {TargetType} -> {Branch}-CL-{CL}
		FString Dir = FPaths::GetPath(PrimaryPath);
		FPaths::NormalizeDirectoryName(Dir);
		if (FPaths::GetCleanFilename(Dir) == TEXT("PipelineCaches"))
		{
			const FString MetadataDir = FPaths::GetPath(Dir);
			if (FPaths::GetCleanFilename(MetadataDir) == TEXT("Metadata"))
			{
				BuildMetadataDir = MetadataDir;
				const FString TargetTypeDir = FPaths::GetPath(MetadataDir);
				const FString TargetTypeName = FPaths::GetCleanFilename(TargetTypeDir);
				const FString BuildDirName = FPaths::GetCleanFilename(FPaths::GetPath(TargetTypeDir));
				if (BuildDirName.Contains(TEXT("-CL-")) && !TargetTypeName.IsEmpty())
				{
					Inv.CacheSubDir = FPaths::Combine(BuildDirName, TargetTypeName, TEXT("Metadata"));
				}
			}
		}
	}

	// Callers provide the full sibling list (via FindSHKSiblings or NAS browser group).
	// For manifest paths, map to real ShaderStableInfo-* names if build dir exists.
	for (const FString& FilePath : FilesToLoad)
	{
		FString FullPath = FilePath;

		// Use pre-computed stat data if available, otherwise stat the file
		FFileStatData Stat;
		if (PrecomputedStats)
		{
			if (const FFileStatData* Found = PrecomputedStats->Find(FullPath))
			{
				Stat = *Found;
			}
		}
		if (Stat.FileSize < 0)
		{
			Stat = IFileManager::Get().GetStatData(*FullPath);
		}
		if (Stat.FileSize > 0)
		{
			// For manifest paths, record the desired dest filename (ShaderStableInfo-{Lib}-{Format}.shk)
			if (bIsManifestPath)
			{
				FNasSHKEntry SibEntry;
				if (FNasSHKEntry::Parse(FPaths::GetCleanFilename(FilePath), SibEntry))
				{
					Inv.DestFilenames.Add(FullPath,
						FString::Printf(TEXT("ShaderStableInfo-%s-%s.shk"),
							*SibEntry.LibraryName, *SibEntry.FormatName));
				}
			}

			Inv.FileStats.Add(FullPath, Stat);
			Inv.SHKFiles.Add(MoveTemp(FullPath));
			Inv.SHKTotalBytes += Stat.FileSize;
		}
	}

	Progress.EnterProgressFrame(1.f,
		NSLOCTEXT("ShaderAudit", "GatherBytecode", "Scanning shader bytecode..."));

	// Only mark bytecode as attempted if we have a directory to look in.
	// Leave at -1 ("never attempted") when BuildMetadataDir is empty so the
	// browser panel can re-fire discovery later with a resolved path.
	if (!BuildMetadataDir.IsEmpty())
	{
		Inv.BytecodeTotalBytes = 0; // 0 = tried, found nothing; >0 = found files
		FString ShaderLibDir = BuildMetadataDir / TEXT("ShaderLibrarySource");
		if (IFileManager::Get().DirectoryExists(*ShaderLibDir))
		{
			const FString SessionTag = !Inv.SessionName.IsEmpty()
				? (TEXT("-") + Inv.SessionName)
				: FString();

			GatherFilesWithSizes(ShaderLibDir, TEXT(".ushaderbytecode"), SessionTag, Inv.BytecodeFiles, Inv.BytecodeTotalBytes, Inv.FileStats);
		}
		else
		{
			Progress.EnterProgressFrame(1.f);
		}
	}

	return Inv;
}

FSessionFileInventory CacheSessionFiles(
	const FSessionFileInventory& Inventory,
	const FString& DestDir,
	const FSessionCacheOptions& Options)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CacheSessionFiles);

	check(!DestDir.IsEmpty());

	FSessionFileInventory Result;
	Result.SessionName = Inventory.SessionName;

	// Build the list of (source, dest) pairs to copy
	struct FFileCopyEntry
	{
		FString SrcPath;
		FString DstPath;
		int64 FileSize;
	};
	TArray<FFileCopyEntry> FilesToCopy;

	auto QueueFiles = [&](const TArray<FString>& SrcFiles, const FString& DestSubDir)
	{
		TArray<FString> LocalPaths;
		for (const FString& Src : SrcFiles)
		{
			const FString* RemappedName = Inventory.DestFilenames.Find(Src);
			FString Dst = FPaths::Combine(DestSubDir, RemappedName ? *RemappedName : FPaths::GetCleanFilename(Src));
			const FFileStatData* FoundStat = Inventory.FileStats.Find(Src);
			const int64 SrcSize = FoundStat ? FoundStat->FileSize : 0;

			// Skip if already cached with matching size (local stat only)
			int64 DstSize = IFileManager::Get().FileSize(*Dst);
			if (DstSize == SrcSize && SrcSize > 0)
			{
				UE_LOGF(LogShaderAuditSessionCache, Display, "Cache hit (size match): %ls", *FPaths::GetCleanFilename(Src));
			}
			else if (SrcSize > 0)
			{
				FilesToCopy.Add({ Src, Dst, SrcSize });
			}

			LocalPaths.Add(MoveTemp(Dst));
		}
		return LocalPaths;
	};

	const FString LocalSHKDir = FPaths::Combine(DestDir, TEXT("PipelineCaches"));
	if (Options.bIncludeSHK)
	{
		Result.SHKFiles = QueueFiles(Inventory.SHKFiles, LocalSHKDir);
		Result.SHKTotalBytes = Inventory.SHKTotalBytes;
	}

	const FString LocalShaderLibDir = FPaths::Combine(DestDir, TEXT("ShaderLibrarySource"));

	if (Options.bIncludeBytecode)
	{
		Result.BytecodeFiles = QueueFiles(Inventory.BytecodeFiles, LocalShaderLibDir);
		Result.BytecodeTotalBytes = Inventory.BytecodeTotalBytes;
	}

	// Nothing to copy? Return the remapped paths.
	if (FilesToCopy.IsEmpty())
	{
		UE_LOGF(LogShaderAuditSessionCache, Display, "All files already cached.");
		return Result;
	}

	// Compute total bytes to copy
	int64 TotalBytesToCopy = 0;
	for (const FFileCopyEntry& Entry : FilesToCopy)
	{
		TotalBytesToCopy += Entry.FileSize;
	}

	UE_LOGF(LogShaderAuditSessionCache, Display, "Caching %d file(s), %.1f MB total",
		FilesToCopy.Num(), static_cast<double>(TotalBytesToCopy) / (1024.0 * 1024.0));

	// Ensure destination directories exist
	if (Options.bIncludeSHK)
	{
		if (!IFileManager::Get().MakeDirectory(*LocalSHKDir, true))
		{
			UE_LOGF(LogShaderAuditSessionCache, Error, "Failed to create directory: %ls", *LocalSHKDir);
			return Result;
		}
	}
	if (Options.bIncludeBytecode)
	{
		if (!IFileManager::Get().MakeDirectory(*LocalShaderLibDir, true))
		{
			UE_LOGF(LogShaderAuditSessionCache, Error, "Failed to create directory: %ls", *LocalShaderLibDir);
			return Result;
		}
	}

	// Shared progress counter: worker threads atomically add bytes, GT reads it
	std::atomic<int64> BytesCopied{0};
	std::atomic<int32> FilesDone{0};
	std::atomic<bool> bCopyDone{false};
	std::atomic<bool> bCancelled{false};

	// Launch parallel copy on a worker thread
	const int32 NumFiles = FilesToCopy.Num();
	TFuture<void> CopyTask = Async(EAsyncExecution::Thread, [&FilesToCopy, &BytesCopied, &FilesDone, &bCopyDone, &bCancelled]()
	{
		ParallelFor(FilesToCopy.Num(), [&](int32 Idx)
		{
			if (bCancelled.load(std::memory_order_relaxed))
			{
				return;
			}

			const FFileCopyEntry& Entry = FilesToCopy[Idx];

			struct FAtomicCopyProgress : FCopyProgress
			{
				std::atomic<int64>& BytesCopied;
				std::atomic<bool>& bCancelled;
				int64 FileSize;
				float LastFraction = 0.f;

				FAtomicCopyProgress(std::atomic<int64>& InBytesCopied, int64 InFileSize, std::atomic<bool>& InCancelled)
					: BytesCopied(InBytesCopied), bCancelled(InCancelled), FileSize(InFileSize) {}

				virtual bool Poll(float Fraction) override
				{
					if (bCancelled.load(std::memory_order_relaxed))
					{
						return false; // Abort this file copy
					}
					float Delta = Fraction - LastFraction;
					LastFraction = Fraction;
					if (Delta > 0.f)
					{
						BytesCopied.fetch_add(static_cast<int64>(Delta * FileSize), std::memory_order_relaxed);
					}
					return true;
				}
			};

			FAtomicCopyProgress Progress(BytesCopied, Entry.FileSize, bCancelled);
			uint32 CopyResult = IFileManager::Get().Copy(
				*Entry.DstPath, *Entry.SrcPath,
				true,  // Replace
				true,  // EvenIfReadOnly
				false, // Attributes
				&Progress);

			if (CopyResult != COPY_OK)
			{
				UE_LOGF(LogShaderAuditSessionCache, Error, "Failed to copy: %ls", *Entry.SrcPath);
			}

			FilesDone.fetch_add(1, std::memory_order_relaxed);
		});

		bCopyDone.store(true, std::memory_order_release);
	});

	// GT: poll progress every 0.5s and update the progress bar
	{
		FScopedProgressTask ProgressTask(100.f,
			FText::Format(
				NSLOCTEXT("ShaderAudit", "CachingFiles", "Caching {0} file(s) from network..."),
				FText::AsNumber(NumFiles)));

		float LastPct = 0.f;
		while (!bCopyDone.load(std::memory_order_acquire))
		{
			FPlatformProcess::Sleep(0.5f);

			if (ProgressTask.ShouldCancel())
			{
				bCancelled.store(true, std::memory_order_release);
			}

			int64 Copied = BytesCopied.load(std::memory_order_relaxed);
			float Pct = (TotalBytesToCopy > 0)
				? static_cast<float>(static_cast<double>(Copied) / static_cast<double>(TotalBytesToCopy) * 100.0)
				: 0.f;
			Pct = FMath::Min(Pct, 99.f); // reserve last tick for completion

			float Delta = Pct - LastPct;
			if (Delta > 0.f)
			{
				int32 Done = FilesDone.load(std::memory_order_relaxed);
				ProgressTask.EnterProgressFrame(Delta,
					FText::Format(
						NSLOCTEXT("ShaderAudit", "CacheProgress", "Copied {0}/{1} files ({2} MB / {3} MB)"),
						FText::AsNumber(Done),
						FText::AsNumber(NumFiles),
						FText::AsNumber(static_cast<int64>(Copied / (1024 * 1024))),
						FText::AsNumber(static_cast<int64>(TotalBytesToCopy / (1024 * 1024)))));
				LastPct = Pct;
			}
		}

		// Final tick
		if (LastPct < 100.f)
		{
			ProgressTask.EnterProgressFrame(100.f - LastPct,
				NSLOCTEXT("ShaderAudit", "CacheDone", "Caching complete."));
		}
	}

	// Ensure the async task has fully completed before we return
	// (locals captured by reference must outlive the task)
	CopyTask.Wait();

	if (bCancelled.load(std::memory_order_relaxed))
	{
		// Clean up partially copied files
		for (const FFileCopyEntry& Entry : FilesToCopy)
		{
			IFileManager::Get().Delete(*Entry.DstPath, false, false, true);
		}
		UE_LOGF(LogShaderAuditSessionCache, Display, "Caching cancelled by user, cleaned up partial files.");
		return FSessionFileInventory();
	}

	UE_LOGF(LogShaderAuditSessionCache, Display, "Cached %d file(s) to %ls", NumFiles, *DestDir);
	return Result;
}

// ============================================================================
// Cache Session Dialog
// ============================================================================

#define LOCTEXT_NAMESPACE "ShaderAuditCache"

bool ShowCacheSessionDialog(
	const FSessionFileInventory& Inventory,
	FSessionCacheOptions& OutOptions,
	FString& OutCacheDir)
{
	OutOptions.bIncludeSHK = Inventory.SHKFiles.Num() > 0;
	if (!OutOptions.bIncludeSHK)
	{
		return false;
	}

	OutOptions.bIncludeBytecode = Inventory.BytecodeFiles.Num() > 0;
	// Default cache root
	const FString CacheRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShaderAuditCache"));

	// Use CacheSubDir from Gather() if available, otherwise fall back to flat cache root.
	OutCacheDir = !Inventory.CacheSubDir.IsEmpty()
		? FPaths::Combine(CacheRoot, Inventory.CacheSubDir)
		: CacheRoot;

	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	bool bResult = false;

	TSharedPtr<SWindow> DialogWindow;
	TSharedPtr<SEditableTextBox> CacheDirBox;
	TSharedPtr<SCheckBox> CheckSHK;
	TSharedPtr<SCheckBox> CheckBytecode;

	auto MakeRow = [](const FText& Label, int64 Bytes, int32 FileCount,
		TSharedPtr<SCheckBox>& OutCheckBox, bool bEnabled, bool bChecked) -> TSharedRef<SHorizontalBox>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(OutCheckBox, SCheckBox)
				.IsEnabled(bEnabled)
				.IsChecked(bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("CacheRowFmt", "{0} ({1} file(s), {2})"),
					Label,
					FText::AsNumber(FileCount),
					FText::FromString(UE::ShaderAudit::Utils::FormatBytes(Bytes))))
				.IsEnabled(bEnabled)
			];
	};

	DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("CacheDialogTitle", "Cache Session Files"))
		.ClientSize(FVector2D(500, 300))
		.IsTopmostWindow(true)
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::FixedSize)
		[
			SNew(SVerticalBox)

			// Source info
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 12.f, 12.f, 4.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("CacheSource", "Session: {0} (network drive detected)"),
					FText::FromString(Inventory.SessionName)))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 8.f, 12.f, 4.f)
			[
				SNew(SSeparator)
			]

			// File category checkboxes
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 4.f, 12.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CacheFilesLabel", "Files to cache:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.f, 4.f, 12.f, 2.f)
			[
				MakeRow(
					LOCTEXT("CacheSHK", "SHK files"),
					Inventory.SHKTotalBytes,
					Inventory.SHKFiles.Num(),
					CheckSHK,
					false,
					true)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.f, 2.f, 12.f, 2.f)
			[
				MakeRow(
					LOCTEXT("CacheBytecode", "Shader bytecode (.ushaderbytecode)"),
					Inventory.BytecodeTotalBytes,
					Inventory.BytecodeFiles.Num(),
					CheckBytecode,
					Inventory.BytecodeFiles.Num() > 0,
					Inventory.BytecodeFiles.Num() > 0)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 8.f, 12.f, 4.f)
			[
				SNew(SSeparator)
			]

			// Cache directory
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 4.f, 12.f, 2.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CacheDirLabel", "Cache to:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.f, 2.f, 12.f, 8.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SAssignNew(CacheDirBox, SEditableTextBox)
					.Text(FText::FromString(OutCacheDir))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("BrowseBtn", "..."))
					.OnClicked_Lambda([&CacheDirBox, &DialogWindow]()
					{
						IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
						if (Platform)
						{
							FString SelectedDir;
							if (Platform->OpenDirectoryDialog(
								DialogWindow.IsValid() ? DialogWindow->GetNativeWindow()->GetOSWindowHandle() : nullptr,
								TEXT("Select cache directory"),
								CacheDirBox->GetText().ToString(),
								SelectedDir))
							{
								CacheDirBox->SetText(FText::FromString(SelectedDir));
							}
						}
						return FReply::Handled();
					})
				]
			]

			// Buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(12.f, 4.f, 12.f, 12.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CacheBtn", "Cache"))
					.OnClicked_Lambda([&bResult, &OutOptions, &OutCacheDir, &DialogWindow,
						&CheckSHK, &CheckBytecode, &CacheDirBox]()
					{
						OutOptions.bIncludeSHK = true;
						OutOptions.bIncludeBytecode = CheckBytecode.IsValid() && CheckBytecode->IsChecked();
						OutCacheDir = CacheDirBox->GetText().ToString();
						if (OutCacheDir.IsEmpty())
						{
							return FReply::Handled();
						}
						bResult = true;
						DialogWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelBtn", "Cancel"))
					.OnClicked_Lambda([&DialogWindow]()
					{
						DialogWindow->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		];

	FSlateApplication::Get().AddModalWindow(DialogWindow.ToSharedRef(),
		FSlateApplication::Get().GetActiveTopLevelRegularWindow());

	return bResult;
}

// ============================================================================
// Cache Cleanup
// ============================================================================

bool ClearCachedSessionDir(const FString& DirToDelete)
{
	if (DirToDelete.IsEmpty() || !IFileManager::Get().DirectoryExists(*DirToDelete))
	{
		return false;
	}

	// Safety: only delete known shader cache file types. Never do a blind recursive
	// directory delete -- if DirToDelete somehow points at the wrong place we don't
	// want to destroy arbitrary user data.
	static const TArray<FString> AllowedExtensions = { TEXT(".shk"), TEXT(".ushaderbytecode") };

	IFileManager& FM = IFileManager::Get();
	int32 FilesDeleted = 0;
	TArray<FString> Directories;

	// Delete only files with allowed extensions, collect directories for cleanup.
	FM.IterateDirectoryRecursively(*DirToDelete, [&](const TCHAR* Path, bool bIsDir) -> bool
	{
		if (bIsDir)
		{
			Directories.Add(Path);
			return true;
		}
		const FStringView PathView(Path);
		for (const FString& Ext : AllowedExtensions)
		{
			if (PathView.EndsWith(Ext, ESearchCase::IgnoreCase))
			{
				FM.Delete(Path, false, false, true);
				++FilesDeleted;
				break;
			}
		}
		return true;
	});

	// Sort longest path first (deepest directories first).
	Directories.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
	for (const FString& Dir : Directories)
	{
		FM.DeleteDirectory(*Dir, false, false); // non-recursive, fails if not empty
	}
	// Try removing the root itself if it's now empty.
	FM.DeleteDirectory(*DirToDelete, false, false);

	UE_LOGF(LogShaderAuditSessionCache, Display, "Cleared %d cached file(s) under: %ls",
		FilesDeleted, *DirToDelete);
	return FilesDeleted > 0;
}

#undef LOCTEXT_NAMESPACE

/**
 * Parse the session name from an SHK filename.
 * Expected pattern: ShaderStableInfo-{SubLibrary}-{SessionName}.shk
 * Returns the SessionName portion, or empty string if the pattern doesn't match.
 */
static FString ParseSessionName(const FString& SHKPath)
{
	FString Filename = FPaths::GetBaseFilename(SHKPath);

	static const FString Prefix = TEXT("ShaderStableInfo-");
	if (!Filename.StartsWith(Prefix))
	{
		return FString();
	}

	// Strip prefix: "GameName-PCD3D_SM6"
	FString Remainder = Filename.Mid(Prefix.Len());

	// Find the LAST hyphen -- everything after it is the session name
	// Sub-library names don't contain hyphens, but session names like PCD3D_SM6 don't either.
	// However the sub-library could be "GameName" and session "PCD3D_SM6".
	int32 HyphenIdx = INDEX_NONE;
	Remainder.FindLastChar(TEXT('-'), HyphenIdx);
	if (HyphenIdx == INDEX_NONE)
	{
		return FString();
	}

	return Remainder.Mid(HyphenIdx + 1);
}

// ============================================================================
// LoadFromInventory
// ============================================================================

TArray<TSharedPtr<FShaderAuditSession>> FShaderAuditSession::LoadFromInventory(
	const FSessionFileInventory& Inventory)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FShaderAuditSession::LoadFromInventory);

	if (Inventory.SHKFiles.Num() == 0)
	{
		return {};
	}

	// --- Resolve caching for network paths ---
	TArray<FString> SHKPaths = Inventory.SHKFiles;
	TArray<FString> BytecodePaths = Inventory.BytecodeFiles;

	if (IsNetworkPath(SHKPaths[0]))
	{
		FSessionCacheOptions CacheOptions;
		FString CacheDir;
		if (ShowCacheSessionDialog(Inventory, CacheOptions, CacheDir))
		{
			FSessionFileInventory Cached = CacheSessionFiles(Inventory, CacheDir, CacheOptions);
			SHKPaths = Cached.SHKFiles;
			BytecodePaths = Cached.BytecodeFiles;
		}
		else
		{
			return {};
		}
	}

	if (SHKPaths.Num() == 0)
	{
		return {};
	}

	// --- Load session from resolved paths ---
	TArray<TSharedPtr<FShaderAuditSession>> Results;

	const FString& PrimaryPath = SHKPaths[0];
	const FString SessionName = ParseSessionName(PrimaryPath);

	FScopedProgressTask Progress(5.f,
		FText::Format(NSLOCTEXT("ShaderAudit", "LoadingProgress", "Loading session {0}..."),
			FText::FromString(SessionName)));

	TSharedPtr<FShaderAuditSession> Session = MakeShared<FShaderAuditSession>();
	Session->Filename = PrimaryPath;
	Session->SessionName = SessionName;

	// =================================================================
	// Phase 1: Parallel I/O -- SHK loading + shader archive import
	// =================================================================

	Progress.EnterProgressFrame(1.f,
		FText::Format(NSLOCTEXT("ShaderAudit", "LoadingParallel", "Loading session data for {0}..."),
			FText::FromString(SessionName)));

	// --- SHK loading (async) ---
	TFuture<TArray<TArray<FStableShaderKeyAndValue>>> SHKFuture = Async(EAsyncExecution::TaskGraph,
		[&SHKPaths]()
		{
			TArray<TArray<FStableShaderKeyAndValue>> LoadedChunks;
			LoadedChunks.SetNum(SHKPaths.Num());

			ParallelFor(SHKPaths.Num(), [&](int32 ChunkIdx)
				{
					if (UE::PipelineCacheUtilities::LoadStableKeysFile(SHKPaths[ChunkIdx], LoadedChunks[ChunkIdx]))
					{
						UE_LOGF(LogShaderAuditSessionCache, Display, "  Loaded %ls (%d entries)",
							*FPaths::GetCleanFilename(SHKPaths[ChunkIdx]), LoadedChunks[ChunkIdx].Num());
					}
					else
					{
						UE_LOGF(LogShaderAuditSessionCache, Error, "  Failed to load SHK: %ls", *SHKPaths[ChunkIdx]);
					}
				});

			return LoadedChunks;
		});

	// --- Shader archives (async) -- use pre-discovered bytecode files from inventory ---
	TFuture<TSharedPtr<FShaderBytecodeDatabase>> ArchiveFuture = Async(EAsyncExecution::TaskGraph,
		[&BytecodePaths]() -> TSharedPtr<FShaderBytecodeDatabase>
		{
			if (BytecodePaths.Num() == 0)
			{
				return nullptr;
			}

			TSharedPtr<FShaderBytecodeDatabase> DB = MakeShared<FShaderBytecodeDatabase>();
			for (const FString& ArchivePath : BytecodePaths)
			{
				DB->ImportShaderArchive(ArchivePath);
			}

			UE_LOGF(LogShaderAuditSessionCache, Display, "Imported %d shader archive(s)",
				BytecodePaths.Num());

			return DB;
		});

	// =================================================================
	// Phase 2: Wait for SHK, merge, build index
	// =================================================================

	Progress.EnterProgressFrame(1.f,
		FText::Format(NSLOCTEXT("ShaderAudit", "BuildingIndex", "Building index for {0}..."),
			FText::FromString(SessionName)));

	TArray<TArray<FStableShaderKeyAndValue>> LoadedChunks = SHKFuture.Get();

	{
		bool bAnyLoaded = false;
		for (const auto& Chunk : LoadedChunks)
		{
			if (Chunk.Num() > 0) { bAnyLoaded = true; break; }
		}
		if (!bAnyLoaded)
		{
			Progress.EnterProgressFrame(3.f);
			ArchiveFuture.Get();
			return Results;
		}
	}

	int32 TotalEntries = 0;
	for (const TArray<FStableShaderKeyAndValue>& Chunk : LoadedChunks)
	{
		TotalEntries += Chunk.Num();
	}
	Session->StableShaderKeyAndValueArray.Reserve(TotalEntries);
	for (TArray<FStableShaderKeyAndValue>& Chunk : LoadedChunks)
	{
		Session->StableShaderKeyAndValueArray.Append(MoveTemp(Chunk));
	}
	LoadedChunks.Empty();

	UE_LOGF(LogShaderAuditSessionCache, Display, "Session '%ls': %d total entries from %d file(s)",
		*SessionName, TotalEntries, SHKPaths.Num());

	Session->BuildIndex();

	Results.Add(Session);

	// =================================================================
	// Phase 3: Attach shader archives
	// =================================================================

	Progress.EnterProgressFrame(1.f,
		NSLOCTEXT("ShaderAudit", "AttachingArchives", "Attaching shader archives..."));

	TSharedPtr<FShaderBytecodeDatabase> DB = ArchiveFuture.Get();
	if (DB.IsValid())
	{
		Session->BytecodeDatabase = DB;
		for (int32 i = 0; i < Session->UniqueShaders.Num(); ++i)
		{
			const FShaderHash& Hash = Session->GetShaderEntry(i).OutputHash;
			if (const FShaderBytecodeInfo* Info = DB->Find(Hash))
			{
				Session->UniqueShaders[i].CompressedSize = Info->CompressedSize;
				Session->UniqueShaders[i].UncompressedSize = Info->UncompressedSize;
				Session->UniqueShaders[i].ArchiveCount = Info->ArchiveCount;
			}
		}
	}

	// Notify extensions
	TArray<IShaderAuditExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<IShaderAuditExtension>(IShaderAuditExtension::FeatureName);
	for (IShaderAuditExtension* Extension : Extensions)
	{
		Extension->OnSessionLoaded(Session);
	}

	return Results;
}
