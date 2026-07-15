// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Misc/SecureHash.h"
#include "ShaderCodeLibrary.h"

#include "ShaderAuditCore.h"
class FShaderBytecodeDatabase;

// ============================================================================
// Shader Filter
// ============================================================================

/** Fields available for filtering. */
enum class EShaderFilterField : uint8
{
	// Asset-level (from pre-parsed index)
	AssetName,			// filename portion of the asset path
	AssetPath,			// full object path
	AssetClass,			// e.g. "MaterialInstanceConstant", "Material"

	// Shader-level (from FStableShaderKeyAndValue)
	ShaderType,
	VFType,
	Permutation,
	Hash,

	// Derived (from UniqueShaders / BytecodeDatabase)
	RefCount,
	Size,				// uncompressed bytecode size in bytes
};

/** Comparison operators. */
enum class EShaderFilterOp : uint8
{
	Equal,				// ==
	NotEqual,			// !=
	LessThan,			// <
	GreaterThan,		// >
	LessEqual,			// <=
	GreaterEqual,		// >=
	Contains,			// substring match
};

/**
 * A filter expression tree node.
 * Leaf nodes hold a single field/op/value clause.
 * Interior nodes combine children with And/Or/Not.
 */
struct FShaderFilterNode
{
	enum class EType : uint8 { Clause, And, Or, Not };

	EType Type = EType::Clause;

	// Clause data (only when Type == Clause)
	EShaderFilterField Field = EShaderFilterField::AssetName;
	EShaderFilterOp Op = EShaderFilterOp::Equal;
	FString Value;

	// Children (And/Or: 2+, Not: 1)
	TArray<FShaderFilterNode> Children;

	/** Parse a filter expression string. Returns false on syntax error. */
	static bool Parse(const FString& Expression, FShaderFilterNode& OutRoot, FString& OutError);

	/** Evaluate this node against a single shader entry. */
	bool Evaluate(
		int32 Idx,
		const struct FShaderAuditSession& Session) const;
};

/** Tokenize a filter expression into individual tokens (views into the input string). */
TArray<FStringView> TokenizeFilterExpression(const FString& Input);

/** Map a field name string (e.g. "asset.class") to its enum value. */
bool ParseFilterFieldName(FStringView Name, EShaderFilterField& OutField);

/** Map an operator string (e.g. "==") to its enum value. */
bool ParseFilterOp(FStringView OpStr, EShaderFilterOp& OutOp);

/**
 * Build a TBitArray from a list of filter nodes (AND'd together) plus a refcount cap.
 * Entries passing all filters have their bit set to true.
 */
SHADERAUDITCORE_API void BuildVisibleShaders(
	const FShaderAuditSession& Session,
	const TArray<FShaderFilterNode>& Filters,
	int32 MaxRefCount,
	TBitArray<>& OutVisible);

/**
 * Populate filter autocompletion suggestions for a filter expression text box.
 * Suggests field names, operators, values from session data, and logic operators.
 * Session may be null (only field/operator suggestions will be offered).
 */
void GetShaderFilterSuggestions(
	const FString& Text,
	const FShaderAuditSession* Session,
	TArray<FString>& OutSuggestions);

// ============================================================================

/**
 * A loaded SHK session -- the core data unit shared between the editor plugin
 * and the standalone ShaderAuditViewer.
 *
 * Contains the raw SHK data, the analyzed model for the spreadsheet views,
 * and an optional shader database for bytecode metadata.
 */
struct FShaderAuditSession
{
	/** Unique session ID, assigned at construction. */
	int32 SessionId = AllocateSessionId();

	// --- Static session registry (game-thread only) ---

	/** The global session list. Both editor subsystem and standalone viewer use this. */
	SHADERAUDITCORE_API static TArray<TSharedPtr<FShaderAuditSession>>& GetSessions();

	/** Find a session by its unique ID. Returns null if not found. */
	SHADERAUDITCORE_API static TSharedPtr<FShaderAuditSession> FindSession(int32 Id);

	/** Remove a session by ID. Returns true if found and removed. */
	SHADERAUDITCORE_API static bool CloseSession(int32 Id);

private:
	static int32 AllocateSessionId();

public:
	/** The source SHK file path. */
	FString Filename;

	/** Session name parsed from the SHK filename convention (e.g. "PCD3D_SM6"). Empty if filename didn't match the pattern. */
	FString SessionName;

	/** Raw shader entries loaded from the SHK file. */
	TArray<FStableShaderKeyAndValue> StableShaderKeyAndValueArray;

	/** Optional shader database linking OutputHash -> bytecode metadata from .ushaderbytecode files. */
	TSharedPtr<FShaderBytecodeDatabase> BytecodeDatabase;

	// --- Bytecode database public API (hides FShaderBytecodeDatabase from extensions) ---

	/** Returns true if a bytecode database is loaded for this session. */
	SHADERAUDITCORE_API bool HasBytecodeDatabase() const;

	/** Number of .ushaderbytecode archive files loaded. */
	SHADERAUDITCORE_API int32 GetBytecodeArchiveCount() const;

	/** Directory containing the first loaded bytecode archive, or empty. */
	SHADERAUDITCORE_API FString GetBytecodeArchiveDirectory() const;

	/**
	 * Look up per-shader bytecode metadata by OutputHash.
	 * Returns false if no bytecode database is loaded or the hash is unknown.
	 */
	SHADERAUDITCORE_API bool GetShaderBytecodeInfo(const FShaderHash& Hash, uint8& OutFrequency, uint32& OutCompressedSize, uint32& OutUncompressedSize) const;

	/**
	 * Read all compressed shader blobs from disk in archive-sequential order.
	 * Returns the number of blobs read. OutBlobs maps OutputHash -> compressed bytes.
	 * Optional OutArchivesDone atomic is incremented as each archive file completes (for progress).
	 */
	SHADERAUDITCORE_API int32 ReadAllCompressedShaderBlobs(TMap<FShaderHash, TArray<uint8>>& OutBlobs, std::atomic<int32>* OutArchivesDone = nullptr) const;

	// --- Extension data (keyed by module name, see IShaderAuditExtension) ---

	/** Base for extension-owned per-session data. Must be inherited so the virtual destructor cleans up properly. */
	struct FExtensionData
	{
		virtual ~FExtensionData() = default;
	};

	/** Opaque per-extension data attached to this session. Keyed by the owning module's FName. */
	TMap<FName, TSharedPtr<FExtensionData>> ExtensionData;

	/** Retrieve typed extension data. Returns null if not set. */
	template<typename T>
	T* GetExtensionData(FName Key) const
	{
		const TSharedPtr<FExtensionData>* Found = ExtensionData.Find(Key);
		return Found ? static_cast<T*>(Found->Get()) : nullptr;
	}

	/** Store typed extension data. Replaces any previous value for this key. */
	template<typename T>
	void SetExtensionData(FName Key, TSharedPtr<T> Data)
	{
		ExtensionData.Add(Key, StaticCastSharedPtr<FExtensionData>(Data));
	}

	// --- Material hierarchy (populated by LoadAssetRegistryDeps or editor connection) ---


	/** Per-entry: which material index this SHK entry belongs to. */
	TArray<int32> EntryMaterialIndex;

	// --- Unique material table ---

	static void InitFromFullName(const FString& FullName, FString& OutPath, FString& OutClassName);

	struct FUniqueMaterial
	{
		FString Path;
		FString ClassName;
		int32 PackageIndex = INDEX_NONE; // -> MaterialPackages[], INDEX_NONE if unresolved
		TArray<int32> ShaderIndices; // SHK entry indices for this material
	};

	TArray<FUniqueMaterial> UniqueMaterials;

	// --- Material package hierarchy (populated by SetupMaterialParents) ---

	struct FMaterialPackage
	{
		FString PackagePath;
		int32 ParentIndex = INDEX_NONE; // -> MaterialPackages[], INDEX_NONE for roots
	};

	TArray<FMaterialPackage> MaterialPackages;

	int32 NumMaterialParents = 0;

	/** Per-unique-shader data. Index = unique shader ID. */
	struct FUniqueShader
	{
		int32 FirstEntryIdx = INDEX_NONE;       // -> StableShaderKeyAndValueArray
		TArray<int32> MaterialIndices;           // which materials reference this shader
		uint32 CompressedSize = 0;               // on-disk size from BytecodeDatabase
		uint32 UncompressedSize = 0;             // decompressed size from BytecodeDatabase
		uint16 ArchiveCount = 0;				 // number of .ushaderbytecode archives containing this shader
	};

	TArray<FUniqueShader> UniqueShaders;

	/** OutputHash -> unique shader ID. The one hash->index map. */
	TMap<FShaderHash, int32> ShaderHashToIndex;

	/** Access the SHK entry for a unique shader. */
	const FStableShaderKeyAndValue& GetShaderEntry(int32 UniqueShaderID) const
	{
		check(UniqueShaders.IsValidIndex(UniqueShaderID));
		check(StableShaderKeyAndValueArray.IsValidIndex(UniqueShaders[UniqueShaderID].FirstEntryIdx));
		return StableShaderKeyAndValueArray[UniqueShaders[UniqueShaderID].FirstEntryIdx];
	}

	/** Number of distinct materials that reference this hash. */
	int32 GetHashRefCount(const FShaderHash& Hash) const
	{
		const int32* Id = ShaderHashToIndex.Find(Hash);
		if (!Id) { return 0; }
		check(UniqueShaders.IsValidIndex(*Id));
		return UniqueShaders[*Id].MaterialIndices.Num();
	}

	/** Build the indexes from StableShaderKeyAndValueArray. Called once after loading. */
	SHADERAUDITCORE_API void BuildIndex();

	/** Has a material parent map been populated (from any source)? */
	bool HasMaterialHierarchy() const { return NumMaterialParents > 0; }

	SHADERAUDITCORE_API void SetupMaterialParents(const TMap<FString, FString>& ParentMap);

	/**
	 * Detect sub-object materials produced by FCompactFullName::AppendString flattening.
	 * Returns true if Path has consecutive duplicate path components (e.g. MI_Name/MI_Name/),
	 * which indicates the Package/Object boundary with sub-objects after it.
	 *
	 * @param OutOuterPackage   The outer asset's package path (e.g. "/Game/Wraps/MI_Wrap_Denim")
	 * @param OutOuterLeafName  The outer asset's leaf name (e.g. "MI_Wrap_Denim")
	 * @param OutSubObjectLeaf  The sub-object leaf name after the last '.' (e.g. "CharacterOpaque")
	 */
	SHADERAUDITCORE_API static bool DetectSubObject(const FString& Path, FString& OutOuterPackage, FString& OutOuterLeafName, FString& OutSubObjectLeaf);

	/** Build a hierarchy path string for a material by walking its parent chain. */
	SHADERAUDITCORE_API FString BuildHierarchyPath(int32 MaterialIndex) const;

	/**
	 * Load from a pre-gathered inventory -- the primary loading entry point.
	 * Handles everything: caching (if network paths), parallel SHK loading,
	 * shader archive import (from Inventory.BytecodeFiles), index building.
	 * No directory scanning is performed -- all file paths come from the inventory.
	 */
	SHADERAUDITCORE_API static TArray<TSharedPtr<FShaderAuditSession>> LoadFromInventory(const struct FSessionFileInventory& Inventory);
};

// ============================================================================
// Session File Caching (NAS -> local)
// ============================================================================

/** Returns true if the given path resides on a network/remote drive. */
bool IsNetworkPath(const FString& Path);

/**
 * Inventory of all files associated with a session
 * Used to present caching options to the user before copying.
 */
struct FSessionFileInventory
{
	/** Parsed session name (e.g. "PCD3D_SM6"). */
	FString SessionName;

	/**
	 * Relative cache subdirectory, constructed by Gather().
	 * e.g. "{Branch}-CL-{CL}/{TargetType}/Metadata"
	 * Used by ShowCacheSessionDialog to build the full cache path.
	 * Empty if the build hierarchy could not be determined.
	 */
	FString CacheSubDir;

	/** SHK files (full paths). */
	TArray<FString> SHKFiles;
	int64 SHKTotalBytes = 0;

	/** Shader bytecode archives in ../ShaderLibrarySource/ (full paths). */
	TArray<FString> BytecodeFiles;
	int64 BytecodeTotalBytes = -1; // -1 = never attempted, 0 = attempted but none found, >0 = has bytecode

	/** Per-file stat data (full path -> FFileStatData). Populated during scan and Gather(). */
	TMap<FString, FFileStatData> FileStats;

	/**
	 * Optional source->dest filename remapping for caching.
	 * When manifest files need to be cached as ShaderStableInfo-* names.
	 * Key = source path (in SHKFiles), Value = desired dest filename (just the filename, not path).
	 * If a source path is not in this map, CacheSessionFiles uses the source filename as-is.
	 */
	TMap<FString, FString> DestFilenames;

	/** Most recent modification time across all files in FileStats. */
	FDateTime GetMostRecentTime() const
	{
		FDateTime Best;
		for (const auto& Pair : FileStats)
		{
			if (Pair.Value.ModificationTime > Best) { Best = Pair.Value.ModificationTime; }
		}
		return Best;
	}
	
	/**
	 * Discover all session-related files from a list of SHK paths.
	 * The first path is used to derive the session name and directory.
	 * All paths are used as the SHK file list (stat'd for sizes unless pre-computed).
	 * Bytecode files are discovered relative to the derived build metadata
	 * directory (BuildRoot + CacheSubDir for manifest paths, or by walking
	 * up the directory hierarchy for non-manifest paths).
	 *
	 * For manifest paths (NAS naming convention), resolves to real build
	 * paths when the build directory exists, or uses them directly otherwise.
	 *
	 * @param FilesToLoad       SHK file paths (full sibling list).
	 * @param bShowProgress     If true, show a progress dialog.
	 * @param PrecomputedStats Optional map of path->stat data. Skips stat for paths found here.
	 */
	static FSessionFileInventory Gather(
		const TArrayView<const FString>& FilesToLoad,
		bool bShowProgress = true,
		const TMap<FString, FFileStatData>* PrecomputedStats = nullptr);

	/**
	 * Find all SHK siblings of a given SHK file.
	 * Enumerates the directory for files matching the same session name suffix.
	 * E.g. for ShaderStableInfo-Game-PCD3D_SM6.shk, finds all *-PCD3D_SM6.shk siblings.
	 * Returns the full list including the input file itself.
	 * If the session name cannot be parsed, returns just the input path.
	 *
	 * @param bShowProgress  If true, show a progress dialog during directory enumeration (useful for NAS paths).
	 */
	static TArray<FString> FindSHKSiblings(const FString& SHKPath, bool bShowProgress = false);
};

/** Which file categories to include when caching. */
struct FSessionCacheOptions
{
	bool bIncludeSHK = true;
	bool bIncludeBytecode = false;
};

/**
 * Copy selected session files from the inventory to a local directory.
 * Shows progress via FScopedProgressTask. Files already present with matching
 * size are skipped.
 *
 * @param Inventory       Gathered file inventory
 * @param DestDir         Local directory to copy into (preserves subfolder structure)
 * @param Options         Which categories to copy
 * @return Paths to the local copies (same structure as inventory, but local)
 */
FSessionFileInventory CacheSessionFiles(
	const FSessionFileInventory& Inventory,
	const FString& DestDir,
	const FSessionCacheOptions& Options);

/**
 * Show a modal dialog letting the user choose which file categories to cache
 * and where to cache them. Returns true if the user clicked Cache, false on Cancel.
 *
 * @param Inventory       Gathered file inventory (displayed to the user)
 * @param OutOptions      Populated with the user's category selections
 * @param OutCacheDir     Populated with the chosen cache directory
 */
bool ShowCacheSessionDialog(
	const FSessionFileInventory& Inventory,
	FSessionCacheOptions& OutOptions,
	FString& OutCacheDir);

/**
 * Delete cached shader files (.shk, .ushaderbytecode) under the given directory.
 * Removes only known file types, then prunes empty directories bottom-up.
 * Can be used to clear a specific CL dir or the entire cache root.
 * @return true if any files were deleted.
 */
SHADERAUDITCORE_API bool ClearCachedSessionDir(const FString& DirToDelete);