// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderAuditSession.h"
#include <atomic>

/**
 * Parsed representation of a NAS SHK filename.
 * Filename format: {Branch}-{Project}-{CL}-{TargetType}-{LibraryName}-{FormatName}.shk
 * Example: ++Project+Release-1.00-GameName-12345678-WindowsClient-GameName-PCD3D_SM6.shk
 */
struct FNasSHKEntry
{
	/** Full path to the SHK file (on NAS or local cache). */
	FString FullPath;

	/** Branch name, e.g. "++Project+Release-1.00". */
	FString Branch;

	/** Changelist number. */
	int32 CL = 0;

	/** Target type, e.g. "WindowsClient". */
	FString TargetType;

	/** Library name, e.g. "GameName". */
	FString LibraryName;

	/** Shader format, e.g. "PCD3D_SM6", "SF_METAL". */
	FString FormatName;

	/** True if this entry was found in the local cache (not the NAS). */
	bool bIsCached = false;
	
	/**
	 * Try to parse a NAS SHK filename into its components.
	 * Parses right-to-left: FormatName and LibraryName are the last two tokens,
	 * CL is the rightmost pure-integer token (digits only, len >= 5),
	 * TargetType spans between CL and LibraryName, Branch is everything before Project.
	 * @param Filename   Just the filename (no directory), e.g. "++Project+Release-1.00-GameName-12345678-WindowsClient-GameName-PCD3D_SM6.shk"
	 * @param OutEntry   Populated on success.
	 * @return true if parsed successfully.
	 */
	static bool Parse(const FString& Filename, FNasSHKEntry& OutEntry);
};

/**
 * A group of SHK entries sharing the same Branch + CL.
 * Represents one build's worth of shader data across all platforms/formats.
 * Each unique format (TargetType|FormatName) has a FSessionFileInventory
 * populated with SHK paths and sizes at scan time (bytecode discovered on expand).
 */
struct FNasBuildGroup
{
	FString Branch;
	int32 CL = 0;

	/** Build the canonical map key: "Branch|CL". */
	static FString MakeGroupKey(const FString& InBranch, int32 InCL)
	{
		return FString::Printf(TEXT("%s|%d"), *InBranch, InCL);
	}

	/** Convenience: key for this instance. */
	FString GetGroupKey() const { return MakeGroupKey(Branch, CL); }

	/** Build a format key with source suffix: "TargetType|FormatName|Local" or "TargetType|FormatName|NAS". */
	static FString MakeFormatKey(const FString& InTargetType, const FString& InFormatName, bool bIsCached)
	{
		return FString::Printf(TEXT("%s|%s|%s"), *InTargetType, *InFormatName, bIsCached ? TEXT("Local") : TEXT("NAS"));
	}

	/** True if the format key indicates a locally cached entry ("|Local" suffix). */
	static bool IsFormatCached(const FString& FormatKey)
	{
		return FormatKey.EndsWith(TEXT("|Local"));
	}

	/** Strip the source suffix to get the base format key: "TargetType|FormatName". */
	static FString GetBaseFormatKey(const FString& FormatKey)
	{
		int32 LastPipe;
		if (FormatKey.FindLastChar(TEXT('|'), LastPipe))
		{
			return FormatKey.Left(LastPipe);
		}
		return FormatKey;
	}

	/**
	 * Per-format inventories. Key = "TargetType|FormatName|Local" or "TargetType|FormatName|NAS".
	 * Populated at scan time with SHKFiles + SHKTotalBytes.
	 * BytecodeFiles populated later when the group is expanded.
	 */
	TMap<FString, FSessionFileInventory> Formats;

	/** Number of formats in this group that are locally cached. Compare with GetNumFormats() for full status. */
	int32 GetNumCachedFormats() const;

	/** Number of unique base formats (ignoring Local/NAS source). */
	int32 GetNumFormats() const;

	/**
	 * Resolve the best format key per base key (Local preferred over NAS).
	 * Returns a map of BaseKey -> best full FormatKey.
	 */
	TMap<FString, FString> ResolveBestFormats() const;
};

/** Result of a NAS scan operation. */
struct FNasScanResult
{
	/** All discovered build groups, sorted by CL descending (newest first). */
	TArray<FNasBuildGroup> Groups;

	/** Unique branch names found. */
	TArray<FString> Branches;

	/** Error message if the scan failed. Empty on success. */
	FString ErrorMessage;

	/** Total files visited during scan (including skipped/unparseable ones). */
	int32 FilesScanned = 0;

	/** Files that matched the branch prefix and were successfully parsed. */
	int32 FilesMatched = 0;
};

/**
 * Scans NAS and local cache directories for SHK files, parses filenames,
 * and groups them by Branch + CL.
 */
class FNasSHKScanner
{
public:
	/**
	 * Read the SHKFilesLocation from engine config.
	 * [/Script/Engine.ShaderCompilerStats] SHKFilesLocation=... (DefaultGame.ini)
	 */
	static FString GetNasLocationFromConfig();

	/**
	 * Read the build root path from ShaderAudit.ini.
	 * [NASBrowser] BuildRoot=... (e.g. \\server\share\Builds\Project)
	 */
	static FString GetBuildRootFromConfig();


	/** Returns {Project}/Saved/ShaderAuditCache/ */
	static FString GetDefaultCacheDir();

	/** Returns the relative cache subdirectory: {Branch}-CL-{CL}/{TargetType}/Metadata */
	static FString GetRelativeCacheSubDir(const FNasSHKEntry& Entry);

	/**
	 * Scan a directory for .shk files, parse them, and group by Branch+CL.
	 * Uses IterateDirectory for streaming enumeration.
	 *
	 * @param Directory       The directory to scan.
	 * @param bIsCached       If true, marks all entries as cached.
	 * @param OnProgress      Called periodically with incremental results (may be called from any thread).
	 * @return Grouped scan results.
	 */
	static FNasScanResult ScanDirectory(
		const FString& Directory,
		bool bIsCached,
		TFunction<void(const FNasScanResult&)> OnProgress = nullptr,
		const std::atomic<bool>* bCancelFlag = nullptr);

	/**
	 * Scan a local cache directory recursively for ShaderStableInfo-*.shk files.
	 * Extracts Branch/CL/TargetType from the directory path structure:
	 *   {CacheDir}/{Branch}-CL-{CL}/{TargetType}/Metadata/PipelineCaches/ShaderStableInfo-{Lib}-{Format}.shk
	 *
	 * @param CacheDir   Root cache directory to scan recursively.
	 * @return Grouped scan results with bIsCached=true.
	 */
	static FNasScanResult ScanLocalCache(const FString& CacheDir);

	/**
	 * Launch async scans of both local cache and NAS directories.
	 * Uses branch prefix filtering and posts incremental NAS results to the game thread.
	 *
	 * @param OnLocalComplete   Called on game thread when local scan finishes.
	 * @param OnNasProgress     Called on game thread as NAS entries are discovered (incremental).
	 * @param OnNasComplete     Called on game thread when NAS scan finishes.
	 */
	static void ScanAsync(
		const FString& NasDir,
		const FString& CacheDir,
		TFunction<void(FNasScanResult)> OnLocalComplete,
		TFunction<void(FNasScanResult)> OnNasProgress,
		TFunction<void(FNasScanResult)> OnNasComplete,
		TSharedPtr<std::atomic<bool>> CancelToken = nullptr);
};
