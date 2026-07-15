// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// Experimental - DO NOT USE

#include "AutoRTFM/Defines.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Containers/StringView.h"
#include "CoreTypes.h"
#include "Delegates/Delegate.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/TransactionallySafeRWLock.h"
#include "Templates/Function.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVersePathRegistry, Log, All);

class FSolarisIde;
struct FVersePathVisitor;
class FSolarisIde;
class FVersePathRegistryTestsBase;

namespace UE::VersePathRegistry
{
class FRegistryBuilder;

enum class UE_INTERNAL EAccess : uint8
{
	EpicInternal = 0,
	Scoped,
	Private,
	Protected,
	Internal,
	Public,

	Count
};
ASSETREGISTRY_API const TCHAR* LexToString(EAccess Access);

enum class UE_INTERNAL EPathKind : uint8
{
	Unknown = 0,
	Module,
	Interface,
	Class,
	Function,
	Data,
	Alias,

	Count
};
ASSETREGISTRY_API const TCHAR* LexToString(EPathKind Kind);

enum class UE_INTERNAL EFlags : uint8
{
	None		= 0,
	Var			= 1 << 0,
	Abstract	= 1 << 1,

	Count = 3
};
ENUM_CLASS_FLAGS(EFlags);
ASSETREGISTRY_API const TCHAR* LexToString(EFlags Flags);

// To consider: We can likely remove this / convert it into an interned FVersePath type
struct UE_INTERNAL FPathHandle
{
	// Todo: Pack path type in the generation
	static constexpr uint32 GenerationMask = ~0u;
	ASSETREGISTRY_API static const FPathHandle Null;

	uint32 Index = 0;
	uint32 Generation = 0;

	[[nodiscard]] bool UEOpEquals(const FPathHandle& Rhs) const
	{
		return Index == Rhs.Index && Generation == Rhs.Generation;
	}
	[[nodiscard]] bool UEOpLessThan(const FPathHandle& Rhs) const
	{ 
		return (Index != Rhs.Index) ? Index < Rhs.Index : Generation < Rhs.Generation;
	}

	[[nodiscard]] friend uint32 GetTypeHash(FPathHandle Handle)
	{
		return HashCombine(GetTypeHash(Handle.Index), GetTypeHash(Handle.Generation));
	}

	[[nodiscard]] bool IsNull() const;
};

// to consider: Partition this data into separate arrays based on Kind[Index] or embed the kind in the handle
// to consider: If we don't use multiple tables, we can union more of the data here but we will need to handle default constructability for cases like the TArrays
// to consider: Since handles <-> pathdata are immutable we can likely embed a decent bit of info into the handle and avoid PathDatas queries
struct UE_INTERNAL FPathData
{
	FString Path; // Move to interned handle
	TArray<FPathHandle> ReadScopeAccessPaths;
	TArray<FPathHandle> WriteScopeAccessPaths;
	TArray<FPathHandle> ConstructorScopeAccessPaths;
	TArray<FPathHandle> Interfaces;
	FPathHandle Parent = FPathHandle::Null;
	union
	{
		FPathHandle ClassSuper = FPathHandle::Null;
		FPathHandle TypeAliasTarget;
	};
	EPathKind Kind = EPathKind::Unknown;
	EAccess ReadAccess = EAccess::Private;
	EAccess WriteAccess = EAccess::Private;
	EAccess ConstructorAccess = EAccess::Private;
	EFlags Flags = EFlags::None;

	[[nodiscard]] bool IsVar() const;
	[[nodiscard]] bool IsAbstract() const;
private:
	friend class FVersePathRegistry;
	uint32 Generation = 0;
};

struct UE_INTERNAL FRegistryEvents
{
	// Maps the old handle removed from the registry to the path string the old handle was registered to
	TMap<FPathHandle, FString> RemovedPaths;

	// Holds all new handles added to the registry. The added path strings can be looked up in the registry using these handles.
	TArray<FPathHandle> AddedPaths;

	// Maps the old handles removed from the registry that have been replaced by a new handle. 
	// The path strings for the updated paths can be looked up in the registry using the new handles (i.e. Pair.Value).
	// This map contains the parent chain of paths to the path that was updated. 
	// e.g. When updating /Module/Class/Data: /Module, /Module/Class and /Module/Class/Data will be marked as updated.
	// This map does not include paths whose FPathData FPathHandle members have changed.
	TMap<FPathHandle, FPathHandle> UpdatedPaths; 

	[[nodiscard]] ASSETREGISTRY_API int32 Num() const;
	ASSETREGISTRY_API void Reset();
};
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVersePathRegistryUpdated, const FRegistryEvents&);

// Todo: determine an appropriate name for this registry / where it lives
/**
* Global registry holding type information for all compiled verse paths.
* The registry can be used to determine if a path can be read/written/constructed (if appropriate)
* when given a context path in which such an operation would be performed. 
* 
* e.g. CanRead("/Verse.org/ModuleA", "/Verse.org/ModuleB/InternalClass") can return false indicating that 
* it would be invalid to access an internal class from another module (as long as ModuleB and InternalClass
* haven't provided scoped access to ModuleA -- something this registry would also track and account for).
* 
* The Registry is almost entirely read-only as it is not expected to change often. A FRegistryBuilder can be used 
* under special circumstances (i.e. verse compilation) to modify the global registry. Upon load of compiled verse
* packages, or the compilation of source verse packages, the registry will be populated with the path information 
* for those packages.
*/
class UE_INTERNAL FVersePathRegistry
{
	ASSETREGISTRY_API FVersePathRegistry();

public:
	FVersePathRegistry(const FVersePathRegistry&) = delete;
	FVersePathRegistry(FVersePathRegistry&&) = delete;
	FVersePathRegistry& operator=(const FVersePathRegistry&) = delete;
	FVersePathRegistry& operator=(FVersePathRegistry&&) = delete;

	/** Returns singleton instance to the global registry. */
	ASSETREGISTRY_API static FVersePathRegistry& Get();
	ASSETREGISTRY_API ~FVersePathRegistry();

	/**
	 * Broadcasts the changes to the registry after each call to FVersePathRegistry::Append().
	 * Listeners can safely read from the registry during a broadcast, but write operations will deadlock.
	 */
	[[nodiscard]] FOnVersePathRegistryUpdated& OnRegistryUpdated();

	/**
	* Determines if the type at the target path is constructable from within the context path.
	* If either the context or target are not registered, false is returned.
	*/
	[[nodiscard]] ASSETREGISTRY_API bool CanConstruct(const FStringView ContextVersePath, const FStringView TargetVersePath) const;

	/**
	* Determines if the type at the target path is constructable from within the context path.
	* If either the context or target handles are invalid, false is returned.
	*/
	[[nodiscard]] ASSETREGISTRY_API bool CanConstruct(const FPathHandle ContextHandle, const FPathHandle TargetHandle) const;

	/**
	* Determines if the target path is readable from the context path.
	* If either the context or target are not registered, false is returned.
	*/
	[[nodiscard]] ASSETREGISTRY_API bool CanRead(const FStringView ContextVersePath, const FStringView TargetVersePath) const;

	/**
	* Determines if the target path is readable from the context path.
	* If either the context or target handles are invalid, false is returned.
	*/
	[[nodiscard]] ASSETREGISTRY_API bool CanRead(const FPathHandle ContextHandle, const FPathHandle TargetHandle) const;

	/** 
	* Determines if the target path is writable from the context path. 
	* If either the context or target are not registered, false is returned.
	*/
	[[nodiscard]] ASSETREGISTRY_API bool CanWrite(const FStringView ContextVersePath, const FStringView TargetVersePath) const;

	/**
	* Determines if the target path is writable from the context path.
	* If either the context or target handles are invalid, false is returned.
	*/
	[[nodiscard]] ASSETREGISTRY_API bool CanWrite(const FPathHandle ContextHandle, const FPathHandle TargetHandle) const;

	/** Returns a copy of the registered FPathData for the provided path. If the path is not registered, false is returned. */
	ASSETREGISTRY_API bool TryGetPathData(const FStringView VersePath, FPathData& OutPathData) const;

	/** Returns a copy of the registered FPathData for the provided handle. If the handle is not valid, false is returned. */
	ASSETREGISTRY_API bool TryGetPathData(const FPathHandle Handle, FPathData& OutPathData) const;

	/** Returns handle for the provided verse path. Otherwise FPathHandle::Null is returned. */
	[[nodiscard]] ASSETREGISTRY_API FPathHandle FindPath(const FStringView VersePath) const;

	/** Returns whether the provided handle is valid for use in the registry. */
	[[nodiscard]] bool IsValidHandle(FPathHandle Handle) const;

	/** Returns the number of paths registered with the registry. */
	[[nodiscard]] int32 Num() const;

	/** 
	* Logs the string representation of the provided path in the registry. If no path is provided,
	* all paths are dumped. Provided paths can optionally specify if child paths should also be dumped (default, true).
	* An archive can be provided to write the results to the archive rather than to the console.
	*/
	ASSETREGISTRY_API void Dump(const FStringView DumpPath = {}, bool bPrintChildren = true, TUniquePtr<FArchive> Ar = nullptr) const;

	/** Removes all registered paths */
	ASSETREGISTRY_API void Empty();

	/**
	 * Invokes Visitor(FPathHandle, const FPathData&) for every path currently registered in the registry.
	 * The visitor is called under a read lock, so it must not perform any write operations on the registry.
	 */
	ASSETREGISTRY_API void ForEachPath(TFunctionRef<void(FPathHandle, const FPathData&)> Visitor) const;

private:
	friend class ::FSolarisIde;
	friend struct ::FVersePathVisitor;
	friend class FRegistryBuilder;
	friend class ::FVersePathRegistryTestsBase;
	
	// The registry is meant to be a reflection of what is compiled and should only be populated from certain sources.
	// Start registry writing methods
	/////////////////////////////////
	
	// Appends the paths from the provided builder into the registry. Paths in the builder that are in the
	// registry already are overwritten, causing all paths under the overwritten path to be removed. The builder can 
	// append to existing nodes in the registry by referring to those paths via FPathHandle directly. Overwritten paths are 
	// marked as UpdatedPaths in the FRegistryEvents if provided. Similarly, any removed paths or newly added paths are recorded 
	// in the FRegistryEvents as well.
	ASSETREGISTRY_API void Append(FRegistryBuilder& Builder, FRegistryEvents* OutEvents = nullptr);

	/** Finds or adds a handle for the provided verse path. */
	ASSETREGISTRY_API FPathHandle FindOrAddPath(const FStringView Path, bool* bAlreadyExisted = nullptr);
	// End registry writing methods
	/////////////////////////////////
	

	/** Normalizes provided paths to ensure they meet the expectations of the registry's path to path handle mapping. */
	template<typename CharType>
	void NormalizeVersePath(TStringBuilderBase<CharType>& Builder, const FStringView VersePath) const;

	// Methods below are expected to have thread safety handled by the caller
	//////////////////////////////////////////////////////////////////////////

	/** Converts verse paths to their FPathHandle index if the paths are valid. If either path is invalid, returns false, otherwise true. */
	const bool TryGetContextAndTargetIndicesNoLock(const FStringView ContextVersePath, const FStringView TargetVersePath,
		uint32& OutContextIndex, uint32& OutTargetIndex) const;


	// Start accessibility methods
	//////////////////////////////
	enum class EAccessType
	{
		Read,		// Read Access. By default, all fields are immutable in Verse unless marked as IsVar
		Write,		// Write access. Only applicable to Targets that are IsVar()
		Construct	// Type construction access. Only applicable to Targets that are EPathKind::Class
	};
	[[nodiscard]] bool CanAccessNoLock(const uint32 ContextIndex, const uint32 TargetIndex, EAccessType AccessType) const;

	/** Is the target module the same or a child of the context module (e.g. is target /A/B/C a child of context /A/B). */
	[[nodiscard]] bool IsModuleSameOrChildOfNoLock(const uint32 ContextIndex, const uint32 TargetIndex) const;

	/** Walk up the parent chain looking for the nearest module. */
	[[nodiscard]] uint32 GetModuleNoLock(const uint32 Index) const;

	/** Walk up the parent chain looking for the nearest class or interface */
	[[nodiscard]] uint32 GetScopeClassOrInterfaceNoLock(const uint32 Index) const;

	/** 
	* Return the index for the path that represents the logical scope for Index.
	* If Index points to a path that is already a logical scope, Index is returned, otherwise the parent scope is returned.
	*/
	[[nodiscard]] uint32 GetLogicalScopeNoLock(const uint32 Index) const;

	/** Is Context the same class or a child class of Target. */
	[[nodiscard]] bool IsClassNoLock(uint32 ContextIndex, uint32 TargetIndex) const;

	/** Is Context the same interface or a child interface of Target. */
	[[nodiscard]] bool IsInterfaceNoLock(uint32 ContextIndex, uint32 TargetIndex) const;

	/** Does path at ContextIndex implement the interface at TargetIndex. */
	[[nodiscard]] bool IsInterfaceImplementorNoLock(uint32 ContextIndex, uint32 TargetIndex) const;
	// End accessibility methods
	////////////////////////////


	/** Returns whether the provided handle is valid for use in the registry. */
	[[nodiscard]] bool IsValidHandleNoLock(FPathHandle Handle) const;

	/** Shrinks the registry's internal storage to the smallest storage required for the elements being stored. */
	void ShrinkNoLock();

	/** Allocates a FPathHandle. Thread-safety must be handled by the caller. */
	FPathHandle AllocateHandleNoLock();

	/** Frees an allocated FPathHandle for recycling. Thread-safety must be handled by the caller. */
	void FreeHandleNoLock(const uint32 Index);

	/** Returns the handle for the provided verse path. Thread-safety must be handled by the caller. */
	[[nodiscard]] FPathHandle FindPathNoLock(const FStringView VersePath) const;

	/** When adding paths from a builder, any internal references via path string are set to the equivalent handle reference from the registry. */
	void FixupAddedPathInternalReferencesNoLock(const FRegistryBuilder& Builder, FPathHandle AddedHandle);

	/** After manipulating the registry, call this method to correct internal FPathHandle references that may have become stale. */
	void FixupInternalReferencesNoLock(const uint32 IndexToFixup, const TMap<FPathHandle, FPathHandle>& UpdatedHandles);

	// Adds the path in the builder at the BuilderRootIndex and all paths in the builder that have a parent chain that leads back to BuilderRootIndex
	// to the registry. If provided, all new paths are added to the OutEvents.AddedPaths list, and paths that were removed knowing they will later be 
	// added are added as the pair key in the PathToUpdateHandles map. Returns the index of the newly added root of the subtree (which is not necessarily a registry root)
	uint32 AddSubtreeNoLock(const FRegistryBuilder& Builder, const FSetElementId BuilderParentId,
		TMap<FString, TPair<FPathHandle, FPathHandle>>& PathToUpdatedHandles, TArray<FPathHandle>& AddedHandles);

	/**
	* Removes the path at index Parent and all paths that have a FPathData.Parent chain that leads back to Parent from the registry. 
	* If provided, removed nodes will be appended to the OutEvents.RemovedPaths list, and the old handle for nodes that 
	* we know will be removed and later replaced will be added as the pair key in the PathToUpdateHandles map.
	* Returns true when the Parent was removed.
	*/
	bool RemoveSubtreeNoLock(const uint32 Parent, TMap<FString, TPair<FPathHandle, FPathHandle>>& PathToUpdatedHandles, TMap<FPathHandle, FString>& RemovedHandlesToPath);

	/**
	* Collects all modules and all roots of non-module sub-trees (e.g. a class but not its data members).
	*/
	void CollectPackageRoots(const FRegistryBuilder& Builder, TSet<uint32>& OutPackageRoots);


	// Consider: we can partition subtrees to avoid contention if necessary
	mutable FTransactionallySafeRWLock Lock;

	FOnVersePathRegistryUpdated OnRegistryUpdatedEvent;

	// Todo: Replace with FString with proper interned FVersePath type
	TMap<FString, uint32> PathToPathDataIndex;
	TArray<FPathData> PathDatas;
	TArray<uint32> FreeList;

	// Hierarchy maintenance
	TSet<uint32> Roots;
	TMap<uint32, TSet<uint32>> ParentToChildren;

	// Used for package maintenance
	TMap<FString, int32> ModuleToRefCount;
	TMap<FString, TSet<uint32>> PackageToRoots;
};

/**
* Builder allowing for a FVersePathRegistry to be constructed for later appending to the global registry.
*/
class UE_INTERNAL FRegistryBuilder
{
public:
	struct FPathDesc
	{
		FString Path;
		FString Parent;
		EPathKind Kind = EPathKind::Unknown;
		EAccess ReadAccess = EAccess::Private;
		EAccess WriteAccess = EAccess::Private;
		EAccess ConstructorAccess = EAccess::Private;
		TArray<FString> ReadScopeAccessPaths;
		TArray<FString> WriteScopeAccessPaths;
		TArray<FString> ConstructorScopeAccessPaths;
		FString ClassSuper;
		FString TypeAliasTarget;
		TArray<FString> Interfaces;
		EFlags Flags;
	};

	FRegistryBuilder() = delete;
	ASSETREGISTRY_API explicit FRegistryBuilder(const FStringView InPackageName);

	/** Registers a path description with the builder. Overwrites any previously registered description with the same path.*/
	ASSETREGISTRY_API bool RegisterPath(const FPathDesc& PathDesc);

	/** Returns true and populates OutPathDesc if the provided verse path has been registered. */
	bool Find(const FStringView VersePath, FPathDesc& OutPathDesc) const;

	/** Returns the FPathDesc for the provided verse path. The path must exist. */
	[[nodiscard]] const FPathDesc& FindChecked(const FStringView VersePath) const;

	/** Returns true if the provided verse path has been registered with the builder. Returns false otherwise. */
	[[nodiscard]] bool Contains(const FStringView VersePath) const;

	void Reserve(const int32 Num);

	/** Returns the number of paths registered with the builder. */
	[[nodiscard]] int32 Num() const;

	/** Removes all registered paths from the builder */
	void Empty();

	/** Returns the package name of the registry builder, indicating the source package used to generate the builder paths. */
	[[nodiscard]] const FString& GetPackageName() const;

	/** Sets the package name of the registry builder, indicating the source package used to generate the builder paths. */
	void SetPackageName(const FString& InPackageName);

private:
	bool ValidatePath(const FPathDesc& PathDesc);
	void BuildHierarchy();

	friend class ::FSolarisIde;
	friend struct ::FVersePathVisitor;
	friend class FVersePathRegistry;
	friend class ::FVersePathRegistryTestsBase;
	
	struct FPathDescKeyFuncs : BaseKeyFuncs<FPathDesc, FString, /*bInAllowDuplicateKeys*/false>
	{
		static const FString& GetSetKey(const FPathDesc& Element)
		{
			return Element.Path;
		}

		static bool Matches(const FString& A, const FString& B)
		{
			return A.Equals(B, ESearchCase::CaseSensitive);
		}

		static bool Matches(const FString& A, const FStringView B)
		{
			return B.Equals(A, ESearchCase::CaseSensitive);
		}

		static uint32 GetKeyHash(const FString& Key)
		{
			return GetTypeHash(Key);
		}

		static uint32 GetKeyHash(const FStringView Key)
		{
			return GetTypeHash(Key);
		}
	};

	struct FSetElementIdFuncs : BaseKeyFuncs<FSetElementId, FSetElementId, /*bInAllowDuplicateKeys*/false>
	{
		static FSetElementId GetSetKey(FSetElementId Element)
		{
			return Element;
		}

		static bool Matches(FSetElementId A, FSetElementId B)
		{
			return A == B;
		}

		static uint32 GetKeyHash(FSetElementId Key)
		{
			return GetTypeHash(Key.AsInteger());
		}
	};

	struct FSetElementIdMapFuncs : BaseKeyFuncs<TPair<FSetElementId, TSet<FSetElementId, FSetElementIdFuncs>>, FSetElementId, /*bInAllowDuplicateKeys*/false>
	{
		static FSetElementId GetSetKey(TPair<FSetElementId, TSet<FSetElementId, FSetElementIdFuncs>> Element)
		{
			return Element.Key;
		}

		static bool Matches(FSetElementId A, FSetElementId B)
		{
			return A == B;
		}

		static uint32 GetKeyHash(FSetElementId Key)
		{
			return GetTypeHash(Key.AsInteger());
		}
	};

	FString PackageName;
	TSet<FPathDesc, FPathDescKeyFuncs> PathDescs;
	TSet<FSetElementId, FSetElementIdFuncs> Roots;
	TMap<FSetElementId, TSet<FSetElementId, FSetElementIdFuncs>, FDefaultSetAllocator, FSetElementIdMapFuncs> ParentToChildren;
	bool bRebuildHierarchy = false;
};

///////////////////////////////////////////////////////
// Inline implementations
///////////////////////////////////////////////////////
inline bool FPathHandle::IsNull() const
{
	return Index == Null.Index && (Generation & GenerationMask) == Null.Generation;
}

inline bool FPathData::IsVar() const
{
	return EnumHasAnyFlags(Flags, EFlags::Var);
}

inline bool FPathData::IsAbstract() const
{
	return EnumHasAnyFlags(Flags, EFlags::Abstract);
}

inline FOnVersePathRegistryUpdated& FVersePathRegistry::OnRegistryUpdated()
{
	return OnRegistryUpdatedEvent;
}

inline int32 FVersePathRegistry::Num() const
{
	UE::TReadScopeLock _(Lock);
	return PathToPathDataIndex.Num();
}

inline FPathHandle FVersePathRegistry::FindPathNoLock(const FStringView VersePath) const
{
	const uint32 Hash = GetTypeHash(VersePath);
	const uint32* Index = PathToPathDataIndex.FindByHash(Hash, VersePath);
	if (Index)
	{
		return { *Index, PathDatas[*Index].Generation};
	}
	return FPathHandle::Null;
}

inline void FVersePathRegistry::ShrinkNoLock()
{
	PathToPathDataIndex.Shrink();
	PathDatas.Shrink();
	Roots.Shrink();
	FreeList.Shrink();
}

inline FPathHandle FVersePathRegistry::AllocateHandleNoLock() 
{
	uint32 Index;
	if (!FreeList.IsEmpty())
	{
		Index = FreeList.Last();
		FreeList.Pop();
	}
	else
	{
		Index = PathDatas.Num();
		PathDatas.AddDefaulted();
	}
	return { Index, PathDatas[Index].Generation};
}

inline void FVersePathRegistry::FreeHandleNoLock(const uint32 Index)
{
	check(Index < (uint32)PathDatas.Num());
	check((PathDatas[Index].Generation & FPathHandle::GenerationMask) + 1 < FPathHandle::GenerationMask);
	PathDatas[Index].Generation++;
	FreeList.Push(Index);
}

inline bool FVersePathRegistry::IsValidHandleNoLock(FPathHandle Handle) const
{
	const uint32 Index = Handle.Index;
	if (Index == FPathHandle::Null.Index || Index >= (uint32)PathDatas.Num())
	{
		return false;
	}

	const uint32 Generation = Handle.Generation & FPathHandle::GenerationMask;
	const uint32 ActualGeneration = PathDatas[Index].Generation;
	if (Generation != ActualGeneration)
	{
		return false;
	}
	return true;
}

inline bool FVersePathRegistry::IsValidHandle(FPathHandle Handle) const
{
	UE::TReadScopeLock _(Lock);
	return IsValidHandleNoLock(Handle);
}

inline bool FRegistryBuilder::Find(const FStringView VersePath, FPathDesc& OutPathDesc) const
{
	const uint32 Hash = GetTypeHash(VersePath);
	if (const FPathDesc* PathDesc = PathDescs.FindByHash(Hash, VersePath))
	{
		OutPathDesc = *PathDesc;
		return true;
	}
	return false;
}

inline const FRegistryBuilder::FPathDesc& FRegistryBuilder::FindChecked(const FStringView VersePath) const
{
	const FPathDesc* PathDesc = PathDescs.FindByHash(GetTypeHash(VersePath), VersePath);
	check(PathDesc);
	return *PathDesc;
}

inline bool FRegistryBuilder::Contains(const FStringView VersePath) const
{
	return PathDescs.ContainsByHash(GetTypeHash(VersePath), VersePath);
}

inline void FRegistryBuilder::Reserve(const int32 Num)
{
	PathDescs.Reserve(Num);
}

inline int32 FRegistryBuilder::Num() const
{
	return PathDescs.Num();
}

inline void FRegistryBuilder::Empty()
{
	PathDescs.Empty();
	Roots.Empty();
	ParentToChildren.Empty();
	bRebuildHierarchy = false;
}

inline const FString& FRegistryBuilder::GetPackageName() const
{
	return PackageName;
}

inline void FRegistryBuilder::SetPackageName(const FString& InPackageName)
{
	PackageName = InPackageName;
}

} // namespace VersePathRegistry