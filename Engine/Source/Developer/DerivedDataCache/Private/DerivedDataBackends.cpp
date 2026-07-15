// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBackendInterface.h"

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "DerivedDataCacheKeyFilter.h"
#include "DerivedDataCacheMaintainer.h"
#include "DerivedDataCacheMethod.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheReplay.h"
#include "DerivedDataCacheStore.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataConfig.h"
#include "DerivedDataRequestOwner.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/ThreadSafeCounter.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Logging/StructuredLog.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Math/UnitConversion.h"
#include "MemoryCacheStore.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineBuildSettings.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "Modules/ModuleManager.h"
#include "PakFileCacheStore.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinaryPackage.h"
#include "String/Find.h"
#include <atomic>

DEFINE_LOG_CATEGORY(LogDerivedDataCache);
LLM_DEFINE_TAG(UntaggedDDCResult);
LLM_DEFINE_TAG(DDCBackend);

#define LOCTEXT_NAMESPACE "DerivedDataBackendGraph"

static TAutoConsoleVariable<FString> GDerivedDataCacheGraphName(
	TEXT("DDC.Graph"),
	TEXT(""),
	TEXT("Name or config of the graph to use for the Derived Data Cache."),
	ECVF_ReadOnly);

namespace UE::DerivedData
{

ILegacyCacheStore* CreateCacheStoreAsync(ICacheStoreOwner& Owner, ILegacyCacheStore* InnerBackend, IMemoryCacheStore* MemoryCache, bool bDeleteInnerCache);
ILegacyCacheStore* CreateCacheStoreHierarchy(ICacheStoreOwner*& OutOwner, ICacheStoreMaintainer*& OutMaintainer, TFunctionRef<void (IMemoryCacheStore*&)> MemoryCacheCreator);
ICacheStoreOwner* CreateCacheStoreThrottle(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ICacheStoreOwner* CreateCacheStoreDebug(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ILegacyCacheStore* CreateCacheStoreVerify(ILegacyCacheStore* InnerCache, bool bPutOnError);
ILegacyCacheStore* TryCreateCacheStoreReplay(ILegacyCacheStore* InnerCache, ICacheStoreOwner& Owner);

ILegacyCacheStore* CreateFileSystemCacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ILegacyCacheStore* CreateHttpCacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ILegacyCacheStore* CreateMemoryCacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ILegacyCacheStore* CreatePakFileCacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ILegacyCacheStore* CreateS3CacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
ILegacyCacheStore* CreateZenCacheStore(const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);

void CreateMemoryCacheStore(IMemoryCacheStore*& OutCache, const TCHAR* Name, const TCHAR* Config, ICacheStoreOwner& Owner);
IPakFileCacheStore* CreatePakFileCacheStore(const TCHAR* Name, const TCHAR* Filename, bool bWriting, bool bCompressed, ICacheStoreOwner& Owner);

/**
 * This class is used to create a singleton that represents the derived data cache hierarchy and all of the wrappers necessary
 * ideally this would be data driven and the backends would be plugins...
 */
class FDerivedDataBackendGraph : public FDerivedDataBackend
{
public:
	using FParsedNode = ILegacyCacheStore*;
	using FParsedNodeMap = TMap<FString, FParsedNode>;

	FDerivedDataBackendGraph()
	{
		// Needs to be initialized from the main thread because it uses config and console variables
		check(IsInGameThread());
		check(GConfig && GConfig->IsReadyForUse());
	}

	static FDerivedDataBackendGraph* TryCreate(FStringView GraphNameOrConfig)
	{
		FDerivedDataBackendGraph* Graph = new FDerivedDataBackendGraph();
		if (Graph->CreateGraph(GraphNameOrConfig))
		{
			Graph->PostCreateGraph();
			return Graph;
		}
		delete Graph;
		return nullptr;
	}

protected:
	bool CreateGraph(FStringView GraphNameOrConfig)
	{
		ActiveGraphName = GraphNameOrConfig;
		FParsedNodeMap ParsedNodes;
		return ParseGraph(ActiveGraphName, ParsedNodes) && Hierarchy->HasAllFlags(ECacheStoreFlags::Query | ECacheStoreFlags::Store);
	}

	void ResetGraph()
	{
		delete RootCache;
		RootCache = nullptr;
		MemoryCache = nullptr;
		WritePakCache = nullptr;
		Hierarchy = nullptr;
		Maintainer = nullptr;
		ReadPakCache.Empty();
		ActiveGraphName.Empty();
	}

	void PostCreateGraph()
	{
		// Async must exist in the graph.
		RootCache = CreateCacheStoreAsync(*Hierarchy, RootCache, MemoryCache, /*bDeleteInnerCache*/ true);

		// Create a Verify node when using -DDC-Verify[=Type1[@Rate2][+Type2[@Rate2]...]] or -DDC-VerifyKeys=Key1[+Key2...].
		if (FString VerifyArg;
			FParse::Param(FCommandLine::Get(), TEXT("DDC-Verify")) ||
			FParse::Value(FCommandLine::Get(), TEXT("-DDC-Verify="), VerifyArg) ||
			FParse::Value(FCommandLine::Get(), TEXT("-DDC-VerifyKeys="), VerifyArg))
		{
			if (!UE::GetMultiprocessId())
			{
				IFileManager::Get().DeleteDirectory(*(FPaths::ProjectSavedDir() / TEXT("VerifyDDC/")), /*bRequireExists*/ false, /*bTree*/ true);
			}
			RootCache = CreateCacheStoreVerify(RootCache, /*bPutOnError*/ false);
		}

		// Create a Replay node when requested on the command line.
		if (ILegacyCacheStore* ReplayNode = TryCreateCacheStoreReplay(RootCache, *Hierarchy))
		{
			RootCache = ReplayNode;
		}
	}

private:
	/**
	 * Parses backend graph node from ini settings
	 *
	 * @param NodeName Name of the node to parse
	 * @param IniFilename Ini filename
	 * @param IniSection Section in the ini file containing the graph definition
	 * @param InParsedNodes Map of parsed nodes and their names to be able to find already parsed nodes
	 * @return Derived data backend interface instance created from ini settings
	 */
	bool ParseNode(FString StoreName, FStringView GraphNameOrConfig, FParsedNodeMap& InParsedNodes)
	{
		if (const FParsedNode* ParsedNode = InParsedNodes.Find(StoreName))
		{
			UE_LOGFMT(LogDerivedDataCache, Display, "Node '{Store}' was referenced more than once in cache graph '{Graph}'. Nodes may not be shared.", StoreName, GraphNameOrConfig);
			return false;
		}

		TStringBuilder<256> StoreConfig;
		if (!TryFindCacheStoreConfig(StoreName, GraphNameOrConfig, StoreConfig))
		{
			UE_LOGFMT(LogDerivedDataCache, Log, "Failed to find config for cache store '{Store}' in cache graph '{Graph}'.", StoreName, GraphNameOrConfig);
			return false;
		}

		FString Entry(StoreConfig);
		{
			Entry.TrimStartInline();
			Entry.RemoveFromStart(TEXT("("));
			Entry.RemoveFromEnd(TEXT(")"));

			ICacheStoreOwner* NodeOwner = Hierarchy;

			// Create a throttle owner if throttle options are specified for this cache store.
			ICacheStoreOwner* ThrottleOwner = CreateCacheStoreThrottle(*StoreName, *Entry, *NodeOwner);
			if (ThrottleOwner)
			{
				NodeOwner = ThrottleOwner;
			}

			// Create a debug owner if debug options are specified for this cache store.
			ICacheStoreOwner* DebugOwner = CreateCacheStoreDebug(*StoreName, *Entry, *NodeOwner);
			if (DebugOwner)
			{
				NodeOwner = DebugOwner;
			}

			ON_SCOPE_EXIT
			{
				if (!InParsedNodes.Contains(StoreName))
				{
					delete DebugOwner;
					delete ThrottleOwner;
				}
			};

			FString	NodeType;
			if (FParse::Value(*Entry, TEXT("Type="), NodeType))
			{
				if (NodeType == TEXT("FileSystem"))
				{
					if (ILegacyCacheStore* CacheStore = RedirectOrCreateFileSystemCacheStore(*StoreName, *Entry, *NodeOwner, InParsedNodes))
					{
						InParsedNodes.Add(StoreName, CacheStore);
						return true;
					}
				}
				else if (NodeType == TEXT("Memory"))
				{
					if (ILegacyCacheStore* CacheStore = CreateMemoryCacheStore(*StoreName, *Entry, *NodeOwner))
					{
						InParsedNodes.Add(StoreName, CacheStore);
						return true;
					}
				}
				else if (NodeType == TEXT("ReadPak") || NodeType == TEXT("WritePak"))
				{
					if (IPakFileCacheStore* PakStore = static_cast<IPakFileCacheStore*>(CreatePakFileCacheStore(*StoreName, *Entry, *NodeOwner)))
					{
						FString PakFilename;
						FParse::Value(*Entry, TEXT("Filename="), PakFilename);

						if (PakStore->IsWritable())
						{
							if (WritePakCache)
							{
								UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to create %s pak cache because only one pak cache write node is supported."), *StoreName);
								delete PakStore;
								return false;
							}
							ReadPakFilename = PakFilename;
							WritePakFilename = PakStore->GetFilename();
							WritePakCache = PakStore;
						}
						else
						{
							ReadPakCache.Add(PakStore);
						}
						InParsedNodes.Add(StoreName, PakStore);
						return true;
					}
				}
				else if (NodeType == TEXT("S3"))
				{
					if (ILegacyCacheStore* CacheStore = CreateS3CacheStore(*StoreName, *Entry, *NodeOwner))
					{
						InParsedNodes.Add(StoreName, CacheStore);
						return true;
					}
				}
				else if (NodeType == TEXT("Cloud") || NodeType == TEXT("Http"))
				{
					if (ILegacyCacheStore* CacheStore = CreateHttpCacheStore(*StoreName, *Entry, *NodeOwner))
					{
						InParsedNodes.Add(StoreName, CacheStore);
						return true;
					}
				}
				else if (NodeType == TEXT("Zen"))
				{
					if (ILegacyCacheStore* CacheStore = CreateZenCacheStore(*StoreName, *Entry, *NodeOwner))
					{
						InParsedNodes.Add(StoreName, CacheStore);
						return true;
					}
				}
			}
		}

		return false;
	}

	bool ParseGraph(FStringView GraphNameOrConfig, FParsedNodeMap& InParsedNodes)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FDerivedDataBackendGraph::ParseGraph);

		UE_LOGFMT(LogDerivedDataCache, Verbose, "Parsing cache graph '{Graph}'...", GraphNameOrConfig);

		RootCache = CreateCacheStoreHierarchy(Hierarchy, Maintainer, [this](IMemoryCacheStore*& OutCache) { GetMemoryCache(OutCache); });

		TStringBuilder<256> GraphConfig;
		if (!TryFindCacheGraphConfig(GraphNameOrConfig, GraphConfig))
		{
			return false;
		}

		bool bParsed = false;
		Config::FConfigIterator It(GraphConfig);
		for (; It; ++It)
		{
			bParsed |= ParseNode(FString(It.GetValue()), GraphNameOrConfig, InParsedNodes);
		}

		if (It.HasError())
		{
			UE_LOGFMT(LogDerivedDataCache, Warning, "Failed to parse cache graph '{Graph}'.", GraphNameOrConfig);
			return false;
		}

		if (!bParsed)
		{
			UE_LOGFMT(LogDerivedDataCache, Warning, "Failed to create at least one cache store for cache graph '{Graph}'.", GraphNameOrConfig);
			return false;
		}

		return true;
	}

	ILegacyCacheStore* RedirectOrCreateFileSystemCacheStore(const TCHAR* NodeName, const TCHAR* Config, ICacheStoreOwner& Owner, FParsedNodeMap& InParsedNodes)
	{
		FString RedirectionFileName;
		FString RedirectionKeyName;
		FParse::Value(Config, TEXT("RedirectionFileName="), RedirectionFileName);
		FParse::Value(Config, TEXT("RedirectionKeyName="), RedirectionKeyName);

		if (!RedirectionFileName.IsEmpty())
		{
			FString CachePath;
			FParse::Value(Config, TEXT("Path="), CachePath);

			FString Key;
			if (FParse::Value(Config, TEXT("EnvPathOverride="), Key))
			{
				if (FString Value = FPlatformMisc::GetEnvironmentVariable(*Key); !Value.IsEmpty())
				{
					CachePath = MoveTemp(Value);
				}
				if (FString Value; FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *Key, Value) && !Value.IsEmpty())
				{
					CachePath = MoveTemp(Value);
				}
			}

			if (FParse::Value(Config, TEXT("CommandLineOverride="), Key))
			{
				FParse::Value(FCommandLine::Get(), *(Key + TEXT("=")), CachePath);
			}

			if (!CachePath.IsEmpty() && CachePath != TEXTVIEW("None"))
			{
				FString RedirectionFilePath = FPaths::Combine(CachePath, RedirectionFileName);
				FConfigFile RedirectionFile;
				if (FConfigCacheIni::LoadLocalIniFile(RedirectionFile, *RedirectionFilePath, false))
				{
					FString KeyName = RedirectionKeyName.IsEmpty() ? TEXT("Default") : RedirectionKeyName;
					FString RedirectionData;
					if (RedirectionFile.GetString(TEXT("Redirect"), *KeyName, RedirectionData))
					{
						RedirectionData.TrimStartInline();
						RedirectionData.RemoveFromStart(TEXT("("));
						RedirectionData.RemoveFromEnd(TEXT(")"));

						FString EnvName;
						FString EnvValue;
						if (FParse::Value(*RedirectionData, TEXT("SetEnvName="), EnvName) && FParse::Value(*RedirectionData, TEXT("SetEnvValue="), EnvValue))
						{
							FPlatformMisc::SetEnvironmentVar(*EnvName, *EnvValue);
						}

						FString TargetName;
						if (FParse::Value(*RedirectionData, TEXT("Target="), TargetName))
						{
							UE_LOGFMT(LogDerivedDataCache, Log, "{Node}: Found redirection to '{Target}'", NodeName, TargetName);

							if (const FParsedNode* ExistingNode = InParsedNodes.Find(TargetName))
							{
								UE_LOGFMT(LogDerivedDataCache, Log, "{Node}: Successfully redirected to '{Target}'", NodeName, TargetName);
								return *ExistingNode;
							}

							if (ParseNode(TargetName, ActiveGraphName, InParsedNodes))
							{
								if (const FParsedNode* ParsedNode = InParsedNodes.Find(TargetName))
								{
									UE_LOGFMT(LogDerivedDataCache, Log, "{Node}: Successfully redirected to '{Target}'", NodeName, TargetName);
									return *ParsedNode;
								}
							}

							UE_LOGFMT(LogDerivedDataCache, Warning, "{Node}: Failed to redirect to '{Target}'", NodeName, TargetName);
						}
					}
				}
			}
		}

		return CreateFileSystemCacheStore(NodeName, Config, Owner);
	}

	void GetMemoryCache(IMemoryCacheStore*& OutCache)
	{
		if (MemoryCache)
		{
			OutCache = MemoryCache;
		}
		else
		{
			// This is unconditionally added to the hierarchy and will be deleted by the hierarchy.
			check(Hierarchy);
			CreateMemoryCacheStore(OutCache, TEXT("Memory"), TEXT("-ReadOnly -StopGetStore -NoStats"), *Hierarchy);
			MemoryCache = OutCache;
		}
	}

protected:
	virtual ~FDerivedDataBackendGraph()
	{
		delete RootCache;
	}

	bool HasRoot() const
	{
		return !!RootCache;
	}

	ILegacyCacheStore& GetRoot() final
	{
		check(RootCache);
		return *RootCache;
	}

	ICacheStoreOwner* GetOwner() const
	{
		return Hierarchy;
	}

private:
	virtual void WaitForQuiescence(bool bShutdown) override
	{
		check(Hierarchy);

		if (bShutdown)
		{
			Hierarchy->SetShuttingDown();
		}

		Hierarchy->WaitForIdle();

		if (bShutdown)
		{
			FString MergePaks;
			if (WritePakCache && WritePakCache->IsWritable() && FParse::Value(FCommandLine::Get(), TEXT("MergePaks="), MergePaks))
			{
				TArray<FString> MergePakList;
				MergePaks.FString::ParseIntoArray(MergePakList, TEXT("+"));

				for(const FString& MergePakName : MergePakList)
				{
					TUniquePtr<IPakFileCacheStore> ReadPak(
						CreatePakFileCacheStore(TEXT("Merge"), *FPaths::Combine(*FPaths::GetPath(WritePakFilename), *MergePakName), /*bWriting*/ false, /*bCompressed*/ false, *Hierarchy));
					WritePakCache->MergeCache(ReadPak.Get());
				}
			}
			for (int32 ReadPakIndex = 0; ReadPakIndex < ReadPakCache.Num(); ReadPakIndex++)
			{
				ReadPakCache[ReadPakIndex]->Close();
			}
			ReadPakCache.Empty();
			if (WritePakCache && WritePakCache->IsWritable())
			{
				WritePakCache->Close();
				WritePakCache = nullptr;
				if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*WritePakFilename))
				{
					UE_LOGF(LogDerivedDataCache, Error, "Pak file %ls was not produced?", *WritePakFilename);
				}
				else
				{
					if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*ReadPakFilename))
					{
						FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ReadPakFilename, false);
						if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*ReadPakFilename))
						{
							UE_LOGF(LogDerivedDataCache, Error, "Could not delete the pak file %ls to overwrite it with a new one.", *ReadPakFilename);
						}
					}
					if (!IPakFileCacheStore::SortAndCopy(WritePakFilename, ReadPakFilename))
					{
						UE_LOGF(LogDerivedDataCache, Error, "Couldn't sort pak file (%ls)", *WritePakFilename);
					}
					else if (!IFileManager::Get().Delete(*WritePakFilename))
					{
						UE_LOGF(LogDerivedDataCache, Error, "Couldn't delete pak file (%ls)", *WritePakFilename);
					}
					else
					{
						UE_LOGF(LogDerivedDataCache, Display, "Successfully wrote %ls.", *ReadPakFilename);
					}
				}
			}
		}
	}

	/** Get whether a shared cache is in use */
	virtual bool GetUsingSharedDDC() const override
	{
		return bUsingSharedDDC;
	}

	virtual void AddToAsyncCompletionCounter(int32 Addend) override
	{
		check(Hierarchy);
		verify(Hierarchy->AddToAsyncTaskCounter(Addend) + Addend >= 0);
	}

	virtual bool AnyAsyncRequestsRemaining() override
	{
		check(Hierarchy);
		return Hierarchy->AddToAsyncTaskCounter(0) > 0;
	}

	bool IsShuttingDown() final
	{
		check(Hierarchy);
		return Hierarchy->IsShuttingDown();
	}

protected:
	virtual const TCHAR* GetGraphName() const override
	{
		return *ActiveGraphName;
	}

	virtual const TCHAR* GetDefaultGraphName() const final
	{
		return FApp::IsEngineInstalled() ? TEXT("Installed") : TEXT("Default");
	}

	virtual ILegacyCacheStore* MountPakFile(const TCHAR* PakFilename) override
	{
		// Assumptions: there's at least one read-only pak backend in the hierarchy
		// and its parent is a hierarchical backend.
		IPakFileCacheStore* ReadPak = nullptr;
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(PakFilename))
		{
			check(Hierarchy);
			ReadPak = CreatePakFileCacheStore(TEXT("Mount"), PakFilename, /*bWriting*/ false, /*bCompressed*/ false, *Hierarchy);
			ReadPakCache.Add(ReadPak);
		}
		else
		{
			UE_LOGF(LogDerivedDataCache, Warning, "Failed to add %ls read-only pak DDC backend. Make sure it exists and there's at least one hierarchical backend in the cache tree.", PakFilename);
		}

		return ReadPak;
	}

	virtual bool UnmountPakFile(const TCHAR* PakFilename) override
	{
		for (int PakIndex = 0; PakIndex < ReadPakCache.Num(); ++PakIndex)
		{
			IPakFileCacheStore* ReadPak = ReadPakCache[PakIndex];
			if (ReadPak->GetFilename() == PakFilename)
			{
				// Wait until all async requests are complete.
				WaitForQuiescence(false);

				ReadPakCache.RemoveAt(PakIndex);
				ReadPak->Close();
				delete ReadPak;
				return true;
			}
		}
		return false;
	}

private:
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override
	{
		TSharedRef<FDerivedDataCacheStatsNode> Stats = MakeShared<FDerivedDataCacheStatsNode>();
		if (RootCache)
		{
			RootCache->LegacyStats(Stats.Get());
		}
		return Stats;
	}

	virtual void GatherResourceStats(TArray<FDerivedDataCacheResourceStat>& DDCResourceStats) const override
	{
		if (Hierarchy)
		{
			Hierarchy->LegacyResourceStats(DDCResourceStats);
		}
	}

	virtual void GatherVerificationStats(FDerivedDataCacheVerificationStats& OutStats) const override
	{
		if (RootCache)
		{
			RootCache->GatherVerificationStats(OutStats);
		}
	}

	// ICache Interface

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DDC_Put);
		return RootCache->Put(Requests, Owner, OnComplete ? MoveTemp(OnComplete) : FOnCachePutComplete([](auto&&) {}));
	}

	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DDC_Get);
		return RootCache->Get(Requests, Owner, OnComplete ? MoveTemp(OnComplete) : FOnCacheGetComplete([](auto&&) {}));
	}

	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DDC_PutValue);
		return RootCache->PutValue(Requests, Owner, OnComplete ? MoveTemp(OnComplete) : FOnCachePutValueComplete([](auto&&) {}));
	}

	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DDC_GetValue);
		return RootCache->GetValue(Requests, Owner, OnComplete ? MoveTemp(OnComplete) : FOnCacheGetValueComplete([](auto&&) {}));
	}

	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DDC_GetChunks);
		return RootCache->GetChunks(Requests, Owner, OnComplete ? MoveTemp(OnComplete) : FOnCacheGetChunkComplete([](auto&&) {}));
	}

	ICacheStoreMaintainer& GetMaintainer() final
	{
		check(Maintainer);
		return *Maintainer;
	}

	FString											ActiveGraphName;
	FString											ReadPakFilename;
	FString											WritePakFilename;

	/** Root of the graph */
	ILegacyCacheStore* RootCache = nullptr;

	/** Instances of backend interfaces which exist in only one copy */
	IMemoryCacheStore* MemoryCache = nullptr;
	IPakFileCacheStore* WritePakCache = nullptr;
	ICacheStoreOwner* Hierarchy = nullptr;
	ICacheStoreMaintainer* Maintainer = nullptr;
	/** Support for multiple read only pak files. */
	TArray<IPakFileCacheStore*> ReadPakCache;

	/** Whether a shared cache is in use */
	bool bUsingSharedDDC = false;
};

class FDerivedDataBackendStaticGraph final : public FDerivedDataBackendGraph
{
public:
	FDerivedDataBackendStaticGraph()
		: MountPakCommand(
			TEXT("DDC.MountPak"),
			*LOCTEXT("CommandText_DDCMountPak", "Mounts read-only pak file").ToString(),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDerivedDataBackendStaticGraph::MountPakCommandHandler))
		, UnmountPakCommand(
			TEXT("DDC.UnmountPak"),
			*LOCTEXT("CommandText_DDCUnmountPak", "Unmounts read-only pak file").ToString(),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDerivedDataBackendStaticGraph::UnmountPakCommandHandler))
		, LoadReplayCommand(
			TEXT("DDC.LoadReplay"),
			*LOCTEXT("CommandText_DDCLoadReplay", "Loads a cache replay file created by -DDC-ReplaySave=<Path>").ToString(),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDerivedDataBackendStaticGraph::LoadReplayCommandHandler))
	{
		check(!StaticGraph);
		StaticGraph = this;

		TArray<FString> CandidateGraphs;

		// Gather graphs from the command line as candidates.
		{
			FString GraphNameOrConfig;
			for (const TCHAR* Remaining = FCommandLine::Get(); FParse::Value(Remaining, TEXT("-DDC="), GraphNameOrConfig, /*bShouldStopOnSeparator*/ false, &Remaining);)
			{
				CandidateGraphs.Add(MoveTemp(GraphNameOrConfig));
			}
		}

		// Add the graph from the CVar as a candidate.
		if (FString GraphNameOrConfig = GDerivedDataCacheGraphName.GetValueOnGameThread(); !GraphNameOrConfig.IsEmpty())
		{
			CandidateGraphs.Add(MoveTemp(GraphNameOrConfig));
		}

		// Add the default graph as a candidate unless it has been suppressed.
		if (!FParse::Param(FCommandLine::Get(), TEXT("DDC-NoDefaultGraph")))
		{
			// UE_DEPRECATED(5.8, "Remove the legacy default block after the standard deprecation period.")
			const TCHAR* const LegacyDefaultGraphName = FApp::IsEngineInstalled() ? TEXT("InstalledDerivedDataBackendGraph") : TEXT("DerivedDataBackendGraph");
			if (GConfig->DoesSectionExist(LegacyDefaultGraphName, GEngineIni))
			{
				CandidateGraphs.Add(LegacyDefaultGraphName);
			}

			CandidateGraphs.Add(GetDefaultGraphName());
		}

		// Construct candidate graphs in the order they were added until one is valid.
		bool bCreatedGraph = false;
		FStringView GraphNameOrConfig;
		for (const FString& CandidateGraph : CandidateGraphs)
		{
			const FStringView FailedGraphNameOrConfig = GraphNameOrConfig;
			GraphNameOrConfig = CandidateGraph;

			if (!FailedGraphNameOrConfig.IsEmpty())
			{
				ICacheStoreOwner* Owner = GetOwner();
				const ANSICHAR* MissingTypes = 
					Owner->HasAllFlags(ECacheStoreFlags::Query) ? "writable" :
					Owner->HasAllFlags(ECacheStoreFlags::Store) ? "readable" : "readable or writable";
				UE_LOGFMT(LogDerivedDataCache, Warning,
					"Failed to create graph '{Graph}' because it has no {MissingTypes} nodes available. "
					"Attempting to create graph '{NextGraph}'...",
					FailedGraphNameOrConfig, MissingTypes, GraphNameOrConfig);
				ResetGraph();
			}

			bCreatedGraph = CreateGraph(GraphNameOrConfig);
			if (bCreatedGraph)
			{
				break;
			}
		}

		if (!bCreatedGraph)
		{
			ICacheStoreOwner* Owner = GetOwner();
			TStringBuilder<256> GraphConfig;
			if (!TryFindCacheGraphConfig(GraphNameOrConfig, GraphConfig))
			{
				UE_LOGFMT(LogDerivedDataCache, Fatal,
					"Failed to create cache graph '{Graph}' because its config is missing in the '{Config}' config.",
					GraphNameOrConfig, GEngineIni);
			}

			const ANSICHAR* MissingTypes = 
				Owner->HasAllFlags(ECacheStoreFlags::Query) ? "writable" :
				Owner->HasAllFlags(ECacheStoreFlags::Store) ? "readable" : "readable or writable";

			if (FParse::Param(FCommandLine::Get(), TEXT("DDC-ForceMemoryCache")))
			{
				UE_LOGFMT(LogDerivedDataCache, Error,
					"Unable to use cache graph '{Graph}' because it has no {MissingTypes} nodes available. "
					"Creating a memory cache as a fallback to avoid crashing immediately.",
					GraphNameOrConfig, MissingTypes);
				IMemoryCacheStore* Memory = nullptr;
				CreateMemoryCacheStore(Memory, TEXT("ForceMemoryCache"), TEXT(""), *Owner);
				check(Owner->HasAllFlags(ECacheStoreFlags::Query | ECacheStoreFlags::Store));
			}
			else if (!FParse::Param(FCommandLine::Get(), TEXT("DDC-AllowNoActiveStores")))
			{
			#if WITH_EDITOR
				if (!FApp::IsUnattended() && !IsRunningCommandlet() && !GIsRunningUnattendedScript)
				{
					const FString StoreId = TEXT("Epic Games");
					const FString SectionName = TEXT("GlobalDataCachePath");
					const FString KeyName = TEXT("UE-LocalDataCachePath");

					// Offer to reset the local data cache path if it has been set.
					if (FString LocalDataCachePath; FPlatformMisc::GetStoredValue(StoreId, SectionName, KeyName, LocalDataCachePath))
					{
						const FText ErrorTitle = LOCTEXT("DDC_NoActiveStore_ErrorMsgTitle", "Derived Data Cache Error");
						const FText ErrorMessage = FText::Format(LOCTEXT("DDC_NoActiveStore_ErrorMsgText",
							"Unable to use cache graph '{Graph}'.\n\n"
							"The local cache path is currently overridden by the setting\nUE-LocalDataCachePath: {LocalDataCachePath}\n\n"
							"Do you want to reset this setting and restart the editor?"),
							FText::FromStringView(GraphNameOrConfig), FText::FromString(LocalDataCachePath));
						const EAppReturnType::Type Result = FPlatformMisc::MessageBoxExt(EAppMsgType::YesNo, *ErrorMessage.ToString(), *ErrorTitle.ToString());
						if (Result == EAppReturnType::Yes)
						{
							UE_LOGFMT(LogDerivedDataCache, Error,
								"Unable to use default cache graph '{Graph}' because it has no {MissingTypes} nodes available."
								"'UE-LocalDataCachePath' setting will be reset.",
								GraphNameOrConfig, MissingTypes);

							FPlatformMisc::DeleteStoredValue(StoreId, SectionName, KeyName);
							FPlatformProcess::CreateProc(FPlatformProcess::ExecutablePath(), FCommandLine::GetOriginal(),
								/*bLaunchDetached*/true, /*bLaunchHidden*/false, /*bLaunchReallyHidden*/false,
								/*OutProcessId*/nullptr, /*PriorityModifier*/0, /*OptionalWorkingDirectory*/nullptr, /*PipeWriteChild*/nullptr);
							FPlatformMisc::RequestExit(/*Force*/false);
							return;
						}
					}
				}
			#endif

				UE_LOGFMT(LogDerivedDataCache, Fatal,
					"Unable to use cache graph '{Graph}' because it has no {MissingTypes} nodes available. "
					"Add -DDC-ForceMemoryCache to the command line to bypass this if you need access "
					"to the editor settings to fix the cache configuration.",
					GraphNameOrConfig, MissingTypes);
			}
		}

		PostCreateGraph();
	}

	~FDerivedDataBackendStaticGraph() final
	{
		check(StaticGraph == this);
		StaticGraph = nullptr;
		Replays.Empty();
	}

	static FORCEINLINE FDerivedDataBackendGraph& GetStatic()
	{
		check(StaticGraph);
		return *StaticGraph;
	}

private:
	void MountPakCommandHandler(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOGF(LogDerivedDataCache, Log, "Usage: DDC.MountPak PakFilename");
			return;
		}
		MountPakFile(*Args[0]);
	}

	void UnmountPakCommandHandler(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOGF(LogDerivedDataCache, Log, "Usage: DDC.UnmountPak PakFilename");
			return;
		}
		UnmountPakFile(*Args[0]);
	}

	void LoadReplayCommandHandler(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOGF(LogDerivedDataCache, Log, "Usage: DDC.LoadReplay ReplayPath"
				" [Methods=Get+GetValue+GetChunks]"
				" [Rate=<0-100>]"
				" [Types=Type1[@Rate1][+Type2[@Rate2]]..."
				" [Salt=PositiveInt32]"
				" [Priority=<Lowest-Blocking>]"
				" [AddPolicy=Query,SkipData]"
				" [RemovePolicy=SkipMeta]");
			return;
		}

		TStringBuilder<512> JoinedArgs;
		JoinedArgs.Join(MakeArrayView(Args).RightChop(1), TEXT(' '));

		FCacheReplayReader Replay(&GetRoot());

		// Parse Key Filter
		const bool bDefaultMatch = String::FindFirst(*JoinedArgs, TEXT("Types="), ESearchCase::IgnoreCase) == INDEX_NONE;
		float DefaultRate = bDefaultMatch ? 100.0f : 0.0f;
		FParse::Value(*JoinedArgs, TEXT("Rate="), DefaultRate);

		FCacheKeyFilter KeyFilter = FCacheKeyFilter::Parse(*JoinedArgs, TEXT("Types="), nullptr, DefaultRate);

		uint32 Salt;
		if (FParse::Value(*JoinedArgs, TEXT("Salt="), Salt))
		{
			if (Salt == 0)
			{
				UE_LOGF(LogDerivedDataCache, Warning,
					"Replay: Ignoring salt of 0. The salt must be a positive integer.");
			}
			else
			{
				KeyFilter.SetSalt(Salt);
			}
		}

		UE_CLOGF(KeyFilter.RequiresSalt(), LogDerivedDataCache, Display,
			"Replay: Using salt %u to filter cache keys to replay.", KeyFilter.GetSalt());

		Replay.SetKeyFilter(MoveTemp(KeyFilter));

		// Parse Method Filter
		FString MethodNames;
		if (FParse::Value(*JoinedArgs, TEXT("Methods="), MethodNames))
		{
			Replay.SetMethodFilter(FCacheMethodFilter::Parse(MethodNames));
		}

		// Parse Policy Transform
		ECachePolicy FlagsToAdd = ECachePolicy::None;
		FString FlagNamesToAdd;
		if (FParse::Value(*JoinedArgs, TEXT("AddPolicy="), FlagNamesToAdd))
		{
			TryLexFromString(FlagsToAdd, FlagNamesToAdd);
		}
		ECachePolicy FlagsToRemove = ECachePolicy::None;
		FString FlagNamesToRemove;
		if (FParse::Value(*JoinedArgs, TEXT("RemovePolicy="), FlagNamesToRemove))
		{
			TryLexFromString(FlagsToRemove, FlagNamesToRemove);
		}
		Replay.SetPolicyTransform(FlagsToAdd, FlagsToRemove);

		// Parse Priority Override
		EPriority Priority{};
		FString PriorityName;
		if (FParse::Value(*JoinedArgs, TEXT("Priority="), PriorityName) &&
			TryLexFromString(Priority, PriorityName))
		{
			Replay.SetPriorityOverride(Priority);
		}

		Replay.ReadFromFileAsync(*Args[0], *GetOwner());
		Replays.Add(MoveTemp(Replay));
	}

private:
	inline static FDerivedDataBackendStaticGraph* StaticGraph;

	FAutoConsoleCommand MountPakCommand;
	FAutoConsoleCommand UnmountPakCommand;

	FAutoConsoleCommand LoadReplayCommand;
	TArray<FCacheReplayReader> Replays;
};

} // UE::DerivedData

namespace UE::DerivedData
{

FDerivedDataBackend* FDerivedDataBackend::CreateStatic()
{
	return new FDerivedDataBackendStaticGraph();
}

FDerivedDataBackend& FDerivedDataBackend::GetStatic()
{
	return FDerivedDataBackendStaticGraph::GetStatic();
}

} // UE::DerivedData

namespace UE::DerivedData::Private
{

ICache* TryCreateCache(FStringView GraphNameOrConfig)
{
	return FDerivedDataBackendGraph::TryCreate(GraphNameOrConfig);
}

} // UE::DerivedData::Private

#undef LOCTEXT_NAMESPACE
