// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureVersePathMapperCommandlet.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "Async/ParallelFor.h"
#include "CoreGlobals.h"
#include "Engine/AssetManager.h"
#include "GameFeatureData.h"
#include "GameFeaturesProjectPolicies.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeaturesSubsystemSettings.h"
#include "IO/IoDispatcherInternal.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/TVariant.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "String/Split.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IPluginManager.h"
#include "Logging/StructuredLog.h"
#include "InstallBundleUtils.h"
#include "JsonObjectConverter.h"
#include "Algo/Transform.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureVersePathMapperCommandlet)

DEFINE_LOG_CATEGORY_STATIC(LogGameFeatureVersePathMapper, Log, All);

namespace GameFeatureVersePathMapper
{
	struct FArgs
	{
		FString DevARPath;

		/**
		 * From -ReferencedSet=path. Non-empty enables DevAR row filtering against the cook's ReferencedSet.txt
		 * (written by JoinCookOutputs). Legacy callers that don't pass the flag run unfiltered.
		 */
		FString ReferencedSetPath;

		FString OutputPath;

		const ITargetPlatform* TargetPlatform = nullptr;

		bool bWithDynamicCookPlugins = true;

		bool bSerializeVerseLookupBinary = false;

		static TOptional<FArgs> Parse(const TCHAR* CmdLineParams)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Parsing command line");

			FArgs Args;

			// Optional path to dev asset registry
			FString DevARFilename;
			if (FParse::Value(CmdLineParams, TEXT("-DevAR="), DevARFilename))
			{
				if (IFileManager::Get().FileExists(*DevARFilename) && FPathViews::GetExtension(DevARFilename) == TEXTVIEW("bin"))
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Using dev asset registry path '{Path}'", DevARFilename);
					Args.DevARPath = DevARFilename;
				}
				else
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-DevAR did not specify a valid path.");
					return {};
				}
			}

			// Required output path
			if (!FParse::Value(CmdLineParams, TEXT("-Output="), Args.OutputPath))
			{
				UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-Output is required.");
				return {};
			}

			// Required target platform
			FString TargetPlatformName;
			if (FParse::Value(CmdLineParams, TEXT("-Platform="), TargetPlatformName))
			{
				if (const ITargetPlatform* TargetPlatform = GetTargetPlatformManagerRef().FindTargetPlatform(TargetPlatformName))
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Using target platform '{Platform}'", TargetPlatformName);
					Args.TargetPlatform = TargetPlatform;
				}
				else
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find target platfom '{Platform}'.", TargetPlatformName);
					return {};
				}
			}
			else
			{
				UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-Platform is required.");
				return {};
			}

			bool bSkipDynamicCooked = false;
			if (FParse::Bool(CmdLineParams, TEXT("-SkipDynamicCookPlugins="), bSkipDynamicCooked))
			{
				Args.bWithDynamicCookPlugins = !bSkipDynamicCooked;
			}

			FString ReferencedSetFilename;
			if (FParse::Value(CmdLineParams, TEXT("-ReferencedSet="), ReferencedSetFilename))
			{
				if (IFileManager::Get().FileExists(*ReferencedSetFilename))
				{
					Args.ReferencedSetPath = MoveTemp(ReferencedSetFilename);
				}
				else
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-ReferencedSet did not specify a valid path.");
					return {};
				}
			}

			Args.bSerializeVerseLookupBinary = FParse::Param(CmdLineParams, TEXT("WriteLookupBinary"));

			return Args;
		}
	};

	static const TMap<FString, TArray<FString>> GetImplicitDependenciesConfig()
	{
		TMap<FString, TArray<FString>> Result;

		TArray<FString> ConfigLines;
		GConfig->GetArray(
			TEXT("/Script/DependentPluginsEditor.DependentPluginsSettings"),
			TEXT("DependentPlugins"),
			ConfigLines,
			TEXT("DefaultDependentPlugins"));

		for (const FString& ConfigLine : ConfigLines)
		{
			FString PluginName;
			if (FParse::Value(*ConfigLine, TEXT("Plugin="), PluginName))
			{
				PluginName = PluginName.Replace(TEXT("\""), TEXT(""));

				FString DependenciesString;
				if (FParse::Value(*ConfigLine, TEXT("DependentPlugins="), DependenciesString))
				{
					DependenciesString.RemoveFromStart(TEXT("("));
					DependenciesString.RemoveFromEnd(TEXT(")"));

					TArray<FString> Dependencies;
					DependenciesString.ParseIntoArray(Dependencies, TEXT(","), true);

					Result.FindOrAdd(PluginName).Append(Dependencies);
				}
			}
		}

		return Result;
	}

	FString GetVerseAppDomain()
	{
		FString AppDomain;
		if (!GConfig->GetString(TEXT("Verse"), TEXT("AppDomain"), AppDomain, GGameIni))
		{
			AppDomain = FPaths::Combine(TEXTVIEW("/"), FString(FApp::GetProjectName()) + TEXTVIEW(".com"));
		}
		AppDomain.RemoveFromEnd(TEXTVIEW("/"));
		return AppDomain;
	}

	FString GetAltVerseAppDomain()
	{
		FString AppDomain;
		if (!GConfig->GetString(TEXT("Verse"), TEXT("AltAppDomain"), AppDomain, GGameIni))
		{
			AppDomain = {};
		}
		AppDomain.RemoveFromEnd(TEXTVIEW("/"));
		return AppDomain;
	}

	class FInstallBundleResolver
	{
		TArray<TPair<FString, TArray<FRegexPattern>>> BundleRegexList;
		TMap<FString, FString> RegexMatchCache;
		FTransactionallySafeRWLock CachedRegexMatchLock;

	public:
		FInstallBundleResolver(const TCHAR* IniPlatformName = nullptr)
		{
			FConfigFile MaybeLoadedConfig;
			const FConfigFile* InstallBundleConfig = IniPlatformName ?
				GConfig->FindOrLoadPlatformConfig(MaybeLoadedConfig, *GInstallBundleIni, IniPlatformName) :
				GConfig->FindConfigFile(GInstallBundleIni);

			// We want to load regex even if PlatformChunkID=-1 to make sure we map GFPs that are not packaged
			BundleRegexList = InstallBundleUtil::LoadBundleRegexFromConfig(*InstallBundleConfig);
		}

		FString Resolve(const FStringView& PluginName, const FString& ChunkPattern)
		{
			FString InstallBundleName = UGameFeaturesSubsystem::Get().GetInstallBundleName(PluginName);
			if (InstallBundleName.IsEmpty() && !ChunkPattern.IsEmpty())
			{
				{
					UE::TReadScopeLock ReadLock(CachedRegexMatchLock);
					if (FString* CachedInstallBundleName = RegexMatchCache.Find(ChunkPattern))
					{
						InstallBundleName = *CachedInstallBundleName;
						return InstallBundleName;
					}
				}
				if (InstallBundleUtil::MatchBundleRegex(BundleRegexList, ChunkPattern, InstallBundleName))
				{
					UE::TWriteScopeLock WriteLock(CachedRegexMatchLock);
					RegexMatchCache.Add(ChunkPattern, InstallBundleName);
				}
			}

			return InstallBundleName;
		}
	};

	FConfigCacheIni* GetPlatformConfigCacheIni(const FString& IniPlatformName)
	{
#if WITH_EDITOR
		FConfigCacheIni* ConfigCache = FConfigCacheIni::ForPlatform(FName(IniPlatformName));
		if (!ConfigCache)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Warning, "Failed to find config for {PlatformName}", *IniPlatformName);
			ConfigCache = GConfig;
		}
#else
		FConfigCacheIni* ConfigCache = GConfig;
#endif
		return ConfigCache;
	}

	static bool PlatformChunksAreAlwaysResident(const ITargetPlatform* TargetPlatform /*= nullptr*/)
	{
		const FString& IniPlatformName = TargetPlatform ? TargetPlatform->IniPlatformName() : FPlatformProperties::IniPlatformName();
		FConfigCacheIni* ConfigCache = GetPlatformConfigCacheIni(IniPlatformName);

		bool bPlatformAlwaysResident = false;
		if (!ConfigCache->GetBool(TEXT("GameFeaturePlugins"), TEXT("bGFPAreAlwaysResident"), bPlatformAlwaysResident, GInstallBundleIni))
		{
			if (TargetPlatform)
			{
				bPlatformAlwaysResident = TargetPlatform->IsServerOnly() || TargetPlatform->HasEditorOnlyData();
			}
			else
			{
				// DS and cooked editor should always resolve to file protocol for now.
				bPlatformAlwaysResident = IsRunningDedicatedServer() || GIsEditor;
			}
		}

		return bPlatformAlwaysResident;
	}

	static FString GetChunkPatternFormat(const FString& IniPlatformName)
	{
		FConfigCacheIni* ConfigCache = GetPlatformConfigCacheIni(IniPlatformName);

		FString ChunkPatternFormat;
		if (!ConfigCache->GetString(TEXT("GameFeaturePlugins"), TEXT("GFPBundleRegexMatchPatternFormat"), ChunkPatternFormat, GInstallBundleIni))
		{
			ChunkPatternFormat = TEXTVIEW("chunk{Chunk}.pak");
		}

		return ChunkPatternFormat;
	}

	static FString GetChunkPattern(const FString& ChunkPatternFormat, const FString& ChunkName)
	{
		return FString::Format(*ChunkPatternFormat, FStringFormatNamedArguments{ {TEXT("Chunk"), ChunkName} });
	}

	static FString GetChunkPattern(const FString& ChunkPatternFormat, int32 Chunk)
	{
		return FString::Format(*ChunkPatternFormat, FStringFormatNamedArguments{ {TEXT("Chunk"), Chunk} });
	}

	static FString GetDevARPathForPlatform(FStringView PlatformName)
	{
		return FPaths::Combine(
			FPaths::ProjectSavedDir(), 
			TEXTVIEW("Cooked"), 
			PlatformName, 
			FApp::GetProjectName(), 
			TEXTVIEW("Metadata"), 
			TEXTVIEW("DevelopmentAssetRegistry.bin"));
	}

	static FString GetDevARPath(const FArgs& Args)
	{
		if (!Args.DevARPath.IsEmpty())
		{
			return Args.DevARPath;
		}

		if (Args.TargetPlatform)
		{
			return GetDevARPathForPlatform(Args.TargetPlatform->PlatformName());
		}

		return {};
	}

	/**
	 * ReferencedSet.txt v1 contract (written by UCookOnTheFlyServer::WriteReferencedSet / JoinCookOutputs):
	 *   - Line 1: # Version 1 header.
	 *   - Subsequent lines: one package path per line; comments may begin with `#`; blank lines are ignored.
	 * FName comparison is case-insensitive, so package names stored in the set match AssetData.PackageName
	 * regardless of the cooker's case normalization.
	 */
	static bool TryLoadReferencedSetFromFile(const FString& FilePath, TSet<FName>& OutReferencedPackages)
	{
		TArray<FString> LoadedLines;
		if (!FFileHelper::LoadFileToStringArray(LoadedLines, *FilePath))
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to read ReferencedSet file '{Path}'", FilePath);
			return false;
		}
		if (LoadedLines.IsEmpty())
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "ReferencedSet file '{Path}' is empty.", FilePath);
			return false;
		}

		// First non-empty line must be the recognized version header; anything else indicates a format
		// we don't understand yet and we refuse to silently misinterpret the body.
		bool bHeaderValidated = false;
		for (const FString& Line : LoadedLines)
		{
			const FStringView Trimmed = FStringView(Line).TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				continue;
			}
			if (!Trimmed.StartsWith(TEXTVIEW("# Version 1")))
			{
				UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "ReferencedSet file '{Path}' has unexpected header '{Line}'; expected '# Version 1'.", ("Path", FilePath), ("Line", FString(Trimmed)));
				return false;
			}
			bHeaderValidated = true;
			break;
		}
		if (!bHeaderValidated)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "ReferencedSet file '{Path}' contained no header line.", FilePath);
			return false;
		}

		for (const FString& Line : LoadedLines)
		{
			const FStringView Trimmed = FStringView(Line).TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT('#')))
			{
				continue;
			}
			OutReferencedPackages.Add(FName(Trimmed));
		}

		if (OutReferencedPackages.IsEmpty())
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "ReferencedSet file '{Path}' contained no package lines.", FilePath);
			return false;
		}

		return true;
	}

	template<class EnumeratorFunc>
	static TMap<FString, int32> FindGFPChunksImpl(const EnumeratorFunc& Enumerator, const TSet<FName>* ReferencedPackageNames)
	{
		const IAssetRegistry& AR = IAssetRegistry::GetChecked();

		FARFilter RawFilter;
#if !WITH_EDITORONLY_DATA
		// work-around for in-memory FAssetData not having chunks set
		RawFilter.bIncludeOnlyOnDiskAssets = true;
#endif
		RawFilter.bRecursiveClasses = true;
		RawFilter.ClassPaths.Add(UGameFeatureData::StaticClass()->GetClassPathName());

		FARCompiledFilter Filter;
		AR.CompileFilter(RawFilter, Filter);

		TMap<FString, int32> GFPChunks;

		FNameBuilder PackagePathBuilder;
		auto FindGFDChunks = [&PackagePathBuilder, &GFPChunks, ReferencedPackageNames](const FAssetData& AssetData) -> bool
		{
			int32 ChunkId = -1;
			if (AssetData.GetChunkIDs().Num() > 0)
			{
				ChunkId = AssetData.GetChunkIDs()[0];
				if (AssetData.GetChunkIDs().Num() > 1)
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Warning, "Multiple Chunks found for {Package}, using chunk {Chunk}", AssetData.PackageName, ChunkId);
				}
			}
			AssetData.PackageName.ToString(PackagePathBuilder);
			if (FStringView(PackagePathBuilder.GetData(), PackagePathBuilder.Len()).StartsWith(TEXT("/Game/Developers")))
			{
				// Ignore "Developers" data
				return true;
			}
			// DevAR produced by a prior incremental cook may still contain UGameFeatureData rows for
			// plugins that have since been disabled or removed. BuildLookup resolves each row to a
			// plugin via IPluginManager::FindPlugin and emits errors for rows whose plugin no longer
			// exists. ReferencedSet (written by this cook's JoinCookOutputs) is the authoritative list
			// of packages actually referenced by the cook session, so we intersect the DevAR
			// enumeration against it to drop stale rows.
			if (ReferencedPackageNames)
			{
				if (!ReferencedPackageNames->Contains(AssetData.PackageName))
				{
					return true;
				}
			}
			FStringView PackageRoot = FPathViews::GetMountPointNameFromPath(PackagePathBuilder);
			GFPChunks.Emplace(PackageRoot, ChunkId);

			return true;
		};

		Enumerator(Filter, FindGFDChunks);

		// Find any GFPs that don't have content and assign them to chunk0
		const UGameFeaturesSubsystemSettings* GameFeaturesSettings = GetDefault<UGameFeaturesSubsystemSettings>();
		IPluginManager& PluginMan = IPluginManager::Get();
		TArray<TSharedRef<IPlugin>> AllPlugins = PluginMan.GetDiscoveredPlugins();
		for (const TSharedRef<IPlugin>& Plugin : AllPlugins)
		{
			if (Plugin->CanContainContent())
			{
				continue;
			}

			if (GFPChunks.Contains(Plugin->GetName()))
			{
				continue;
			}

			if (!GameFeaturesSettings->IsValidGameFeaturePlugin(Plugin->GetDescriptorFileName()))
			{
				continue;
			}

			GFPChunks.Emplace(Plugin->GetName(), 0);
		}

		return GFPChunks;
	}

	static TMap<FString, int32> FindGFPChunks(const FAssetRegistryState& DevAR, const TSet<FName>* ReferencedPackageNames)
	{
		return FindGFPChunksImpl([&DevAR](const FARCompiledFilter& Filter, TFunctionRef<bool(const FAssetData&)> Callback)
		{
			DevAR.EnumerateAssets(Filter, {}, Callback, 
				UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnmountedPaths | 
				UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnfilteredArAssets);
		}, ReferencedPackageNames);
	}

	static TMap<FString, int32> FindGFPChunks()
	{
		const IAssetRegistry& AR = IAssetRegistry::GetChecked();
		return FindGFPChunksImpl([&AR](const FARCompiledFilter& Filter, TFunctionRef<bool(const FAssetData&)> Callback)
		{
			AR.EnumerateAssets(Filter, Callback, UE::AssetRegistry::EEnumerateAssetsFlags::AllowUnmountedPaths);
		}, nullptr);
	}

	TArray<FDLCInfo> FindGFPToDLC(const ITargetPlatform* TargetPlatform)
	{
		FConfigFile* InstallBundleConfig = nullptr;
		if (TargetPlatform)
		{
			FConfigFile MaybeLoadedConfig;
			const FString IniPlatformName = TargetPlatform->IniPlatformName();
			InstallBundleConfig = GConfig->FindOrLoadPlatformConfig(MaybeLoadedConfig, *GInstallBundleIni, *IniPlatformName);
		}
		else
		{
			InstallBundleConfig = GConfig->FindConfigFile(GInstallBundleIni);
		}

		TArray<FDLCInfo> FoundDLCInfo;
		const FString DLCInfoSectionPrefix(TEXT("DLCInfo "));
		for (const TPair<FString, FConfigSection>& Pair : *InstallBundleConfig)
		{
			const FString& Section = Pair.Key;
			if (!Section.StartsWith(DLCInfoSectionPrefix))
				continue;

			FString InstallBundleName;
			if (!InstallBundleConfig->GetString(*Section, TEXT("InstallBundleName"), InstallBundleName))
				continue;

			TArray<FString> Plugins;
			if (!InstallBundleConfig->GetArray(*Section, TEXT("Plugins"), Plugins))
				continue;

			FString DLCAndBundle = Section.RightChop(DLCInfoSectionPrefix.Len());
			FStringView DLCAndBundleView = FStringView(DLCAndBundle);
			FStringView DLCName;
			FStringView DLCBundleName;
			if (!UE::String::SplitFirstChar(DLCAndBundleView, TEXT('#'), DLCName, DLCBundleName))
			{
				DLCName = DLCAndBundleView;
			}
			FDLCInfo& NewDLCInfo = FoundDLCInfo.Emplace_GetRef();
			NewDLCInfo.DLCName = DLCName;
			NewDLCInfo.InstallBundleName = MoveTemp(InstallBundleName);
			NewDLCInfo.Plugins = MoveTemp(Plugins);
		}

		return FoundDLCInfo;
	}

	static bool IsChunkAlwaysResident(TConstArrayView<int32> AlwaysResidentChunks, int32 Chunk)
	{
		return Chunk < 0 || AlwaysResidentChunks.Contains(Chunk);
	}

	// Filter GFPs cooked of out of band
	static bool IsGFPUpluginInBaseBuild(FStringView GFPName)
	{
		// Consider a GFP part of the base build if its plugin was added outside of the
		// GFP statemachine. If there are cases where this doesn't hold, then its probably
		// better to generate an explicit manifest.

		UGameFeaturesSubsystem& GFPSys = UGameFeaturesSubsystem::Get();

		bool bGFPAddedUplugin = false;

		FString GFPURL;
		if (GFPSys.GetPluginURLByName(GFPName, GFPURL))
		{
			GFPSys.GetGameFeatureControlsUPlugin(GFPURL, bGFPAddedUplugin);
		}

		return !bGFPAddedUplugin;
	}

	bool FDepthFirstGameFeatureSorter::Visit(const FName Plugin, TFunctionRef<void(FName, const FString&)> AddOutput)
	{
		const FGameFeaturePluginInfo* MaybePluginInfo = GfpInfoMap.Find(Plugin);
		if (!MaybePluginInfo)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "DepthFirstGameFeatureSorter: could not find {PluginName}", Plugin);
			return false;
		}
		const FGameFeaturePluginInfo& PluginInfo = *MaybePluginInfo;

		// Add a scope here to make sure VisitState isn't used later. It can become invalid if VisitedPlugins is resized
		{
			EVisitState& VisitState = VisitedPlugins.FindOrAdd(Plugin, EVisitState::None);
			if (VisitState == EVisitState::Visiting)
			{
				UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "DepthFirstGameFeatureSorter: Cycle detected in plugin dependencies with {PluginName}", Plugin);
				return false;
			}

			if (VisitState == EVisitState::Visited)
			{
				return true;
			}

			VisitState = EVisitState::Visiting;
		}

		for (const FName DepPlugin : PluginInfo.Dependencies)
		{
			if (!Visit(DepPlugin, AddOutput))
			{
				return false;
			}
		}

		VisitedPlugins.FindChecked(Plugin) = EVisitState::Visited;
		if (bIncludeVirtualNodes || !PluginInfo.GfpUri.IsEmpty()) // An empty URI means this is virtual node that only exists for Verse path resolution
		{
			AddOutput(Plugin, PluginInfo.GfpUri);
		}
		return true;
	}

	bool FDepthFirstGameFeatureSorter::Sort(TFunctionRef<FName()> GetNextRootPlugin, TFunctionRef<void(FName, const FString&)> AddOutput)
	{
		for (FName RootPlugin = GetNextRootPlugin(); !RootPlugin.IsNone(); RootPlugin = GetNextRootPlugin())
		{
			if (!Visit(RootPlugin, AddOutput))
			{
				return false;
			}
		}
		return true;
	}

	bool FDepthFirstGameFeatureSorter::Sort(TConstArrayView<FName> RootPlugins, TFunctionRef<void(FName, const FString&)> AddOutput)
	{
		return Sort(
			[RootPlugins, i = int32(0)]() mutable -> FName
			{
				if (!RootPlugins.IsValidIndex(i))
				{
					return {};
				}
				return RootPlugins[i++];
			},
			AddOutput);
	}

	bool FDepthFirstGameFeatureSorter::Sort(TConstArrayView<FName> RootPlugins, TArray<FName>& OutPlugins)
	{
		return Sort(
			[RootPlugins, i = int32(0)]() mutable -> FName
			{
				if (!RootPlugins.IsValidIndex(i))
				{
					return {};
				}
				return RootPlugins[i++];
			}, 
			[&OutPlugins](FName OutPlugin, const FString& URI)
			{
				OutPlugins.Add(OutPlugin);
			});
	}

	static constexpr uint32 VerseLookupBinaryMagic   = 0x50564647; // 'G','F','V','P'
	static constexpr uint32 VerseLookupBinaryVersion = 1;

	FString GetVerseLookupBinaryPath(FStringView PlatformName)
	{
		if (PlatformName.IsEmpty())
		{
			return FPaths::Combine(FPaths::ProjectDir(), TEXT("GameFeatureVersePaths.bin"));
		}
		return FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Cooked"), PlatformName,
			FApp::GetProjectName(), TEXT("GameFeatureVersePaths.bin"));
	}

	bool SerializeLookupBinary(FArchive& Ar, FGameFeatureVersePathLookup& Lookup)
	{
		uint32 Magic = VerseLookupBinaryMagic;
		Ar << Magic;
		if (Ar.IsLoading() && Magic != VerseLookupBinaryMagic)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Binary lookup: unexpected magic {Magic}", Magic);
			return false;
		}

		uint32 Version = VerseLookupBinaryVersion;
		Ar << Version;
		if (Ar.IsLoading() && Version != VerseLookupBinaryVersion)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Binary lookup: unsupported version {Version}", Version);
			return false;
		}

		Ar << Lookup.VersePathToGfpMap;
		Ar << Lookup.GfpInfoMap;
		return !Ar.IsError();
	}

	TOptional<FGameFeatureVersePathLookup> BuildLookup(
		const ITargetPlatform* TargetPlatform /*= nullptr*/,
		const FAssetRegistryState* DevAR /*= nullptr*/,
		EBuildLookupOptions Options /*= EBuildLookupOptions::None*/,
		const TSet<FName>* ReferencedPackageNames /*= nullptr*/)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("GameFeatureVersePathMapper::BuildLookup");

		// At runtime in a cooked build (no cook target platform), prefer the pre-built binary produced by the commandlet.
		// The binary provides a stable mapping that does not depend on the AR being loaded or mounted,
		// which is not guaranteed in cooked builds after map load and AR unmounting.
		
		const bool bHasCookedData = FIoDispatcherInternal::HasPackageData();
#if !WITH_EDITOR
		// cooked editor assumed all plugins will be file type.
		if (TargetPlatform == nullptr && bHasCookedData)
		{
			static FGameFeatureVersePathLookup BinaryLookup = []()
				{
					FGameFeatureVersePathLookup ReturnBinaryLookup;
					const FString BinaryPath = GetVerseLookupBinaryPath();
					TArray<uint8> FileData;
					if (FFileHelper::LoadFileToArray(FileData, *BinaryPath, FILEREAD_Silent))
					{
						FMemoryReader MemoryReader(FileData, true);
						if (SerializeLookupBinary(MemoryReader, ReturnBinaryLookup))
						{
							UE_LOGFMT(LogGameFeatureVersePathMapper, Log, "BuildLookup: loaded from binary '{Path}'", BinaryPath);
						}
						UE_LOGFMT(LogGameFeatureVersePathMapper, Log, "BuildLookup: failed to deserialize binary '{Path}', falling back to AR scan", BinaryPath);
					}
					return ReturnBinaryLookup;
				}();
			if (BinaryLookup.GfpInfoMap.Num() > 0)
			{
				return BinaryLookup;
			}
		}
#endif // !WITH_EDITOR

		const TMap<FString, int32> GFPChunks = DevAR ? FindGFPChunks(*DevAR, ReferencedPackageNames) : FindGFPChunks();
		const TArray<FDLCInfo> DLCInfos = FindGFPToDLC(TargetPlatform);
		IPluginManager& PluginMan = IPluginManager::Get();

		FInstallBundleResolver InstallBundleResolver(TargetPlatform ? *TargetPlatform->IniPlatformName() : nullptr);

		const FString AppDomain = GameFeatureVersePathMapper::GetVerseAppDomain();
		const FString GameFeatureRootVersePath = UGameFeatureVersePathMapperCommandlet::GetGameFeatureRootVersePath();
		
		const FString& IniPlatformName = TargetPlatform ? TargetPlatform->IniPlatformName() : FPlatformProperties::IniPlatformName();
		const FString ChunkPatternFormat = GetChunkPatternFormat(IniPlatformName);
		const bool bPlatformChunksAreAlwaysResident = PlatformChunksAreAlwaysResident(TargetPlatform);

		TMap<int32, FString> ChunkIdStringOverride;
		UAssetManager::Get().GetPakChunkIdToStringMapping(IniPlatformName, ChunkIdStringOverride);
		const FString NamedChunkPatternFormat = FString::Printf(TEXT("chunk%c{Chunk}%c.pak"), NAMED_PAK_CHUNK_DELIMITER_CHAR, NAMED_PAK_CHUNK_DELIMITER_CHAR);

		FString TargetPlatformName = TargetPlatform ? TargetPlatform->IniPlatformName() : FPlatformMisc::GetUBTPlatform();
		if (TargetPlatformName.Equals(TEXT("Windows"), ESearchCase::IgnoreCase))
		{
			// legacy change of windows -> win64 as that's how SupportedTargetPlatforms expects windows.
			TargetPlatformName = TEXT("Win64");
		}
		struct FGFPBundleInfo
		{
			TVariant<int32, FString> ChunkOrBundle;
			FGameFeaturePluginInfo GfpInfo;
			bool bSkipped = false;
		};
		TMap<FString, FGFPBundleInfo> GFPToChunkOrBundle;
		GFPToChunkOrBundle.Reserve(GFPChunks.Num());
		for (const TPair<FString, int32>& Pair : GFPChunks)
		{
			FGFPBundleInfo& NewChunkOrBundle = GFPToChunkOrBundle.Add(Pair.Key);
			NewChunkOrBundle.ChunkOrBundle.Set<int32>(Pair.Value);
		}
		
		for (const FDLCInfo& DLC : DLCInfos)
		{
			for (const FString& Plugin : DLC.Plugins)
			{
				FGFPBundleInfo& NewChunkOrBundle = GFPToChunkOrBundle.FindOrAdd(Plugin);
				if (NewChunkOrBundle.ChunkOrBundle.IsType<int32>())
				{
					if (NewChunkOrBundle.ChunkOrBundle.Get<int32>() == 0)
					{
						NewChunkOrBundle.ChunkOrBundle.Set<FString>(DLC.InstallBundleName);
					}
				}
				else
				{
					// hasn't been set yet.
					NewChunkOrBundle.ChunkOrBundle.Set<FString>(DLC.InstallBundleName);
				}
			}
		}

		auto PluginIsDynamicCooked = [](const TSharedRef<IPlugin>& Plugin) -> bool
			{
				bool bDynamicModule = false;
				if (TSharedPtr<const FGameFeaturePluginDetails> PluginDetails = UGameFeaturesSubsystem::Get().GetBuiltInGameFeaturePluginDetails(Plugin))
				{
					const TSharedPtr<FJsonValue>& CookBehavior = PluginDetails->AdditionalMetadata.FindRef(PLUGIN_TEXT("CookBehavior"));
					if (CookBehavior.IsValid() && CookBehavior->Type == EJson::Object)
					{
						FString CookType;
						CookBehavior->AsObject()->TryGetStringField(TEXT("Type"), CookType);
						bDynamicModule = CookType.Compare(TEXT("ContentWorker"), ESearchCase::IgnoreCase) == 0;
					}
				}
				return bDynamicModule;
			};

		UGameFeaturesSubsystem& GameFeaturesSubsystem = UGameFeaturesSubsystem::Get();
		const UGameFeaturesProjectPolicies& Policy = GameFeaturesSubsystem.GetPolicy();
		ParallelFor(GFPToChunkOrBundle.Num(), [&GFPToChunkOrBundle, &PluginIsDynamicCooked, &PluginMan, &Policy, &ChunkIdStringOverride, &InstallBundleResolver, Options, TargetPlatformName, NamedChunkPatternFormat, ChunkPatternFormat, bPlatformChunksAreAlwaysResident, bHasCookedData](int32 Index)
			{
				TPair<FString, FGFPBundleInfo>& PluginGFPInfoPair = GFPToChunkOrBundle.Get(FSetElementId::FromInteger(Index));
				const FString& PluginName = PluginGFPInfoPair.Key;
				FGFPBundleInfo& ChunkOrBundle = PluginGFPInfoPair.Value;

				TSharedPtr<IPlugin> Plugin = PluginMan.FindPlugin(PluginName);
				if (!Plugin)
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find uplugin {PluginName}", PluginName);
					return;
				}

				FStringView PluginNameView(PluginName);
				if (EnumHasAnyFlags(Options, EBuildLookupOptions::OnlyBaseBuildPlugins))
				{
					if (!IsGFPUpluginInBaseBuild(PluginNameView))
					{
						ChunkOrBundle.bSkipped = true;
						return;
					}
				}

				// Skip plugins that won't be enabled on the platform.
				if (!Plugin->GetDescriptor().SupportsTargetPlatform(TargetPlatformName))
				{
					ChunkOrBundle.bSkipped = true;
					return;
				}
			
				if (!EnumHasAnyFlags(Options, EBuildLookupOptions::WithDynamicCookPlugins) && PluginIsDynamicCooked(Plugin.ToSharedRef()))
				{
					ChunkOrBundle.bSkipped = true;
					return;
				}
				
				FString InstallBundleName;
				if (ChunkOrBundle.ChunkOrBundle.IsType<FString>() && !ChunkOrBundle.ChunkOrBundle.Get<FString>().IsEmpty())
				{
					// Allow a specific bundle to be defined for a plugin.
					InstallBundleName = UGameFeaturesSubsystem::Get().GetInstallBundleName(PluginNameView);
					if (InstallBundleName.IsEmpty())
					{
						InstallBundleName = ChunkOrBundle.ChunkOrBundle.Get<FString>();
					}
				}
				else
				{
					const bool bIsChunkAlwaysResident = bPlatformChunksAreAlwaysResident;
					FString ChunkPattern;
					if (bIsChunkAlwaysResident)
					{
						// pass. ChunkPattern is empty.
					}
					else if (ChunkIdStringOverride.Contains(ChunkOrBundle.ChunkOrBundle.Get<int32>()))
					{
						FString NamedChunkStr = Plugin->GetName();
						
						TSharedPtr<FJsonObject> ObjectPtr;
						FString FileContents;
						const FString& PluginDescriptorFilename = Plugin->GetDescriptorFileName();
						if (FFileHelper::LoadFileToString(FileContents, *PluginDescriptorFilename))
						{
							TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
							if (FJsonSerializer::Deserialize(Reader, ObjectPtr) && ObjectPtr.IsValid())
							{
								FString CookToNamePakChunk;
								if (ObjectPtr->TryGetStringField(TEXT("CookToNamePakChunk"), CookToNamePakChunk))
								{
									NamedChunkStr = CookToNamePakChunk;
								}
							}
						}
						
						ChunkPattern = GetChunkPattern(NamedChunkPatternFormat, NamedChunkStr);
					}
					else
					{
						ChunkPattern = GetChunkPattern(ChunkPatternFormat, ChunkOrBundle.ChunkOrBundle.Get<int32>());
					}
					InstallBundleName = bIsChunkAlwaysResident ? FString() : InstallBundleResolver.Resolve(PluginNameView, ChunkPattern);
				}

				const FString DescriptorFileName = FPaths::CreateStandardFilename(Plugin->GetDescriptorFileName());
				ChunkOrBundle.GfpInfo.GfpUri = (InstallBundleName.IsEmpty()) ?
					UGameFeaturesSubsystem::GetPluginURL_FileProtocol(DescriptorFileName) :
					UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(DescriptorFileName, InstallBundleName);

				ChunkOrBundle.GfpInfo.Dependencies.Reserve(Plugin->GetDescriptor().Plugins.Num());
				const FString PluginURLFromFile = UGameFeaturesSubsystem::GetPluginURL_FileProtocol(DescriptorFileName);
				for (const FPluginReferenceDescriptor& Dependency : Plugin->GetDescriptor().Plugins)
				{
					// Currently GameFeatureSubsystem only checks bEnabled to determine if it should wait on a dependency, so match that logic here
					if (!Dependency.bEnabled)
					{
						continue;
					}

					if (!GFPToChunkOrBundle.Contains(Dependency.Name))
					{
						// Dependency is not a GFP
						continue;
					}
#if WITH_EDITOR
					if (!bHasCookedData)
					{
						// if we are in uncooked then resolve dependencies as they might be stripped as part of the cook and packaging
						TValueOrError<FString, FString> DependencyURLInfo = Policy.ResolvePluginDependency(PluginURLFromFile, Dependency.Name);
						if (DependencyURLInfo.HasError() || DependencyURLInfo.GetValue().IsEmpty())
						{
							continue;
						}
					}
#endif // WITH_EDITOR
					if (!Dependency.IsSupportedTargetPlatform(TargetPlatformName))
					{
						continue;
					}
					TSharedPtr<IPlugin> DepPlugin = PluginMan.FindPlugin(Dependency.Name);
					if (!DepPlugin)
					{
						UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find uplugin dependency {PluginName}", Dependency.Name);
						continue;
					}
					if (!DepPlugin->GetDescriptor().SupportsTargetPlatform(TargetPlatformName))
					{
						continue;
					}

					if (!EnumHasAnyFlags(Options, EBuildLookupOptions::WithDynamicCookPlugins) && PluginIsDynamicCooked(DepPlugin.ToSharedRef()))
					{
						if (!PluginIsDynamicCooked(Plugin.ToSharedRef()))
						{
							UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Static plugin '{PluginName}' depends on dynamic plugin '{DependentPluginName}'. Remove the dependency or mark {PluginName} as CookBehavior.ContentWorker",
								("PluginName", Plugin->GetName()), ("DependentPluginName", Dependency.Name));
						}
						else
						{
							continue;
						}
					}

					ChunkOrBundle.GfpInfo.Dependencies.Emplace(FStringView(Dependency.Name));
				}
			});

		FGameFeatureVersePathLookup Output;
		Output.VersePathToGfpMap.Reserve(GFPToChunkOrBundle.Num());
		Output.GfpInfoMap.Reserve(GFPToChunkOrBundle.Num());
		const TMap<FString, TArray<FString>> ImplicitDependenciesConfig = GetImplicitDependenciesConfig();
		for (TPair<FString, FGFPBundleInfo>& Pair : GFPToChunkOrBundle)
		{
			const FString& PluginName = Pair.Key;

			TSharedPtr<IPlugin> Plugin = PluginMan.FindPlugin(PluginName);
			if (!Plugin)
			{
				continue;
			}
			if (Pair.Value.bSkipped)
			{
				continue;
			}

			const FName PluginFName(PluginName);
			FStringView PluginNameView(PluginName);
			Output.VersePathToGfpMap.Add(FPaths::Combine(GameFeatureRootVersePath, PluginNameView), PluginFName);

			// Add a virtual GFP to support plugin specified Verse paths
			const FString& VersePath = Plugin->GetVersePath();
			if (!VersePath.IsEmpty() &&
				VersePath != AppDomain) // Filter out references to the root path, we don't wan't to allow resolving all content (and we don't register sub-paths)
			{
				// Add a virtual GFP with this Verse path that depends on this GFP
				FName& VirtualGFPName = Output.VersePathToGfpMap.FindOrAdd(VersePath);
				if (VirtualGFPName.IsNone())
				{
					VirtualGFPName = FName(FStringView(TEXTVIEW("V_") + VersePath));
				}

				FGameFeaturePluginInfo& GfpInfo = Output.GfpInfoMap.FindOrAdd(VirtualGFPName);

				const TArray<FString>* ImplicitDependencies = ImplicitDependenciesConfig.Find(PluginName);
				if (ImplicitDependencies && !ImplicitDependencies->IsEmpty())
				{
					GfpInfo.ImplicitDependencies.Append(*ImplicitDependencies);
				}

				GfpInfo.Dependencies.Add(PluginFName);
			}

			Output.GfpInfoMap.Add(PluginFName, MoveTemp(Pair.Value.GfpInfo));
		}

		check(Output.VersePathToGfpMap.Num() == Output.GfpInfoMap.Num());

		return Output;
	}
}

int32 UGameFeatureVersePathMapperCommandlet::Main(const FString& CmdLineParams)
{
	const TOptional<GameFeatureVersePathMapper::FArgs> MaybeArgs = GameFeatureVersePathMapper::FArgs::Parse(*CmdLineParams);
	if (!MaybeArgs)
	{
		// Parse function should print errors
		return 1;
	}
	const GameFeatureVersePathMapper::FArgs& Args = MaybeArgs.GetValue();

	FString DevArPath = GameFeatureVersePathMapper::GetDevARPath(Args);

	// If no DevAR path was provided, run against the global asset registry (BuildLookup supports DevAR=nullptr).
	// Filtering has nothing to filter in that mode, so we force it off regardless of the opt-in flag.
	FAssetRegistryState DevAR;
	const FAssetRegistryState* DevARPtr = nullptr;
	if (!DevArPath.IsEmpty())
	{
		if (!FPaths::FileExists(DevArPath))
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find development asset registry at '{Path}'", DevArPath);
			return 1;
		}
		if (!FAssetRegistryState::LoadFromDisk(*DevArPath, FAssetRegistryLoadOptions(), DevAR))
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to load development asset registry from {Path}", DevArPath);
			return 1;
		}
		DevARPtr = &DevAR;
	}
	else
	{
		UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "No -DevAR= provided; using the global asset registry.");
	}

	// Filtering is opt-in: callers enable it by passing -ReferencedSet=path.
	// Absent the flag, the commandlet runs unfiltered.
	TSet<FName> ReferencedPackages;
	const TSet<FName>* ReferencedPackageNamesPtr = nullptr;
	if (!Args.ReferencedSetPath.IsEmpty() && DevARPtr)
	{
		if (!GameFeatureVersePathMapper::TryLoadReferencedSetFromFile(Args.ReferencedSetPath, ReferencedPackages))
		{
			return 1;
		}
		ReferencedPackageNamesPtr = &ReferencedPackages;
		UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Filtering DevAR GFP enumeration using ReferencedSet '{Path}' ({Count} packages)", ("Path", Args.ReferencedSetPath), ("Count", ReferencedPackages.Num()));
	}

	GameFeatureVersePathMapper::EBuildLookupOptions BuildOptions = GameFeatureVersePathMapper::EBuildLookupOptions::None;
	if (Args.bWithDynamicCookPlugins)
	{
		BuildOptions |= GameFeatureVersePathMapper::EBuildLookupOptions::WithDynamicCookPlugins;
	}
	TOptional<GameFeatureVersePathMapper::FGameFeatureVersePathLookup> MaybeLookup = GameFeatureVersePathMapper::BuildLookup(Args.TargetPlatform, DevARPtr, BuildOptions, ReferencedPackageNamesPtr);
	if (!MaybeLookup)
	{
		// BuildLookup will emit errors
		return 1;
	}
	GameFeatureVersePathMapper::FGameFeatureVersePathLookup& Lookup = *MaybeLookup;

	// Write the binary before building the JSON: the JSON builder MoveTemps GfpUri out of each entry.
	if (Args.TargetPlatform && Args.bSerializeVerseLookupBinary)
	{
		const FString BinaryOutputPath = GameFeatureVersePathMapper::GetVerseLookupBinaryPath(Args.TargetPlatform->PlatformName());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(BinaryOutputPath));

		TArray<uint8> Data;
		FMemoryWriter MemoryWriter(Data, true);
		if (!GameFeatureVersePathMapper::SerializeLookupBinary(MemoryWriter, Lookup))
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to serialize binary lookup to {Path}", BinaryOutputPath);
			return 1;
		}
		if (!FFileHelper::SaveArrayToFile(Data, *BinaryOutputPath))
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to write binary lookup to {Path}", BinaryOutputPath);
			return 1;
		}
		UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Wrote binary lookup to {Path}", BinaryOutputPath);
	}

	TSharedRef<FJsonObject> OutJsonObject = MakeShared<FJsonObject>();
	{
		{
			// Reversing the VersePathToGfpMap makes it more natural for the registration API
			TMap<FName, TSharedRef<FJsonValueString>> TempGfpVersePathMap;
			TempGfpVersePathMap.Reserve(Lookup.VersePathToGfpMap.Num());
			for (const TPair<FString, FName>& Pair : Lookup.VersePathToGfpMap)
			{
				TempGfpVersePathMap.Emplace(Pair.Value, MakeShared<FJsonValueString>(Pair.Key));
			}

			TSharedRef<FJsonObject> GfpVersePathMap = MakeShared<FJsonObject>();

			// Sort the reversed map in dependency order
			GameFeatureVersePathMapper::FDepthFirstGameFeatureSorter Sorter(Lookup.GfpInfoMap, true /*bIncludeVirtualNodes*/);
			Sorter.Sort(
				[It = TempGfpVersePathMap.CreateConstIterator()]() mutable -> FName
				{
					if (!It)
					{
						return {};
					}
					FName Plugin = It.Key();
					++It;
					return Plugin;
				},
				[&TempGfpVersePathMap, GfpVersePathMap](FName OutPlugin, const FString& OutGfpUri)
				{
					GfpVersePathMap->SetField(OutPlugin.ToString(), TempGfpVersePathMap.FindChecked(OutPlugin));
				});

			OutJsonObject->SetField(TEXT("GfpVersePathMap"), MakeShared<FJsonValueObject>(GfpVersePathMap));
		}

		{
			TSharedRef<FJsonObject> GfpInfoMap = MakeShared<FJsonObject>();
			for (TPair<FName, FGameFeaturePluginInfo>& Pair : Lookup.GfpInfoMap)
			{
				TSharedRef<FJsonObject> GfpInfo = MakeShared<FJsonObject>();
				GfpInfo->SetField(TEXT("GfpUri"), MakeShared<FJsonValueString>(MoveTemp(Pair.Value.GfpUri)));

				TArray<TSharedPtr<FJsonValue>> Dependencies;
				Dependencies.Reserve(Pair.Value.Dependencies.Num());
				Algo::Transform(Pair.Value.Dependencies, Dependencies, [](FName Name) { return MakeShared<FJsonValueString>(Name.ToString()); });
				GfpInfo->SetField(TEXT("Dependencies"), MakeShared<FJsonValueArray>(MoveTemp(Dependencies)));

				TArray<TSharedPtr<FJsonValue>> ImplicitDependencies;
				ImplicitDependencies.Reserve(Pair.Value.ImplicitDependencies.Num());
				Algo::Transform(Pair.Value.ImplicitDependencies, ImplicitDependencies, [](FName Name) { return MakeShared<FJsonValueString>(Name.ToString()); });
				GfpInfo->SetField(TEXT("ImplicitDependencies"), MakeShared<FJsonValueArray>(MoveTemp(ImplicitDependencies)));

				GfpInfoMap->SetField(Pair.Key.ToString(), MakeShared<FJsonValueObject>(GfpInfo));
			}

			OutJsonObject->SetField(TEXT("GfpInfoMap"), MakeShared<FJsonValueObject>(GfpInfoMap));
		}
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Args.OutputPath));

	TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(*Args.OutputPath));
	TSharedRef<TJsonWriter<UTF8CHAR>> JsonWriter = TJsonWriterFactory<UTF8CHAR>::Create(FileWriter.Get());
	if (!FJsonSerializer::Serialize(OutJsonObject, JsonWriter))
	{
		UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to save output file at {Path}", Args.OutputPath);
		return 1;
	}

	return 0;
}

/*static*/ FString UGameFeatureVersePathMapperCommandlet::GetGameFeatureRootVersePath()
{
	return FPaths::Combine(GameFeatureVersePathMapper::GetVerseAppDomain(), TEXTVIEW("GameFeatures"));
}

bool FGameFeaturePluginInfo::Serialize(FArchive& Ar)
{
	UScriptStruct* Struct = FGameFeaturePluginInfo::StaticStruct();
	Struct->SerializeTaggedProperties(Ar, reinterpret_cast<uint8*>(this), Struct, nullptr);
	return true;
}
