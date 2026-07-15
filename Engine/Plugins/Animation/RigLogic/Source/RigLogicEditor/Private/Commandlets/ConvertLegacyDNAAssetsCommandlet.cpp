// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/ConvertLegacyDNAAssetsCommandlet.h"

#include "DNA.h"
#include "DNAAsset.h"
#include "DNAAssetUserData.h"
#include "DNAImporter.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "Engine/SkeletalMesh.h"
#include "EditorFramework/AssetImportData.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlHelpers.h"

#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogConvertLegacyDNA, Log, All);

namespace UE::RigLogic::ConvertLegacyDNA
{
	static bool ParseSwitch(const FString& Params, const TCHAR* SwitchName)
	{
		// FParse::Param is the canonical engine helper for detecting a `-Switch`
		// (or `/Switch`) on a command line. Equivalent to running
		// UCommandlet::ParseCommandLine() and testing Switches.Contains(SwitchName),
		// but cheaper and the convention used by the rest of UnrealEd.
		return FParse::Param(*Params, SwitchName);
	}

	static FString ParseValue(const FString& Params, const TCHAR* Key, const FString& Default)
	{
		FString Value;
		if (FParse::Value(*Params, Key, Value))
		{
			Value.TrimQuotesInline();
			Value.TrimStartAndEndInline();
			return Value;
		}
		return Default;
	}

	static void PrintHelp()
	{
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("ConvertLegacyDNAAssets - Convert legacy UDNAAsset user-data on SkeletalMeshes"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("                          into standalone UDNA assets."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("Usage:"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  UnrealEditor-Cmd.exe <Project>.uproject -run=ConvertLegacyDNAAssets [options]"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("Options:"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -PathFilter=<MountedPath>   Limit scan to this content path (default: /Game)."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("                              Example: -PathFilter=/Game/MetaHumans"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -DryRun                     Report what would change but do not save."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -NoSCC                      Do not check files out from source control."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -ReimportFromSource         Re-import each mesh's original .dna file (when"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("                              available) instead of converting in place."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -Verbose                    Verbose per-asset logging."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -ResaveDNA                  Phase 2: also load and resave every UDNA / orphan"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("                              UDNAAsset under the path filter so the on-disk"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("                              FDNAAssetCustomVersion is bumped to LatestVersion."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("                              Equivalent to running -run=ResavePackages afterwards."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -help                       Print this help and exit."));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("If you do not pass -ResaveDNA, follow up with -run=ResavePackages instead:"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  -run=ResavePackages -PackageFolder=/Game/MetaHumans -AutoCheckOut -AutoCheckIn"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
	}

	static bool TrySavePackage(UPackage* Package, bool bUseSCC)
	{
		if (!Package)
		{
			return false;
		}

		const FString PackageName = Package->GetName();
		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetAssetPackageExtension()))
		{
			UE_LOG(LogConvertLegacyDNA, Error, TEXT("Failed to resolve filename for package %s"), *PackageName);
			return false;
		}

		// Make sure the directory exists for newly-created packages.
		const FString DirName = FPaths::GetPath(Filename);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.DirectoryExists(*DirName))
		{
			PlatformFile.CreateDirectoryTree(*DirName);
		}

		if (bUseSCC && ISourceControlModule::Get().IsEnabled() && ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			if (PlatformFile.FileExists(*Filename))
			{
				if (!USourceControlHelpers::CheckOutOrAddFile(Filename, /*bSilent=*/true))
				{
					UE_LOG(LogConvertLegacyDNA, Warning, TEXT("Source control check-out failed for %s; attempting save anyway."), *Filename);
				}
			}
			// Newly-created packages will be added by the source control provider on next submit.
		}
		else if (PlatformFile.FileExists(*Filename) && PlatformFile.IsReadOnly(*Filename))
		{
			// Not under SCC; clear the read-only bit so the save can proceed.
			PlatformFile.SetReadOnly(*Filename, false);
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		SaveArgs.Error = GError;

		const bool bSaved = UPackage::SavePackage(Package, /*Asset=*/nullptr, *Filename, SaveArgs);
		if (!bSaved)
		{
			UE_LOG(LogConvertLegacyDNA, Error, TEXT("SavePackage failed for %s"), *PackageName);
		}
		return bSaved;
	}
}

UConvertLegacyDNAAssetsCommandlet::UConvertLegacyDNAAssetsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
	FastExit = false;
}

int32 UConvertLegacyDNAAssetsCommandlet::Main(const FString& Params)
{
	using namespace UE::RigLogic::ConvertLegacyDNA;

	if (ParseSwitch(Params, TEXT("help")))
	{
		PrintHelp();
		return 0;
	}

	const FString PathFilter        = ParseValue(Params, TEXT("PathFilter="), TEXT("/Game"));
	const bool    bDryRun           = ParseSwitch(Params, TEXT("DryRun"));
	const bool    bNoSCC            = ParseSwitch(Params, TEXT("NoSCC"));
	const bool    bReimportFromSrc  = ParseSwitch(Params, TEXT("ReimportFromSource"));
	const bool    bVerbose          = ParseSwitch(Params, TEXT("Verbose"));
	const bool    bResaveDNA        = ParseSwitch(Params, TEXT("ResaveDNA"));
	const bool    bUseSCC           = !bNoSCC;

	UE_LOG(LogConvertLegacyDNA, Display, TEXT("ConvertLegacyDNAAssets starting"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  PathFilter         : %s"), *PathFilter);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  DryRun             : %s"), bDryRun ? TEXT("true") : TEXT("false"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  SourceControl      : %s"), bUseSCC ? TEXT("on") : TEXT("off"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  ReimportFromSource : %s"), bReimportFromSrc ? TEXT("true") : TEXT("false"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  ResaveDNA          : %s"), bResaveDNA ? TEXT("true") : TEXT("false"));

	// Wait for the asset registry to finish discovering everything under the path filter.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	{
		TArray<FString> ScanPaths = { PathFilter };
		AssetRegistry.ScanPathsSynchronous(ScanPaths, /*bForceRescan=*/false);
		AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/false);
		AssetRegistry.WaitForCompletion();
	}

	// Find every SkeletalMesh under the path filter; subclasses included.
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(FName(*PathFilter));
	Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

	TArray<FAssetData> SkeletalMeshes;
	AssetRegistry.GetAssets(Filter, SkeletalMeshes);

	UE_LOG(LogConvertLegacyDNA, Display, TEXT("Discovered %d SkeletalMesh asset(s) under '%s'"), SkeletalMeshes.Num(), *PathFilter);

	int32 NumScanned         = 0;
	int32 NumConverted       = 0;
	int32 NumReimported      = 0;
	int32 NumAlreadyMigrated = 0;
	int32 NumSkipped         = 0;
	int32 NumFailed          = 0;
	int32 NumSaved           = 0;
	int32 NumSaveFailed      = 0;

	const int32 GarbageCollectInterval = 50;
	int32 SinceLastGC = 0;

	// Track packages already saved by Phase 1 so Phase 2 doesn't resave them.
	TSet<FName> SavedPackageNames;

	for (int32 Index = 0; Index < SkeletalMeshes.Num(); ++Index)
	{
		const FAssetData& AssetData = SkeletalMeshes[Index];
		++NumScanned;

		if (bVerbose)
		{
			UE_LOG(LogConvertLegacyDNA, Verbose, TEXT("[%d/%d] %s"), Index + 1, SkeletalMeshes.Num(), *AssetData.GetObjectPathString());
		}

		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
		if (!SkeletalMesh)
		{
			UE_LOG(LogConvertLegacyDNA, Warning, TEXT("Failed to load %s; skipping"), *AssetData.GetObjectPathString());
			++NumFailed;
			continue;
		}

		UDNAAsset* LegacyDNA = SkeletalMesh->GetAssetUserData<UDNAAsset>();
		if (LegacyDNA == nullptr)
		{
			// Idempotent: nothing to do for meshes that already use UDNAAssetUserData
			// (or that simply have no DNA data at all).
			UE_LOG(LogConvertLegacyDNA, Verbose, TEXT("No UDNAAsset in %s; skipping"), *AssetData.GetObjectPathString());
			++NumSkipped;
			continue;
		}

		const FString MeshPath = SkeletalMesh->GetPathName();

		// Pre-flight: detect a name collision at <Mesh>_DNA in the same folder.
		const FString TargetDNAName = FString::Printf(TEXT("%s_DNA"), *SkeletalMesh->GetName());
		const FString TargetDNAFolder = FPackageName::GetLongPackagePath(SkeletalMesh->GetOutermost()->GetName());
		const FString TargetDNAPackagePath = TargetDNAFolder / TargetDNAName;
		const FSoftObjectPath TargetDNAObjectPath(FString::Printf(TEXT("%s.%s"), *TargetDNAPackagePath, *TargetDNAName));
		const FAssetData ExistingTargetDNA = AssetRegistry.GetAssetByObjectPath(TargetDNAObjectPath);
		if (ExistingTargetDNA.IsValid() && !bReimportFromSrc)
		{
			UE_LOG(LogConvertLegacyDNA, Display, TEXT("Skipping %s: target asset %s already exists. Re-run with -ReimportFromSource to rebuild."), *MeshPath, *TargetDNAPackagePath);
			++NumAlreadyMigrated;
			continue;
		}

		// We hold strong refs to the produced packages only for the duration of this iteration,
		// so they remain rooted across the inline save and the periodic GC at the end of the loop.
		TStrongObjectPtr<UPackage> MeshPackage(SkeletalMesh->GetOutermost());
		TStrongObjectPtr<UPackage> NewDNAPackage;

		bool bConvertedThisMesh = false;
		bool bWentReimportPath  = false;

		// --- Path A: -ReimportFromSource (when a usable .dna source file exists) ---
		if (bReimportFromSrc)
		{
			FString SourceFile;
			if (LegacyDNA->AssetImportData)
			{
				SourceFile = LegacyDNA->AssetImportData->GetFirstFilename();
			}

			if (!SourceFile.IsEmpty() && FPaths::FileExists(SourceFile))
			{
				bWentReimportPath = true;

				if (bDryRun)
				{
					UE_LOG(LogConvertLegacyDNA, Display, TEXT("[DryRun] Would re-import DNA for %s from %s"), *MeshPath, *SourceFile);
					++NumReimported;
					continue;
				}

				TStrongObjectPtr<UDNAImporter> Reimporter(NewObject<UDNAImporter>());
				const bool bReimportOK = Reimporter->ImportDNAAutomated(SourceFile, SkeletalMesh, /*bReplaceExisting=*/true);
				if (!bReimportOK)
				{
					UE_LOG(LogConvertLegacyDNA, Error, TEXT("ImportDNAAutomated failed for %s (source: %s); skipping."), *MeshPath, *SourceFile);
					++NumFailed;
					continue;
				}

				if (UDNAAssetUserData* NewUserData = SkeletalMesh->GetAssetUserData<UDNAAssetUserData>())
				{
					if (UDNA* NewDNA = NewUserData->DNAAsset)
					{
						NewDNAPackage.Reset(NewDNA->GetOutermost());
						bConvertedThisMesh = true;
					}
				}

				if (!bConvertedThisMesh)
				{
					UE_LOG(LogConvertLegacyDNA, Error, TEXT("ImportDNAAutomated did not produce a UDNAAssetUserData on %s; skipping."), *MeshPath);
					++NumFailed;
					continue;
				}

				++NumReimported;
			}
			else
			{
				UE_LOG(LogConvertLegacyDNA, Warning, TEXT("Source .dna not available for %s (legacy import path: '%s'); falling back to in-place conversion."), *MeshPath, *SourceFile);
				// Fall through to Path B below.
			}
		}

		// --- Path B: in-place conversion via UDNAImporter::ConvertFromLegacyAssetUserData ---
		if (!bConvertedThisMesh && !bWentReimportPath)
		{
			// We deferred the collision check above when -ReimportFromSource was set
			// (Path A is allowed to overwrite). If we fell through to Path B because
			// the source .dna was missing, re-honour the collision: Path B's
			// ConvertFromLegacyAssetUserData would call CreatePackage()+Rename() on
			// the existing target and stomp any custom DNAConfig / RigLogicConfiguration
			// stored on the standalone UDNA.
			if (ExistingTargetDNA.IsValid())
			{
				UE_LOG(LogConvertLegacyDNA, Warning,
					TEXT("Skipping %s: -ReimportFromSource was requested but the source .dna is unavailable, "
						 "and target %s already exists. Refusing to overwrite via in-place conversion. "
						 "Restore the source .dna or delete the existing target and re-run."),
					*MeshPath, *TargetDNAPackagePath);
				++NumAlreadyMigrated;
				continue;
			}

			if (bDryRun)
			{
				UE_LOG(LogConvertLegacyDNA, Display, TEXT("[DryRun] Would convert legacy DNA on %s -> %s"), *MeshPath, *TargetDNAPackagePath);
				++NumConverted;
				continue;
			}

			TStrongObjectPtr<UDNAImporter> Importer(NewObject<UDNAImporter>());
			const bool bConvertOK = Importer->ConvertFromLegacyAssetUserData(SkeletalMesh);
			if (!bConvertOK)
			{
				UE_LOG(LogConvertLegacyDNA, Error, TEXT("ConvertFromLegacyAssetUserData failed for %s"), *MeshPath);
				++NumFailed;
				continue;
			}

			if (UDNAAssetUserData* NewUserData = SkeletalMesh->GetAssetUserData<UDNAAssetUserData>())
			{
				if (UDNA* NewDNA = NewUserData->DNAAsset)
				{
					NewDNAPackage.Reset(NewDNA->GetOutermost());
					bConvertedThisMesh = true;
				}
			}

			if (!bConvertedThisMesh)
			{
				UE_LOG(LogConvertLegacyDNA, Error, TEXT("ConvertFromLegacyAssetUserData succeeded but no UDNAAssetUserData found on %s"), *MeshPath);
				++NumFailed;
				continue;
			}

			++NumConverted;
		}

		if (bVerbose)
		{
			UE_LOG(LogConvertLegacyDNA, Verbose, TEXT("Converted %s"), *MeshPath);
		}

		// Save inline so we never hold UPackage* across a CollectGarbage() call.
		if (NewDNAPackage.IsValid())
		{
			if (TrySavePackage(NewDNAPackage.Get(), bUseSCC))
			{
				++NumSaved;
				SavedPackageNames.Add(NewDNAPackage->GetFName());
			}
			else
			{
				++NumSaveFailed;
			}
		}
		if (MeshPackage.IsValid())
		{
			if (TrySavePackage(MeshPackage.Get(), bUseSCC))
			{
				++NumSaved;
				SavedPackageNames.Add(MeshPackage->GetFName());
			}
			else
			{
				++NumSaveFailed;
			}
		}

		// Drop our strong refs before GC so converted packages can be flushed.
		MeshPackage.Reset();
		NewDNAPackage.Reset();

		++SinceLastGC;
		if (SinceLastGC >= GarbageCollectInterval)
		{
			CollectGarbage(RF_NoFlags);
			SinceLastGC = 0;
		}
	}

	if (!bDryRun)
	{
		// Final GC pass to release any remaining loaded assets.
		CollectGarbage(RF_NoFlags);
	}

	// =================================================================================
	// Phase 2 (optional): resave every existing UDNA / orphan UDNAAsset package under
	// the path filter so the on-disk FDNAAssetCustomVersion is bumped to LatestVersion.
	//
	// We don't peek at the linker's custom version: loading a DNA asset runs
	// UDNA::Serialize which migrates between versions automatically, and saving writes
	// at LatestVersion. Already-current assets just round-trip unchanged.
	// =================================================================================
	int32 NumResaveScanned         = 0;
	int32 NumResaveSaved           = 0;
	int32 NumResaveDryRun          = 0;
	int32 NumResaveSaveFailed      = 0;
	int32 NumResaveFailed          = 0;
	int32 NumResaveAlreadySaved    = 0;
	int32 NumResaveSkippedNoLegacy = 0;

	if (bResaveDNA)
	{
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("Phase 2: resaving DNA-bearing packages under '%s'"), *PathFilter);

		// UDNA assets are top-level so the registry lists them directly. Legacy
		// UDNAAsset is a UAssetUserData subobject of a USkeletalMesh package, so it
		// never appears in AssetRegistry results - we have to look at meshes again
		// and check for the legacy block after load.
		FARFilter DNAFilter;
		DNAFilter.bRecursivePaths = true;
		DNAFilter.bRecursiveClasses = true;
		DNAFilter.PackagePaths.Add(FName(*PathFilter));
		DNAFilter.ClassPaths.Add(UDNA::StaticClass()->GetClassPathName());
		DNAFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

		TArray<FAssetData> DNAAssets;
		AssetRegistry.GetAssets(DNAFilter, DNAAssets);

		UE_LOG(LogConvertLegacyDNA, Display, TEXT("Discovered %d UDNA / SkeletalMesh asset(s) to inspect"), DNAAssets.Num());

		int32 ResaveSinceGC = 0;
		for (int32 Index = 0; Index < DNAAssets.Num(); ++Index)
		{
			const FAssetData& AssetData = DNAAssets[Index];
			++NumResaveScanned;

			if (bVerbose)
			{
				UE_LOG(LogConvertLegacyDNA, Verbose, TEXT("[Resave %d/%d] %s"), Index + 1, DNAAssets.Num(), *AssetData.GetObjectPathString());
			}

			UObject* Asset = AssetData.GetAsset();
			if (!Asset)
			{
				UE_LOG(LogConvertLegacyDNA, Warning, TEXT("Failed to load %s; skipping"), *AssetData.GetObjectPathString());
				++NumResaveFailed;
				continue;
			}

			// SkeletalMeshes only need a Phase 2 resave when they still carry a legacy
			// UDNAAsset block (post-Phase-1 conversion will already have rewritten them).
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
			{
				if (SkeletalMesh->GetAssetUserData<UDNAAsset>() == nullptr)
				{
					++NumResaveSkippedNoLegacy;
					continue;
				}
			}

			TStrongObjectPtr<UPackage> Package(Asset->GetOutermost());
			if (!Package.IsValid())
			{
				UE_LOG(LogConvertLegacyDNA, Warning, TEXT("Asset %s has no outer package; skipping"),
					*AssetData.GetObjectPathString());
				++NumResaveFailed;
				continue;
			}

			// Skip packages already saved by Phase 1 to avoid a redundant write.
			if (SavedPackageNames.Contains(Package->GetFName()))
			{
				UE_LOG(LogConvertLegacyDNA, Verbose,
					TEXT("Skipping %s: already saved by Phase 1"), *Package->GetName());
				++NumResaveAlreadySaved;
				continue;
			}

			// Force a save; UDNA::Serialize migrated the on-disk custom version when the
			// asset was loaded above and SavePackage will write at LatestVersion.
			Package->MarkPackageDirty();

			if (bDryRun)
			{
				UE_LOG(LogConvertLegacyDNA, Display, TEXT("[DryRun] Would resave %s"), *Package->GetName());
				++NumResaveDryRun;
			}
			else if (TrySavePackage(Package.Get(), bUseSCC))
			{
				++NumResaveSaved;
				SavedPackageNames.Add(Package->GetFName());
			}
			else
			{
				++NumResaveSaveFailed;
			}

			Package.Reset();

			++ResaveSinceGC;
			if (!bDryRun && ResaveSinceGC >= GarbageCollectInterval)
			{
				CollectGarbage(RF_NoFlags);
				ResaveSinceGC = 0;
			}
		}

		if (!bDryRun)
		{
			CollectGarbage(RF_NoFlags);
		}
	}

	UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("ConvertLegacyDNAAssets summary"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("Mode: %s"), bDryRun ? TEXT("DRY RUN") : TEXT("APPLIED"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("Phase 1 (Convert)"));
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Scanned                : %d"), NumScanned);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Converted in place     : %d"), NumConverted);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Reimported from source : %d"), NumReimported);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Already migrated       : %d"), NumAlreadyMigrated);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Skipped (no legacy)    : %d"), NumSkipped);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Failed                 : %d"), NumFailed);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Packages saved         : %d"), NumSaved);
	UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Save failures          : %d"), NumSaveFailed);
	if (bResaveDNA)
	{
		UE_LOG(LogConvertLegacyDNA, Display, TEXT(""));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("Phase 2 (Resave)"));
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Scanned                  : %d"), NumResaveScanned);
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Packages saved           : %d"), NumResaveSaved);
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Would-save (DryRun)      : %d"), NumResaveDryRun);
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Skipped (saved by Ph.1)  : %d"), NumResaveAlreadySaved);
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Skipped (no legacy DNA)  : %d"), NumResaveSkippedNoLegacy);
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Save failures            : %d"), NumResaveSaveFailed);
		UE_LOG(LogConvertLegacyDNA, Display, TEXT("  Load failures            : %d"), NumResaveFailed);
	}

	const bool bAnyFailure = (NumFailed > 0) || (NumSaveFailed > 0) || (NumResaveFailed > 0) || (NumResaveSaveFailed > 0);
	return bAnyFailure ? 1 : 0;
}
