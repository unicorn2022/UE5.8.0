// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Misc/Crc.h"
#include "UObject/NameTypes.h"
#include "GameFeatureVersePathMapperCommandlet.generated.h"

class FArchive;
class FAssetRegistryState;

USTRUCT()
struct FGameFeaturePluginInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString GfpUri;

	UPROPERTY()
	TArray<FName> Dependencies;

	UPROPERTY()
	TArray<FName> ImplicitDependencies;

	/** Serialize natively */
	bool Serialize(FArchive& Ar);

	friend FArchive& operator<<(FArchive& Ar, FGameFeaturePluginInfo& GameFeaturePluginInfo)
	{
		GameFeaturePluginInfo.Serialize(Ar);
		return Ar;
	}
};

namespace GameFeatureVersePathMapper
{
	/** Case sensitive hashing function for TMap */
	template <typename ValueType>
	struct FCaseSensitiveKeyMapFuncs : BaseKeyFuncs<ValueType, FString, /*bInAllowDuplicateKeys*/false>
	{
		static inline const FString& GetSetKey(const TPair<FString, ValueType>& Element)
		{
			return Element.Key;
		}
		static inline bool Matches(const FString& A, const FString& B)
		{
			return A.Equals(B, ESearchCase::CaseSensitive);
		}
		static inline uint32 GetKeyHash(const FString& Key)
		{
			return FCrc::StrCrc32(*Key);
		}
	};

	struct FGameFeatureVersePathLookup
	{
		TMap<FString, FName, FDefaultSetAllocator, GameFeatureVersePathMapper::FCaseSensitiveKeyMapFuncs<FName>> VersePathToGfpMap;
		TMap<FName, FGameFeaturePluginInfo> GfpInfoMap;
	};

	struct FDLCInfo
	{
		FString DLCName;
		FString InstallBundleName;
		TArray<FString> Plugins;
	};

	GAMEFEATURES_API TArray<FDLCInfo> FindGFPToDLC(const ITargetPlatform* TargetPlatform);

	GAMEFEATURES_API FString GetVerseAppDomain();
	GAMEFEATURES_API FString GetAltVerseAppDomain();

	/**
	 * Finds plugin dependencies and returns them in dependency order (reverse topological sort order)
	 */
	class FDepthFirstGameFeatureSorter
	{
	private:
		enum class EVisitState : uint8
		{
			None,
			Visiting,
			Visited
		};

		const TMap<FName, FGameFeaturePluginInfo>& GfpInfoMap;
		TMap<FName, EVisitState> VisitedPlugins;

		bool bIncludeVirtualNodes = false;

		bool Visit(FName Plugin, TFunctionRef<void(FName, const FString&)> AddOutput);

	public:
		/**
		 * Constructor
		 * @Param InGfpInfoMap Map containing plugin dependencies (FDepthFirstGameFeatureSorter points to this map, it is not copied)
		 */
		FDepthFirstGameFeatureSorter(const TMap<FName, FGameFeaturePluginInfo>& InGfpInfoMap, bool bInIncludeVirtualNodes = false) 
			: GfpInfoMap(InGfpInfoMap) 
			, bIncludeVirtualNodes(bInIncludeVirtualNodes)
		{}

		// @TODO: Allow passing a callback to fetch dependencies?

		/**
		 * Find and sort all dependencies
		 * @Param GetNextRootPlugin function that iterates plugins in the root set, returning None after the final plugin in the set.
		 * @Param AddOutput callback to receive roots and dependencies, called in dependency order
		 * @Return false if there is an error or a cyclic dependency is discovered
		 */
		GAMEFEATURES_API bool Sort(TFunctionRef<FName()> GetNextRootPlugin, TFunctionRef<void(FName, const FString&)> AddOutput);

		/**
		 * Find and sort all dependencies
		 * @Param RootPlugins set of root plugins
		 * @Param AddOutput callback to receive roots and dependencies, called in dependency order
		 * @Return false if there is an error or a cyclic dependency is discovered
		 */
		GAMEFEATURES_API bool Sort(TConstArrayView<FName> RootPlugins, TFunctionRef<void(FName, const FString&)> AddOutput);
		bool Sort(FName RootPlugin, TFunctionRef<void(FName, const FString&)> AddOutput) { return Sort(MakeArrayView(&RootPlugin, 1), AddOutput); }

		/**
		 * Find and sort all dependencies
		 * @Param RootPlugins set of root plugins
		 * @Param OutPlugins roots and dependencies in dependency order
		 * @Return false if there is an error or a cyclic dependency is discovered
		 */
		GAMEFEATURES_API bool Sort(TConstArrayView<FName> RootPlugins, TArray<FName>& OutPlugins);
		bool Sort(FName RootPlugin, TArray<FName>& OutPlugins) { return Sort(MakeArrayView(&RootPlugin, 1), OutPlugins); }
	};

	enum class EBuildLookupOptions : uint32
	{
		None = 0,
		OnlyBaseBuildPlugins = 1 << 0, // Only include GFPs that are intrinsic to the current target
		WithDynamicCookPlugins = 1 << 1, // Include GFPs with CookBehavior.Type=ContentWorker
	};
	ENUM_CLASS_FLAGS(EBuildLookupOptions)

	/**
	 * Build a FGameFeatureVersePathLookup that can be used to map Verse paths to Game Feature URIs
	 * @Param TargetPlatform If set, uses the corresponding platform config
	 * @Param DevAR If set, use the specified development asset registry state instead of the global asset registry
	 * @Param ReferencedPackageNames With DevAR: if non-null, only enumerate UGameFeatureData whose package is in this set (from ReferencedSet.txt)
	 * @Return false if there is an error or a cyclic dependency is discovered
	 */
	GAMEFEATURES_API TOptional<FGameFeatureVersePathLookup> BuildLookup(
		const ITargetPlatform* TargetPlatform = nullptr,
		const FAssetRegistryState* DevAR = nullptr,
		EBuildLookupOptions Options = EBuildLookupOptions::None,
		const TSet<FName>* ReferencedPackageNames = nullptr);

	/**
	 * Returns the path where GameFeatureVersePaths.bin is written (commandlet) or read (runtime).
	 * Pass the target platform name at cook time to get Saved/Cooked/<Platform>/<Project>/GameFeatureVersePaths.bin.
	 * Pass empty (default) at runtime to get FPaths::ProjectDir()/GameFeatureVersePaths.bin (same level as AssetRegistry.bin).
	 */
	GAMEFEATURES_API FString GetVerseLookupBinaryPath(FStringView PlatformName = FStringView());

	/**
	 * Binary serialize or deserialize a FGameFeatureVersePathLookup.
	 * Direction is determined by Ar.IsLoading(). Returns false on magic/version mismatch or archive error.
	 */
	GAMEFEATURES_API bool SerializeLookupBinary(FArchive& Ar, FGameFeatureVersePathLookup& Lookup);
}

UCLASS(config = Editor)
class UGameFeatureVersePathMapperCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& CmdLineParams) override;

	GAMEFEATURES_API static FString GetGameFeatureRootVersePath();
};
