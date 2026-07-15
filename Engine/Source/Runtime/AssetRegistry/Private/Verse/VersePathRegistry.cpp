// Copyright Epic Games, Inc. All Rights Reserved.

#include "Verse/VersePathRegistry.h"

#include "AssetRegistryPrivate.h"
#include "Async/ParallelFor.h"
#include "Containers/StringFwd.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Logging/StructuredLog.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

DEFINE_LOG_CATEGORY(LogVersePathRegistry);

FAutoConsoleCommand StaticVersePathRegistryDump(
	TEXT("VersePathRegistry.Dump"), 
	TEXT("Dumps paths in the Verse Path Registry.\n")
	TEXT("Args: [<Path>] [-children] [-file]\n")
	TEXT("\tPath: Verse path to dump. If not provided, all registry paths are dumped\n")
	TEXT("\t-children: Prints all child paths for the provided path\n")
	TEXT("\t-file: Output is printed to a file instead of console\n"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UE::VersePathRegistry::FVersePathRegistry& Registry = UE::VersePathRegistry::FVersePathRegistry::Get();
		const bool bPrintChildren = Args.ContainsByPredicate([](const FString& Elem) 
			{
				return Elem.StartsWith(TEXT("-children"), ESearchCase::IgnoreCase);
			});
		const bool bPrintToFile = Args.ContainsByPredicate([](const FString& Elem)
			{
				return Elem.StartsWith(TEXT("-file"), ESearchCase::IgnoreCase);
			});

		const int32 OptionCount = bPrintChildren + bPrintToFile;
		if (Args.Num() > (OptionCount + 1 /*Path Argument*/))
		{
			UE_LOGF(LogVersePathRegistry, Error, "Too many arguments passed. Expected: '[<Path>] [-children] [-file]'");
			return;
		}

		FString Path;
		if (Args.Num() != OptionCount)
		{
			// Dump via path
			Path = Args[0];
		}
		
		if (!Path.IsEmpty() && Path[0] != TEXT('/'))
		{
			UE_LOGF(LogVersePathRegistry, Error, "The first argument must be the verse path to dump, which should always start with a '/'.");
			return;
		}

		TUniquePtr<FArchive> Ar = nullptr;
		if (bPrintToFile)
		{
			FString OutputPath = FPaths::ProjectLogDir() / FString::Printf(TEXT("VersePathRegistry_%s.txt"), *FDateTime::Now().ToString());
			Ar.Reset(IFileManager::Get().CreateFileWriter(*OutputPath));
			if (!Ar)
			{
				UE_LOGF(LogVersePathRegistry, Error, "Failed to create file %ls", *OutputPath);
				return;
			}
			UE_LOGF(LogVersePathRegistry, Display, "Writing output to %ls", *OutputPath);
		}
		Registry.Dump(Path, bPrintChildren, MoveTemp(Ar));
	}));

FAutoConsoleCommand StaticVersePathRegistryCanAccess(
	TEXT("VersePathRegistry.CanAccess"),
	TEXT("Check access permissions for two paths in the Verse Path Registry.\n")
	TEXT("Can be thought of: Can <ContextPath> (read|write|construct) <TargetPath>?\n")
	TEXT("Args: <ContextPath> <TargetPath>\n"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			using namespace UE::VersePathRegistry;

			if (Args.Num() < 2)
			{
				UE_LOGF(LogVersePathRegistry, Error, "Two path arguments are expected to VersePathRegistry.CanAccess: <ContextPath> <TargetPath>");
				return;
			}
			const FString& ContextPath = Args[0];
			const FString& TargetPath = Args[1];
			FVersePathRegistry& Registry = FVersePathRegistry::Get();

			if (Registry.FindPath(ContextPath).IsNull())
			{
				UE_LOGFMT(LogVersePathRegistry, Error, "ContextPath path {ContextPath} isn't registered.", *ContextPath);
				return;
			}
			FPathData TargetData;
			if (!Registry.TryGetPathData(TargetPath, TargetData))
			{
				UE_LOGFMT(LogVersePathRegistry, Error, "TargetPath path {TargetPath} isn't registered.", *TargetPath);
				return;
			}

			TStringBuilder<256> Builder;
			Builder.Appendf(TEXT("CanRead: %s"), (Registry.CanRead(ContextPath, TargetPath) ? TEXT("true") : TEXT("false")));

			if (TargetData.Kind == EPathKind::Data)
			{
				Builder.Appendf(TEXT(", CanWrite: %s"), (Registry.CanWrite(ContextPath, TargetPath) ? TEXT("true") : TEXT("false")));
			}
			else if (TargetData.Kind == EPathKind::Class)
			{
				Builder.Appendf(TEXT(", CanConstruct: %s"), (Registry.CanConstruct(ContextPath, TargetPath) ? TEXT("true") : TEXT("false")));
			}

			UE_LOGFMT(LogVersePathRegistry, Display, "{Builder}", Builder.ToString());
		}));

namespace UE::VersePathRegistry
{
const FPathHandle FPathHandle::Null = { 0, 0 };

const TCHAR* LexToString(EAccess Access)
{
	static_assert((int32)EAccess::Count == 6, "Missing handling of an EAccess value");
	switch (Access)
	{
		case EAccess::EpicInternal: return TEXT("EpicInternal");
		case EAccess::Scoped: return TEXT("Scoped");
		case EAccess::Private: return TEXT("Private");
		case EAccess::Protected: return TEXT("Protected");
		case EAccess::Internal: return TEXT("Internal");
		case EAccess::Public: return TEXT("Public");
	}
	return TEXT("<Invalid>");
}

const TCHAR* LexToString(EPathKind Kind)
{
	static_assert((int32)EPathKind::Count == 7, "Missing handling of an EPathKind value");
	switch (Kind)
	{
		case EPathKind::Unknown: return TEXT("Unknown");
		case EPathKind::Module: return TEXT("Module");
		case EPathKind::Interface: return TEXT("Interface");
		case EPathKind::Class: return TEXT("Class");
		case EPathKind::Function: return TEXT("Function");
		case EPathKind::Data: return TEXT("Data");
		case EPathKind::Alias: return TEXT("Alias");
	}
	return TEXT("<Invalid>");
}

const TCHAR* LexToString(EFlags Flags)
{
	static_assert((int32)EFlags::Count == 3, "Missing handling of an EFlags value");
	switch (Flags)
	{
		case EFlags::None: return TEXT("None");
		case EFlags::Var: return TEXT("Var");
		case EFlags::Abstract: return TEXT("Abstract");
	}
	return TEXT("<Invalid>");
}

FVersePathRegistry& FVersePathRegistry::Get()
{
	static FVersePathRegistry Singleton;
	return Singleton;
}

FVersePathRegistry::FVersePathRegistry()
{
	// Reserve 0 as invalid 
	AllocateHandleNoLock();
}

FVersePathRegistry::~FVersePathRegistry()
{
}

const bool FVersePathRegistry::TryGetContextAndTargetIndicesNoLock(const FStringView ContextVersePath, const FStringView TargetVersePath, 
	uint32& OutContextIndex, uint32& OutTargetIndex) const
{
	// to consider: we could refactor to hoist the normalization and hashing outside of the read lock
	TStringBuilder<256> ContextBuilder;
	NormalizeVersePath(ContextBuilder, ContextVersePath);
	const FStringView NormalizedContextVersePath = ContextBuilder.ToView();
	const uint32 ContextHash = GetTypeHash(NormalizedContextVersePath);

	TStringBuilder<256> TargetBuilder;
	NormalizeVersePath(TargetBuilder, TargetVersePath);
	const FStringView NormalizedTargetVersePath = TargetBuilder.ToView();
	const uint32 TargetHash = GetTypeHash(NormalizedTargetVersePath);

	const uint32* ContextIndex = PathToPathDataIndex.FindByHash(ContextHash, NormalizedContextVersePath);
	const uint32* TargetIndex = PathToPathDataIndex.FindByHash(TargetHash, NormalizedTargetVersePath);
	if (!ContextIndex)
	{
		UE_LOGFMT(LogVersePathRegistry, Warning, "Context path {VersePath} does not exist in the Verse Path Registry.", ContextVersePath);
		return false;
	}
	if (!TargetIndex)
	{
		// To consider: We don't do any path walking at the moment. i.e. the registry doesn't care about the actual
		// path strings beyond using them as a key. If we end up with a mechanism to efficiently walk up a path's outer chain
		// we could avoid storing some paths (e.g. private paths) in the registry and resolve access checks by walking path
		// chains inside of CanAccess. This would mean we don't want to warn here since it's expected to be passed paths to things
		// that might exist but the registry doesn't know about (but can deduce the access is simply Private by its non-existence)
		UE_LOGFMT(LogVersePathRegistry, Warning, "Target path {VersePath} does not exist in the Verse Path Registry.", TargetVersePath);
		return false;
	}

	OutContextIndex = *ContextIndex;
	OutTargetIndex = *TargetIndex;
	return true;
}

bool FVersePathRegistry::CanConstruct(const FStringView ContextVersePath, const FStringView TargetVersePath) const
{
	uint32 ContextIndex;
	uint32 TargetIndex;
	UE::TReadScopeLock _(Lock);
	if (!TryGetContextAndTargetIndicesNoLock(ContextVersePath, TargetVersePath, ContextIndex, TargetIndex))
	{
		return false;
	}
	return CanAccessNoLock(ContextIndex, TargetIndex, EAccessType::Construct);
}

bool FVersePathRegistry::CanConstruct(const FPathHandle ContextHandle, const FPathHandle TargetHandle) const
{
	UE::TReadScopeLock _(Lock);
	if (!IsValidHandleNoLock(ContextHandle) || !IsValidHandleNoLock(TargetHandle))
	{
		return false;
	}
	return CanAccessNoLock(ContextHandle.Index, TargetHandle.Index, EAccessType::Construct);
}

bool FVersePathRegistry::CanWrite(const FStringView ContextVersePath, const FStringView TargetVersePath) const
{
	uint32 ContextIndex;
	uint32 TargetIndex;
	UE::TReadScopeLock _(Lock);
	if (!TryGetContextAndTargetIndicesNoLock(ContextVersePath, TargetVersePath, ContextIndex, TargetIndex))
	{
		return false;
	}
	return CanAccessNoLock(ContextIndex, TargetIndex, EAccessType::Write);
}

bool FVersePathRegistry::CanWrite(const FPathHandle ContextHandle, const FPathHandle TargetHandle) const
{
	UE::TReadScopeLock _(Lock);
	if (!IsValidHandleNoLock(ContextHandle) || !IsValidHandleNoLock(TargetHandle))
	{
		return false;
	}
	return CanAccessNoLock(ContextHandle.Index, TargetHandle.Index, EAccessType::Write);
}

bool FVersePathRegistry::CanRead(const FStringView ContextVersePath, const FStringView TargetVersePath) const
{
	uint32 ContextIndex;
	uint32 TargetIndex;
	UE::TReadScopeLock _(Lock);
	if (!TryGetContextAndTargetIndicesNoLock(ContextVersePath, TargetVersePath, ContextIndex, TargetIndex))
	{
		return false;
	}
	return CanAccessNoLock(ContextIndex, TargetIndex, EAccessType::Read);
}

bool FVersePathRegistry::CanRead(const FPathHandle ContextHandle, const FPathHandle TargetHandle) const
{
	UE::TReadScopeLock _(Lock);
	if (!IsValidHandleNoLock(ContextHandle) || !IsValidHandleNoLock(TargetHandle))
	{
		return false;
	}
	return CanAccessNoLock(ContextHandle.Index, TargetHandle.Index, EAccessType::Read);
}

bool FVersePathRegistry::CanAccessNoLock(const uint32 ContextIndex, const uint32 TargetIndex, EAccessType AccessType) const
{
	check(ContextIndex < (uint32)PathDatas.Num());
	check(TargetIndex < (uint32)PathDatas.Num());
	const FPathData& TargetData = PathDatas[TargetIndex];

	if (AccessType == EAccessType::Construct)
	{
		// Construction is only a question for class paths
		if (TargetData.Kind != EPathKind::Class)
		{
			return false;
		}
		// We cannot construct abstract types
		if (TargetData.IsAbstract())
		{
			return false;
		}
	}
	else if (AccessType == EAccessType::Write && !TargetData.IsVar())
	{
		// We can only write to var types
		return false;
	}

	const auto GetAccessLevel = [this](EAccessType AccessType, const FPathData& PathData, const TArray<FPathHandle>*& OutScopeAccessPaths)
		{
			EAccess AccessLevel;
			if (AccessType == EAccessType::Construct && PathData.Kind == EPathKind::Class)
			{
				AccessLevel = PathData.ConstructorAccess;
				OutScopeAccessPaths = &PathData.ConstructorScopeAccessPaths;
			}
			else if (PathData.IsVar() && AccessType == EAccessType::Write)
			{
				check(PathData.Kind == EPathKind::Data);
				AccessLevel = PathData.WriteAccess;
				OutScopeAccessPaths = &PathData.WriteScopeAccessPaths;
			}
			else
			{
				AccessLevel = PathData.ReadAccess;
				OutScopeAccessPaths = &PathData.ReadScopeAccessPaths;
			}
			return AccessLevel;
		};

	const TArray<FPathHandle>* ScopeAccessPaths = nullptr;
	const EAccess AccessLevel = GetAccessLevel(AccessType, TargetData, ScopeAccessPaths);

	// Do access resolution logic. See CScope::CanAccess
	switch (AccessLevel)
	{
		case EAccess::Public:
		{
			// Access is permitted anywhere
			// This case is present to be complete for switch
			return true;
		}
		case EAccess::Scoped:
		case EAccess::Internal:
		{
			// for both internal and scoped, we may need to do some work to see if any parent scopes are scoped
			const uint32 ReferenceSiteModuleIndex = GetModuleNoLock(ContextIndex);
			const FPathData& ReferenceSiteModule = PathDatas[ReferenceSiteModuleIndex];

			// Note, Solaris has the concept of constrained definitions which allows us to conditionally perform these checks.
			// We can likely capture that flag in our PathData to do the same optimization here

			const uint32 TargetModuleIndex = GetModuleNoLock(TargetIndex);
			const FPathData& TargetModule = PathDatas[TargetModuleIndex];
			if (IsModuleSameOrChildOfNoLock(ReferenceSiteModuleIndex, TargetModuleIndex))
			{
				return true;
			}

			auto HasScopedAccess = [this, ReferenceSiteModuleIndex](const EAccess AccessLevel, const TArray<FPathHandle>* ScopeAccessPaths)
				{
					if (AccessLevel != EAccess::Scoped)
					{
						return false;
					}
					check(ScopeAccessPaths);
					FPathData Scope;
					for (const FPathHandle& ScopePath : *ScopeAccessPaths)
					{
						// The registry should know about any internal paths it contains
						checkf(IsValidHandleNoLock(ScopePath), TEXT("The verse path registry should know about all internal paths"));
						if (IsModuleSameOrChildOfNoLock(ReferenceSiteModuleIndex, ScopePath.Index))
						{
							return true;
						}
					}
					return false;
				};

			// If the definition is scoped, then we need to check each of those to see if they can see the reference scope
			if (HasScopedAccess(AccessLevel, ScopeAccessPaths))
			{
				return true;
			}

			// If the definition site is internal, but the reference site is scoped to the definition, then that's also ok
			// Walk up the parent scopes for the Definition and look for any scope access levels. We want to know if any of those 
			// parents of the definition gave access to the reference site
			FPathHandle ParentHandle = TargetData.Parent;
			while (IsValidHandleNoLock(ParentHandle))
			{
				const FPathData& ParentData = PathDatas[ParentHandle.Index];
				const TArray<FPathHandle>* ParentScopeAccessPaths = nullptr;
				const EAccess ParentAccessLevel = GetAccessLevel(AccessType, ParentData, ParentScopeAccessPaths);
				if (HasScopedAccess(ParentAccessLevel, ParentScopeAccessPaths))
				{
					return true;
				}
				ParentHandle = ParentData.Parent;
			}
			return false;
		}
		case EAccess::Protected:
		{
			const uint32 ContextClassOrInterfaceIndex = GetScopeClassOrInterfaceNoLock(ContextIndex);
			if (!ContextClassOrInterfaceIndex)
			{
				return false;
			}
			const uint32 TargetLogicalScopeIndex = GetLogicalScopeNoLock(TargetIndex);
			const uint32 TargetClassOrInterfaceIndex = GetScopeClassOrInterfaceNoLock(TargetLogicalScopeIndex);
			if (!TargetClassOrInterfaceIndex)
			{
				return false;
			}

			const FPathData& ContextClassOrInterfaceData = PathDatas[ContextClassOrInterfaceIndex];
			if (ContextClassOrInterfaceData.Kind == EPathKind::Class)
			{
				const FPathData& TargetClassOrInterfaceData = PathDatas[TargetClassOrInterfaceIndex];
				if (TargetClassOrInterfaceData.Kind == EPathKind::Class)
				{
					return IsClassNoLock(ContextClassOrInterfaceIndex, TargetClassOrInterfaceIndex);
				}
				if (TargetClassOrInterfaceData.Kind == EPathKind::Interface)
				{
					return IsInterfaceImplementorNoLock(ContextClassOrInterfaceIndex, TargetClassOrInterfaceIndex);
				}
			}
			else if (ContextClassOrInterfaceData.Kind == EPathKind::Interface)
			{
				const FPathData& TargetClassOrInterfaceData = PathDatas[TargetClassOrInterfaceIndex];
				if (TargetClassOrInterfaceData.Kind == EPathKind::Interface)
				{
					return IsInterfaceNoLock(ContextClassOrInterfaceIndex, TargetClassOrInterfaceIndex);
				}
			}
			return false;
		}
		case EAccess::Private:
		{
			const uint32 ContextClassOrInterfaceIndex = GetScopeClassOrInterfaceNoLock(ContextIndex);
			if (!ContextClassOrInterfaceIndex)
			{
				return false;
			}
			const uint32 TargetLogicalScopeIndex = GetLogicalScopeNoLock(TargetIndex);
			const uint32 TargetClassOrInterfaceIndex = GetScopeClassOrInterfaceNoLock(TargetLogicalScopeIndex);
			if (!TargetClassOrInterfaceIndex)
			{
				return false;
			}
			// Must be in same class or interface
			return ContextClassOrInterfaceIndex == TargetClassOrInterfaceIndex;
		}
		case EAccess::EpicInternal:
		{
			// Todo: Will check if the target is under a few fixed epic owned modules
			return false;
		}
	}

	return false;
}

uint32 FVersePathRegistry::GetModuleNoLock(const uint32 Index) const
{
	FPathData PathData = PathDatas[Index];
	FPathHandle ModuleHandle = { Index, 0 }; // Generation isn't important for iteration
	while (PathData.Kind != EPathKind::Module)
	{
		ModuleHandle = PathData.Parent;
		if (!IsValidHandleNoLock(ModuleHandle))
		{
			break;
		}
		PathData = PathDatas[ModuleHandle.Index];
	}
	check(PathData.Kind == EPathKind::Module);
	return ModuleHandle.Index;
}

uint32 FVersePathRegistry::GetScopeClassOrInterfaceNoLock(const uint32 Index) const
{
	FPathData PathData = PathDatas[Index];
	FPathHandle ModuleHandle = { Index, 0 }; // Generation isn't important for iteration
	while (PathData.Kind != EPathKind::Class && PathData.Kind != EPathKind::Interface)
	{
		ModuleHandle = PathData.Parent;
		if (!IsValidHandleNoLock(ModuleHandle))
		{
			break;
		}
		PathData = PathDatas[ModuleHandle.Index];
	}
	check(ModuleHandle.IsNull() || PathData.Kind == EPathKind::Class || PathData.Kind == EPathKind::Interface);
	return ModuleHandle.Index;
}

uint32 FVersePathRegistry::GetLogicalScopeNoLock(const uint32 Index) const
{
	const FPathData& PathData = PathDatas[Index];
	// Data and type aliases are not logical scopes, but they are contained within logical scopes
	if (PathData.Kind == EPathKind::Data || PathData.Kind == EPathKind::Alias)
	{
		return PathData.Parent.Index;
	}
	return Index;
}

bool FVersePathRegistry::IsClassNoLock(uint32 ContextIndex, uint32 TargetIndex) const
{
	TArray<uint32, TInlineAllocator<16>> SeenClassIndices;
	uint32 RelatedClass = ContextIndex;

	do
	{
		if (SeenClassIndices.Contains(RelatedClass))
		{
			return false;
		}
		SeenClassIndices.Push(RelatedClass);

		if (RelatedClass == TargetIndex)
		{
			return true;
		}

		const FPathData& RelatedClassData = PathDatas[RelatedClass];
		check(RelatedClassData.Kind == EPathKind::Class);
		RelatedClass = RelatedClassData.ClassSuper.Index;
	} while (RelatedClass);

	return false;
}

bool FVersePathRegistry::IsInterfaceNoLock(uint32 ContextIndex, uint32 TargetIndex) const
{
	if (ContextIndex == TargetIndex)
	{
		return true;
	}
	const FPathData& ContextData = PathDatas[ContextIndex];
	for (const FPathHandle SuperInterface : ContextData.Interfaces)
	{
		if (IsInterfaceNoLock(SuperInterface.Index, TargetIndex))
		{
			return true;
		}
	}
	return false;
}

bool FVersePathRegistry::IsInterfaceImplementorNoLock(uint32 ContextIndex, uint32 TargetIndex) const
{
	const FPathData& ContextData = PathDatas[ContextIndex];
	for (const FPathHandle SuperInterface : ContextData.Interfaces)
	{
		if (IsInterfaceNoLock(SuperInterface.Index, TargetIndex))
		{
			return true;
		}
	}
	if (!ContextData.ClassSuper.IsNull())
	{
		return IsInterfaceImplementorNoLock(ContextData.ClassSuper.Index, TargetIndex);
	}
	return false;
}

bool FVersePathRegistry::IsModuleSameOrChildOfNoLock(const uint32 ContextIndex, const uint32 TargetIndex) const
{
	uint32 ContextParentIndex = ContextIndex;
	do
	{
		if (TargetIndex == ContextParentIndex)
		{
			return true;
		}
		check(ContextParentIndex < (uint32)PathDatas.Num());
		const FPathData& ContextParentData = PathDatas[ContextParentIndex];
		ContextParentIndex = ContextParentData.Parent.Index;
	} while (ContextParentIndex);
	return false;
}

template<typename CharType>
void FVersePathRegistry::NormalizeVersePath(TStringBuilderBase<CharType>& Builder, const FStringView VersePath) const
{
	// noop currently
	Builder.Append(VersePath);
}

FPathHandle FVersePathRegistry::FindOrAddPath(const FStringView VersePath, bool* bAlreadyExisted)
{
	// todo: change this to be conditional on a new method IsFullyQualifiedVersePath() so we can avoid a 
	// copy if the user keeps paths in a form the registry expects (whatever that ends up being).
	// Might moot once we intern the paths and defer the responsibility to the pathing impl. 
	TStringBuilder<256> Builder;
	NormalizeVersePath(Builder, VersePath);
	const FStringView NormalizedVersePath = Builder.ToView();
	const uint32 Hash = GetTypeHash(NormalizedVersePath);

	const auto FindByHash = [this](const uint32 Hash, const FStringView VersePath, bool* bAlreadyExisted) -> FPathHandle
		{
			const uint32* Index = PathToPathDataIndex.FindByHash(Hash, VersePath);
			if (bAlreadyExisted)
			{
				*bAlreadyExisted = !!Index;
			}

			if (Index)
			{
				return { *Index, PathDatas[*Index].Generation };
			}
			return FPathHandle::Null;
		};

	{
		UE::TReadScopeLock _(Lock);
		const FPathHandle Handle = FindByHash(Hash, NormalizedVersePath, bAlreadyExisted);
		if (!Handle.IsNull())
		{
			return Handle;
		}
	}

	// Failed to find, take a write lock and try again, allocating if we fail again
	UE::TWriteScopeLock _(Lock);
	FPathHandle Handle = FindByHash(Hash, NormalizedVersePath, bAlreadyExisted);
	if (!Handle.IsNull())
	{
		return Handle;
	}
	Handle = AllocateHandleNoLock();
	check(Handle.Index);
	PathToPathDataIndex.EmplaceByHash(Hash, NormalizedVersePath, Handle.Index);
	return Handle;
}

bool FVersePathRegistry::TryGetPathData(const FStringView VersePath, FPathData& OutPathData) const
{
	TStringBuilder<256> Builder;
	NormalizeVersePath(Builder, VersePath);
	const FStringView NormalizedVersePath = Builder.ToView();
	const uint32 Hash = GetTypeHash(NormalizedVersePath);

	UE::TReadScopeLock _(Lock);
	const uint32* Index = PathToPathDataIndex.FindByHash(Hash, NormalizedVersePath);
	if (!Index)
	{
		return false;
	}
	OutPathData = PathDatas[*Index];
	return true;
}

bool FVersePathRegistry::TryGetPathData(const FPathHandle Handle, FPathData& OutPathData) const
{
	if (!IsValidHandleNoLock(Handle))
	{
		return false;
	}

	UE::TReadScopeLock _(Lock);
	OutPathData = PathDatas[Handle.Index];
	return true;
}

FPathHandle FVersePathRegistry::FindPath(const FStringView VersePath) const
{
	TStringBuilder<256> Builder;
	NormalizeVersePath(Builder, VersePath);
	const FStringView NormalizedVersePath = Builder.ToView();

	UE::TReadScopeLock _(Lock);
	return FindPathNoLock(NormalizedVersePath);
}
void FVersePathRegistry::FixupAddedPathInternalReferencesNoLock(const FRegistryBuilder& Builder, FPathHandle AddedHandle)
{
	FPathData& PathDataToFixup = PathDatas[AddedHandle.Index];
	const FRegistryBuilder::FPathDesc& PathDesc = Builder.FindChecked(PathDataToFixup.Path);

	if (!PathDesc.Parent.IsEmpty())
	{
		PathDataToFixup.Parent = FindPathNoLock(PathDesc.Parent);
		if (PathDataToFixup.Parent.IsNull())
		{
			UE_LOGF(LogVersePathRegistry, Warning, "Verse path '%ls' has Parent path '%ls' that is not registered. Nulling out.",
				*PathDesc.Path, *PathDesc.Parent);
		}
		else
		{
			// The builder may have added a subtree that is rooted under a path already in the registry. 
			// If so we haven't fixed up the hierarchy for this subtree's root yet so ensure that happens now.
			ParentToChildren.FindOrAdd(PathDataToFixup.Parent.Index).Add(AddedHandle.Index);
		}
	}

	PathDataToFixup.ClassSuper = FindPathNoLock(PathDesc.ClassSuper);
	if (PathDataToFixup.ClassSuper.IsNull() && !PathDesc.ClassSuper.IsEmpty())
	{
		UE_LOGF(LogVersePathRegistry, Warning, "Verse path '%ls' has ClassSuper path '%ls' that is not registered. Nulling out.",
			*PathDesc.Path, *PathDesc.ClassSuper);
	}

	PathDataToFixup.Interfaces.Reset();
	for (const FString& Path : PathDesc.Interfaces)
	{
		FPathHandle Handle = FindPathNoLock(Path);
		if (Handle.IsNull())
		{
			UE_LOGF(LogVersePathRegistry, Warning, "Verse path '%ls' has Interface path '%ls' that is not registered. Nulling out.",
				*PathDesc.Path, *Path);
		}
		else
		{
			PathDataToFixup.Interfaces.Add(Handle);
		}
	}
	PathDataToFixup.Interfaces.Shrink();

	PathDataToFixup.ReadScopeAccessPaths.Reset();
	for (const FString& Path : PathDesc.ReadScopeAccessPaths)
	{
		FPathHandle Handle = FindPathNoLock(Path);
		if (Handle.IsNull())
		{
			UE_LOGF(LogVersePathRegistry, Warning, "Verse path '%ls' has ReadScopeAccess path '%ls' that is not registered. Nulling out.",
				*PathDesc.Path, *Path);
		}
		else
		{
			PathDataToFixup.ReadScopeAccessPaths.Add(Handle);
		}
	}
	PathDataToFixup.ReadScopeAccessPaths.Shrink();

	PathDataToFixup.WriteScopeAccessPaths.Reset();
	for (const FString& Path : PathDesc.WriteScopeAccessPaths)
	{
		FPathHandle Handle = FindPathNoLock(Path);
		if (Handle.IsNull())
		{
			UE_LOGF(LogVersePathRegistry, Warning, "Verse path '%ls' has WriteScopeAccess path '%ls' that is not registered. Nulling out.",
				*PathDesc.Path, *Path);
		}
		else
		{
			PathDataToFixup.WriteScopeAccessPaths.Add(Handle);
		}
	}
	PathDataToFixup.WriteScopeAccessPaths.Shrink();

	PathDataToFixup.ConstructorScopeAccessPaths.Reset();
	for (const FString& Path : PathDesc.ConstructorScopeAccessPaths)
	{
		FPathHandle Handle = FindPathNoLock(Path);
		if (Handle.IsNull())
		{
			UE_LOGF(LogVersePathRegistry, Warning, "Verse path '%ls' has ConstructorScopeAccess path '%ls' that is not registered. Nulling out.",
				*PathDesc.Path, *Path);
		}
		else
		{
			PathDataToFixup.ConstructorScopeAccessPaths.Add(Handle);
		}
	}
	PathDataToFixup.ConstructorScopeAccessPaths.Shrink();
}

void FVersePathRegistry::FixupInternalReferencesNoLock(const uint32 IndexToFixup, const TMap<FPathHandle, FPathHandle>& UpdatedHandles)
{
	FPathData& PathDataToFixup = PathDatas[IndexToFixup];
	PathDataToFixup.Parent = UpdatedHandles.FindRef(PathDataToFixup.Parent, PathDataToFixup.Parent);
	PathDataToFixup.ClassSuper = UpdatedHandles.FindRef(PathDataToFixup.ClassSuper, PathDataToFixup.ClassSuper);

	for (FPathHandle& Handle : PathDataToFixup.Interfaces)
	{
		Handle = UpdatedHandles.FindRef(Handle, Handle);
	}
	for (FPathHandle& Handle : PathDataToFixup.ReadScopeAccessPaths)
	{
		Handle = UpdatedHandles.FindRef(Handle, Handle);
	}
	for (FPathHandle& Handle : PathDataToFixup.WriteScopeAccessPaths)
	{
		Handle = UpdatedHandles.FindRef(Handle, Handle);
	}
	for (FPathHandle& Handle : PathDataToFixup.ConstructorScopeAccessPaths)
	{
		Handle = UpdatedHandles.FindRef(Handle, Handle);
	}
}

bool FVersePathRegistry::RemoveSubtreeNoLock(const uint32 Parent, 
	TMap<FString, TPair<FPathHandle, FPathHandle>>& PathToUpdatedHandles, TMap<FPathHandle, FString>& RemovedHandlesToPath)
{
	bool bRemovePath = true;
	FPathData& ParentData = PathDatas[Parent];
	FString& ParentPath = ParentData.Path;
	const bool bPathIsBeingUpdated = PathToUpdatedHandles.Contains(ParentPath);

	// Don't change the ref-count for modules being updated
	if (ParentData.Kind == EPathKind::Module)
	{
		if (bPathIsBeingUpdated)
		{
			bRemovePath = false;
		}
		else
		{
			// If we removed the last reference to the module remove it from the registry
			if (int32* RefCount = ModuleToRefCount.Find(ParentData.Path))
			{
				if (--(*RefCount) == 0)
				{
					ModuleToRefCount.Remove(ParentData.Path);
				}
				else
				{
					bRemovePath = false;
				}
			}
		}
	}

	if (!bRemovePath)
	{
		return false;
	}

	PathToPathDataIndex.Remove(ParentPath);
	if (!bPathIsBeingUpdated)
	{
		const FPathHandle ParentHandle = { Parent, PathDatas[Parent].Generation };
		const uint32 Hash = GetTypeHash(ParentHandle);
		if (!RemovedHandlesToPath.ContainsByHash(Hash, ParentHandle))
		{
			RemovedHandlesToPath.AddByHash(Hash, ParentHandle, MoveTemp(ParentPath));
		}
	}
	FreeHandleNoLock(Parent);

	// Take a copy as we will be mutating ParentToChildren as we recurse
	TSet<uint32> ChildrenToRemove;
	if (!ParentToChildren.RemoveAndCopyValue(Parent, ChildrenToRemove))
	{
		return bRemovePath;
	}

	RemovedHandlesToPath.Reserve(RemovedHandlesToPath.Num() + ChildrenToRemove.Num());
	for (const uint32 Child : ChildrenToRemove)
	{
		RemoveSubtreeNoLock(Child, PathToUpdatedHandles, RemovedHandlesToPath);
	}

	return bRemovePath;
}

uint32 FVersePathRegistry::AddSubtreeNoLock(const FRegistryBuilder& Builder, const FSetElementId BuilderParentId,
	TMap<FString, TPair<FPathHandle, FPathHandle>>& PathToUpdatedHandles, TArray<FPathHandle>& AddedHandles)
{
	const FRegistryBuilder::FPathDesc& SrcParentData = Builder.PathDescs.Get(BuilderParentId);
	TPair<FPathHandle, FPathHandle>* UpdatePair = PathToUpdatedHandles.Find(SrcParentData.Path);
	const bool bPathIsBeingUpdated = !!UpdatePair;

	// Special handling for modules. Modules are shared across packages, so we need to refcount them so we don't 
	// remove them and their children unless there are no packages referring to them. Otherwise, removing a module might remove all valid 
	// paths under it incorrectly. Even if we don't add a new Module path handle here, we still want to add all children the builder
	// has specified as we are appending nodes for this package even if the module is shared amongst other packages.
	bool bAddPath = true;
	if (SrcParentData.Kind == EPathKind::Module)
	{
		if (bPathIsBeingUpdated)
		{
			bAddPath = false;
		}
		else
		{
			int32& RefCount = ModuleToRefCount.FindOrAdd(SrcParentData.Path);
			if (RefCount++ != 0)
			{
				bAddPath = false;
			}
		}
	}

	FPathHandle DestParentHandle;
	if (bPathIsBeingUpdated)
	{
		check(UpdatePair);
		if (bAddPath)
		{
			DestParentHandle = AllocateHandleNoLock();
			UpdatePair->Value = DestParentHandle;
		}
		else
		{
			DestParentHandle = UpdatePair->Key;
			PathToUpdatedHandles.Remove(SrcParentData.Path);
		}
	}
	else
	{
		DestParentHandle = AllocateHandleNoLock();
		AddedHandles.Add(DestParentHandle);
	}

	const uint32 DestParentIndex = DestParentHandle.Index;
	if (bAddPath)
	{
		// We can't assign all fields since we don't know the path handle for all paths yet
		// but we can fill in all non-pathhandle fields and fixup the rest later
		FPathData& DestParentData = PathDatas[DestParentIndex];
		DestParentData.Path = SrcParentData.Path;
		DestParentData.Kind = SrcParentData.Kind;
		DestParentData.ReadAccess = SrcParentData.ReadAccess;
		DestParentData.WriteAccess = SrcParentData.WriteAccess;
		DestParentData.ConstructorAccess = SrcParentData.ConstructorAccess;
		DestParentData.Flags = SrcParentData.Flags;

		// We don't know the parent handle yet but we don't want to signal this is a root path either
		// if the current Parent is Null, so set Parent to an invalid handle (0 index but > 0 generation).
		// In append we will determine if the parent is Null or not to determine if this is a root.
		DestParentData.Parent = SrcParentData.Parent.IsEmpty() ? FPathHandle::Null : FPathHandle{0, 1};

		PathToPathDataIndex.Add(SrcParentData.Path, DestParentIndex);
	}

	const TSet<FSetElementId, FRegistryBuilder::FSetElementIdFuncs>* ChildrenToAdd = Builder.ParentToChildren.Find(BuilderParentId);
	if (ChildrenToAdd)
	{
		TSet<uint32> DestChildren;
		DestChildren.Reserve(ChildrenToAdd->Num());
		AddedHandles.Reserve(AddedHandles.Num() + ChildrenToAdd->Num());

		for (const FSetElementId SrcChildId : *ChildrenToAdd)
		{
			const uint32 DestChildIndex = AddSubtreeNoLock(Builder, SrcChildId, PathToUpdatedHandles, AddedHandles);
			DestChildren.Add(DestChildIndex);
		}
		TSet<uint32>& ExistingDestChildren = ParentToChildren.FindOrAdd(DestParentIndex);
		ExistingDestChildren.Append(MoveTemp(DestChildren));
	}
	return DestParentIndex;
}

void FVersePathRegistry::CollectPackageRoots(const FRegistryBuilder& Builder, TSet<uint32>& OutPackageRoots)
{
	check(!Builder.bRebuildHierarchy);
	OutPackageRoots.Reset();
	TArray<FSetElementId, TInlineAllocator<64>> Stack;
	for (const FSetElementId Root : Builder.Roots)
	{
		Stack.Push(Root);
	}

	while (Stack.Num() > 0)
	{
		const FSetElementId ParentBuilderId = Stack.Pop();
		check(Builder.PathDescs.IsValidId(ParentBuilderId));

		const FRegistryBuilder::FPathDesc& ParentDesc = Builder.PathDescs.Get(ParentBuilderId);
		// For now, roots will always be modules, but once dynamic field support is added
		// this check will likely become invalid and need to be updated
		check(ParentDesc.Kind == EPathKind::Module);

		FPathHandle ParentHandle = FindPathNoLock(ParentDesc.Path);
		check(!ParentHandle.IsNull());
		OutPackageRoots.Add(ParentHandle.Index);

		if (const TSet<FSetElementId, FRegistryBuilder::FSetElementIdFuncs>* Children = Builder.ParentToChildren.Find(ParentBuilderId))
		{
			for (FSetElementId ChildBuilderId : *Children)
			{
				const FRegistryBuilder::FPathDesc& ChildDesc = Builder.PathDescs.Get(ChildBuilderId);
				if (ChildDesc.Kind == EPathKind::Module)
				{
					Stack.Push(ChildBuilderId);
				}
				else
				{
					FPathHandle ChildHandle = FindPathNoLock(ChildDesc.Path);
					check(!ChildHandle.IsNull());
					OutPackageRoots.Add(ChildHandle.Index);
				}				
			}
		}
	}
	OutPackageRoots.Shrink();
}

void FVersePathRegistry::Append(FRegistryBuilder& Builder, FRegistryEvents* OutEvents)
{
	if (!Builder.Num())
	{
		return;
	}

	FRegistryEvents LocalEvents;
	FRegistryEvents* Events = OutEvents ? OutEvents : &LocalEvents;

	TArray<FPathHandle> AddedHandles;
	TMap<FPathHandle, FString> RemovedHandlesToPath;
	TMap<FPathHandle, FPathHandle> UpdatedHandles;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FVersePathRegistry::Append);
		Builder.BuildHierarchy();

		UE::TWriteScopeLock _(Lock);
		TMap<FString, TPair<FPathHandle, FPathHandle>> PathToUpdatedHandles;
		AddedHandles.Reserve(Builder.Roots.Num());

		// Determine which paths are being updated (i.e. exist in the builder and the registry)
		int32 NumNewPaths = 0;
		for (const FRegistryBuilder::FPathDesc& PathDesc : Builder.PathDescs)
		{
			const uint32* Index = PathToPathDataIndex.Find(PathDesc.Path);
			NumNewPaths += (Index == nullptr);
			if (Index)
			{
				// Only store key as we don't know the updated handle value yet.
				// The updated handle value will be assigned during the calls to AddSubtreeNoLock.
				TPair<FPathHandle, FPathHandle> UpdatePair;
				UpdatePair.Key = { *Index, PathDatas[*Index].Generation };
				PathToUpdatedHandles.Add(PathDesc.Path, MoveTemp(UpdatePair));
			}
		}

		// If we are registering the builder for a package already registered, remove the roots added last time before proceeding.
		TSet<uint32> EmptyPackageRoots;
		TSet<uint32>* PackageRoots = &EmptyPackageRoots;
		if (!Builder.GetPackageName().IsEmpty())
		{
			PackageRoots = &PackageToRoots.FindOrAdd(Builder.GetPackageName());
			for (const uint32 RootIndex : *PackageRoots)
			{
				// Found a matching root. Remove the entire sub-tree from the registry
				const FPathData& Data = PathDatas[RootIndex];
				const uint32 ParentIndex = Data.Parent.Index;
				if (RemoveSubtreeNoLock(RootIndex, PathToUpdatedHandles, RemovedHandlesToPath))
				{
					Roots.Remove(RootIndex);

					// Since we aren't guaranteed to have removed the package root's parent
					// we need to ensure we remove ourselves from the parent to childen list
					if (TSet<uint32>* Children = ParentToChildren.Find(ParentIndex))
					{
						Children->Remove(RootIndex);
					}
				}
			}
		}

		// We may have removed the package roots from the last time we registered the package above but the builder may have new paths
		// that overlap something in the registry, so remove all paths under the builder roots we are about to add
		for (const FSetElementId BuilderRootId : Builder.Roots)
		{
			const FRegistryBuilder::FPathDesc& BuilderRoot = Builder.PathDescs.Get(BuilderRootId);
			if (const uint32* pIndex = PathToPathDataIndex.Find(BuilderRoot.Path))
			{
				// Make a copy RemoveSubtreeNoLock will mutate PathToPathDataIndex
				const uint32 RootIndexToRemove = *pIndex;

				// If we attempted to remove this root because the package was registered before, 
				// don't double free which is problematic for modules which are ref-counted
				if (PackageRoots->Contains(RootIndexToRemove))
				{
					continue;
				}

				// Found a matching root. Remove the entire sub-tree from the registry
				if (RemoveSubtreeNoLock(RootIndexToRemove, PathToUpdatedHandles, RemovedHandlesToPath))
				{
					Roots.Remove(RootIndexToRemove);
				}
			}
		}

		// Add all subtrees under the new roots
		PathToPathDataIndex.Reserve(PathToPathDataIndex.Num() + NumNewPaths);
		PathDatas.Reserve(PathDatas.Num() + NumNewPaths);
		for (const FSetElementId BuilderRootId : Builder.Roots)
		{
			// Add new root
			const uint32 NewRootIndex = AddSubtreeNoLock(Builder, BuilderRootId, PathToUpdatedHandles, AddedHandles);

			// If a "root" was added in the builder, it might actually be appending to an existing subtree in
			// the global registry instead so only add it to the Roots list if it has no parent.
			if (PathDatas[NewRootIndex].Parent.IsNull())
			{
				Roots.Add(NewRootIndex);
			}
		}

		if (PackageRoots != &EmptyPackageRoots)
		{
			// Now that all paths are registered collect the roots
			CollectPackageRoots(Builder, *PackageRoots);
		}

		UpdatedHandles.Reserve(PathToUpdatedHandles.Num());
		for (const TPair<FString, TPair<FPathHandle, FPathHandle>>& Pair : PathToUpdatedHandles)
		{
			const TPair<FPathHandle, FPathHandle>& OldToNewHandles = Pair.Value;
			UpdatedHandles.Add(OldToNewHandles.Key, OldToNewHandles.Value);
		}

		// First correct all added/updated handles -- they were left partially initialized
		for (FPathHandle AddedHandle : AddedHandles)
		{
			FixupAddedPathInternalReferencesNoLock(Builder, AddedHandle);
		}
		for (const TPair<FPathHandle, FPathHandle>& Pair : UpdatedHandles)
		{
			FPathHandle AddedHandle = Pair.Value;
			FixupAddedPathInternalReferencesNoLock(Builder, AddedHandle);
		}

		// Now that we have fixed all added/updated paths we must walk all paths (the added ones can be 
		// skipped but we don't currently) in the registry to fix up any internal references that may have been 
		// invalidated. We can't walk only the added or updated paths since it's possible, for example, for a 
		// class path to refer to a ClassSuper path that was updated but the class path itself was not updated.
		const int32 MaxSizeForSingleThreadedFixup = 1024;
		EParallelForFlags PFFlags = PathToPathDataIndex.GetMaxIndex() < MaxSizeForSingleThreadedFixup
			? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
		ParallelFor(TEXT("FVersePathRegistry_FixupInternalReferences_PF"), PathToPathDataIndex.GetMaxIndex(), 1,
			[this, &UpdatedHandles](int32 Index)
			{
				FSetElementId Id = FSetElementId::FromInteger(Index);
				if (!PathToPathDataIndex.IsValidId(Id))
				{
					return;
				}

				TPair<FString, uint32>& Pair = PathToPathDataIndex.Get(Id);
				const uint32 PathDataIndex = Pair.Value;
				FixupInternalReferencesNoLock(PathDataIndex, UpdatedHandles);
			}, PFFlags);

		ShrinkNoLock();
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FVersePathRegistry::Append_Broadcast);
	UE::TReadScopeLock _(Lock);
	Events->RemovedPaths = MoveTemp(RemovedHandlesToPath);
	Events->AddedPaths = MoveTemp(AddedHandles);
	Events->UpdatedPaths = MoveTemp(UpdatedHandles);

	if (Events->Num())
	{
		OnRegistryUpdatedEvent.Broadcast(*Events);
	}
}

void FVersePathRegistry::ForEachPath(TFunctionRef<void(FPathHandle, const FPathData&)> Visitor) const
{
	UE::TReadScopeLock _(Lock);
	for (const TPair<FString, uint32>& Pair : PathToPathDataIndex)
	{
		const uint32 Index = Pair.Value;
		const FPathData& Data = PathDatas[Index];
		Visitor(FPathHandle{ Index, Data.Generation }, Data);
	}
}

void FVersePathRegistry::Dump(const FStringView DumpPath, bool bPrintChildren, TUniquePtr<FArchive> Ar) const
{
	TStringBuilder<1024> Builder;
	Builder.AppendChar(TEXT('\n'));

	auto AppendFlags = [](TStringBuilderBase<TCHAR>& Builder, EFlags Flags) 
		{
			Builder.Append(TEXT("Flags: "));
			if (Flags == EFlags::None)
			{
				Builder.Append(LexToString(Flags));
				return;
			}

			uint8 ToPrint = 1;
			do
			{
				if ((uint8)Flags & ToPrint)
				{
					Builder.Appendf(TEXT("%s|"), LexToString((EFlags)ToPrint));
				}
				ToPrint <<= 1;
			} while ((uint8)Flags >= ToPrint);
			Builder.RemoveSuffix(1);
		};
	auto AppendAccess = [this](TStringBuilderBase<TCHAR>& Builder, const FPathData& PathData) 
		{
			if (PathData.ReadAccess != EAccess::Scoped)
			{
				Builder.Appendf(TEXT("ReadAccess: %s"), LexToString(PathData.ReadAccess));
			}
			else
			{
				Builder.Append(TEXT("ScopeReadAccess: ["));
				for (const FPathHandle ScopeHandle : PathData.ReadScopeAccessPaths)
				{
					const FPathData& ScopeData = PathDatas[ScopeHandle.Index];
					Builder.Appendf(TEXT("%s, "), *ScopeData.Path);
				}
				Builder.RemoveSuffix(2);
				Builder.Append(TEXT("]"));
			}

			if (PathData.IsVar())
			{
				Builder.Append(TEXT(", "));
				if (PathData.WriteAccess != EAccess::Scoped)
				{
					Builder.Appendf(TEXT("WriteAccess: %s"), LexToString(PathData.WriteAccess));
				}
				else
				{
					Builder.Append(TEXT("ScopeWriteAccess: ["));
					for (const FPathHandle ScopeHandle : PathData.WriteScopeAccessPaths)
					{
						const FPathData& ScopeData = PathDatas[ScopeHandle.Index];
						Builder.Appendf(TEXT("%s, "), *ScopeData.Path);
					}
					Builder.RemoveSuffix(2);
					Builder.Append(TEXT("]"));
				}
			}
			else if (PathData.Kind == EPathKind::Class)
			{
				Builder.Append(TEXT(", "));
				if (PathData.ConstructorAccess != EAccess::Scoped)
				{
					Builder.Appendf(TEXT("ConstructorAccess: %s"), LexToString(PathData.ConstructorAccess));
				}
				else
				{
					Builder.Append(TEXT("ScopeConstructorAccess: ["));
					for (const FPathHandle ScopeHandle : PathData.ConstructorScopeAccessPaths)
					{
						const FPathData& ScopeData = PathDatas[ScopeHandle.Index];
						Builder.Appendf(TEXT("%s, "), *ScopeData.Path);
					}
					Builder.RemoveSuffix(2);
					Builder.Append(TEXT("]"));
				}
			}
		};

	auto AppendPathData = [this, &AppendAccess, &AppendFlags](TStringBuilderBase<TCHAR>& Builder, const FPathData& PathData)
		{
			Builder.Appendf(TEXT("%ls"), *PathData.Path);
			Builder.Appendf(TEXT(" (Kind: %s"), LexToString(PathData.Kind));
			Builder.Append(TEXT(", "));
			AppendFlags(Builder, PathData.Flags);
			Builder.Append(TEXT(", "));
			AppendAccess(Builder, PathData);
			Builder.Append(TEXT(")"));

			if (!PathData.Parent.IsNull())
			{
				const FPathData& ParentData = PathDatas[PathData.Parent.Index];
				Builder.Appendf(TEXT(" - Parent: %s"), *ParentData.Path);
			}

			if (PathData.Kind == EPathKind::Class || PathData.Kind == EPathKind::Interface)
			{
				if (!PathData.ClassSuper.IsNull())
				{
					check(PathData.Kind == EPathKind::Class);
					const FPathData& SuperData = PathDatas[PathData.ClassSuper.Index];
					Builder.Appendf(TEXT(" - SuperClass: %s"), *SuperData.Path);
				}

				if (PathData.Interfaces.Num())
				{
					Builder.Append(TEXT(" - Interfaces: ["));
					for (FPathHandle InterfaceHandle : PathData.Interfaces)
					{
						const FPathData& InterfaceData = PathDatas[InterfaceHandle.Index];
						check(!InterfaceData.Path.IsEmpty());
						Builder.Appendf(TEXT("%s, "), *InterfaceData.Path);
					}
					Builder.RemoveSuffix(2);
					Builder.Append(TEXT("]"));
				}
			}

			Builder.Append(TEXT("\n"));
		};

	auto DumpChildren = [this, &AppendPathData](TStringBuilderBase<TCHAR>& Builder, uint32 Root, int32 Indent, auto&& DumpChildren) -> void
		{
			for (int32 i = 0; i < Indent; ++i)
			{
				Builder.AppendChar(TEXT('\t'));
			}
			const FPathData& PathData = PathDatas[Root];
			AppendPathData(Builder, PathData);

			if (const TSet<uint32>* Children = ParentToChildren.Find(Root))
			{
				// Sort children for more deterministic output
				TArray<uint32> SortedChildren;
				for (const int32 Index : *Children)
				{
					SortedChildren.Add(Index);
				}
				SortedChildren.Sort([this](const int32& Lhs, const int32& Rhs)
					{
						return PathDatas[Lhs].Path < PathDatas[Rhs].Path;
					});


				for (const uint32 Child : SortedChildren)
				{
					Indent++;
					DumpChildren(Builder, Child, Indent, DumpChildren);
					Indent--;
				}
			}
		};

	// No path specified means print everything
	UE::TReadScopeLock _(Lock);
	if (DumpPath.IsEmpty())
	{
		// Sort roots for more deterministic output
		TArray<uint32> SortedRoots;
		for (const int32 Index : Roots)
		{
			SortedRoots.Add(Index);
		}
		SortedRoots.Sort([this](const int32& Lhs, const int32& Rhs)
			{
				return PathDatas[Lhs].Path < PathDatas[Rhs].Path;
			});

		for (uint32 Root : SortedRoots)
		{
			DumpChildren(Builder, Root, 0, DumpChildren);
		}
	}
	else 
	{
		FPathHandle PathHandle = FindPathNoLock(DumpPath);
		if (PathHandle.IsNull())
		{
			UE_LOGFMT(LogVersePathRegistry, Error, "Path '{DumpPath}' is not in the registry.", DumpPath);
		}
		else if (bPrintChildren)
		{
			DumpChildren(Builder, PathHandle.Index, 0, DumpChildren);
		}
		else
		{
			const FPathData& PathData = PathDatas[PathHandle.Index];
			AppendPathData(Builder, PathData);
		}
	}

	if (Ar)
	{
		Ar->Logf(TEXT("%s"), Builder.ToString());
	}
	else
	{
		UE_LOGF(LogVersePathRegistry, Display, "\n\n%ls", Builder.ToString());
	}
}

void FVersePathRegistry::Empty()
{
	UE::TWriteScopeLock _(Lock);
	PathDatas.Empty();
	PathToPathDataIndex.Empty();
	FreeList.Empty();
	Roots.Empty();
	ParentToChildren.Empty();
	ModuleToRefCount.Empty();
	PackageToRoots.Empty();

	// Reserve 0 as invalid 
	AllocateHandleNoLock();
}

///////////////////////////////////////////////////////
// FRegistryEvents
///////////////////////////////////////////////////////

int32 FRegistryEvents::Num() const
{
	return RemovedPaths.Num() + AddedPaths.Num() + UpdatedPaths.Num();
}

void FRegistryEvents::Reset()
{
	RemovedPaths.Empty();
	AddedPaths.Empty();
	UpdatedPaths.Empty();
}

///////////////////////////////////////////////////////
// FRegistryBuilder
///////////////////////////////////////////////////////

FRegistryBuilder::FRegistryBuilder(const FStringView InPackageName)
	: PackageName(InPackageName)
{
}

bool FRegistryBuilder::ValidatePath(const FPathDesc& PathDesc)
{
	constexpr auto VerifyPath = [](const FString& Path)
		{
			if (Path.IsEmpty() || 
				// We allow decorated names starting with '(' for functions until 
				// Verse has a proper fix for identifying overloaded functions
				(Path[0] != TEXT('/') && Path[0] != TEXT('(')))
			{
				return false;
			}
			return true;
		};

	if (!VerifyPath(PathDesc.Path))
	{
		UE_LOGF(LogVersePathRegistry, Warning, "Failed to register invalid Verse path '%ls'", *PathDesc.Path);
		return false;
	}

	if (!PathDesc.Parent.IsEmpty())
	{
		if (!VerifyPath(PathDesc.Parent))
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls' with invalid Parent Verse path '%ls'", *PathDesc.Path, *PathDesc.Parent);
			return false;
		}
	}

	if (PathDesc.Kind == EPathKind::Unknown || ((uint8)PathDesc.Kind >= (uint8)EPathKind::Count))
	{
		UE_LOGF(LogVersePathRegistry, Warning,
			"Failed to register Verse Path '%ls' with invalid EPathKind '%ls'", *PathDesc.Path, LexToString(PathDesc.Kind));
		return false;
	}

	if (!PathDesc.ClassSuper.IsEmpty())
	{
		if (PathDesc.Kind != EPathKind::Class)
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls' as EPathKind '%ls' with ClassSuper '%ls'. Only EPathKind::Class may have a ClassSuper",
				*PathDesc.Path, LexToString(PathDesc.Kind), *PathDesc.ClassSuper);
			return false;
		}

		if (!VerifyPath(PathDesc.ClassSuper))
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls' with invalid ClassSuper Verse path '%ls'", *PathDesc.Path, *PathDesc.ClassSuper);
			return false;
		}
	}

	if (!PathDesc.Interfaces.IsEmpty())
	{
		if (PathDesc.Kind != EPathKind::Class && PathDesc.Kind != EPathKind::Interface)
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls'. This path has Interfaces defined and thus must be a Class or Interface but is EPathKind '%ls'",
				*PathDesc.Path, LexToString(PathDesc.Kind));
			return false;
		}

		for (const FString& InterfacePath : PathDesc.Interfaces)
		{
			if (!VerifyPath(InterfacePath))
			{
				UE_LOGF(LogVersePathRegistry, Warning,
					"Failed to register Verse path '%ls' with invalid Interface Verse path '%ls'", *PathDesc.Path, *InterfacePath);
				return false;
			}
		}
	}

	for (const FString& ScopeAccessPath : PathDesc.ReadScopeAccessPaths)
	{
		if (!VerifyPath(ScopeAccessPath))
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls' with invalid ReadScopeAccess Verse path '%ls'", *PathDesc.Path, *ScopeAccessPath);
			return false;
		}

		// If we have this path registered already we can confirm it matches what we will be writing
		if (const FPathDesc* ScopeAccessDesc = PathDescs.Find(ScopeAccessPath))
		{
			if (ScopeAccessDesc->Kind != EPathKind::Module)
			{
				UE_LOGF(LogVersePathRegistry, Warning,
					"Failed to register Verse path '%ls'. ReadScopeAccess Verse path '%ls' should be a Module but is instead a %ls ",
					*PathDesc.Path, *ScopeAccessPath, LexToString(ScopeAccessDesc->Kind));
				return false;
			}
		}
	}
	if (!PathDesc.WriteScopeAccessPaths.IsEmpty())
	{
		if (PathDesc.Kind != EPathKind::Data || !EnumHasAnyFlags(PathDesc.Flags, EFlags::Var))
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls'. Paths with WriteScopeAccessPaths must be EPathKind Data and be flagged as Var. EPathKind=%ls, Flags=%02x",
				*PathDesc.Path, LexToString(PathDesc.Kind), (uint8)PathDesc.Flags);
			return false;
		}

		for (const FString& ScopeAccessPath : PathDesc.WriteScopeAccessPaths)
		{
			if (!VerifyPath(ScopeAccessPath))
			{
				UE_LOGF(LogVersePathRegistry, Warning,
					"Failed to register Verse path '%ls' with invalid WriteScopeAccess Verse path '%ls'", *PathDesc.Path, *ScopeAccessPath);
				return false;
			}

			// If we have this path registered already we can confirm it matches what we will be writing
			if (const FPathDesc* ScopeAccessDesc = PathDescs.Find(ScopeAccessPath))
			{
				if (ScopeAccessDesc->Kind != EPathKind::Module)
				{
					UE_LOGF(LogVersePathRegistry, Warning,
						"Failed to register Verse path '%ls'. WriteScopeAccess Verse path '%ls' should be a Module but is instead a %ls ",
						*PathDesc.Path, *ScopeAccessPath, LexToString(ScopeAccessDesc->Kind));
					return false;
				}
			}
		}
	}
	if (!PathDesc.ConstructorScopeAccessPaths.IsEmpty())
	{
		// Verse lets arbitrary functions be treated as constructors as long as they are decorated appropriately
		if (PathDesc.Kind != EPathKind::Function && PathDesc.Kind != EPathKind::Class)
		{
			UE_LOGF(LogVersePathRegistry, Warning,
				"Failed to register Verse path '%ls'. Paths with ConstructorScopeAccessPaths must be either Class or Function but is EPathKind '%ls'",
				*PathDesc.Path, LexToString(PathDesc.Kind));
			return false;
		}

		for (const FString& ScopeAccessPath : PathDesc.ConstructorScopeAccessPaths)
		{
			if (!VerifyPath(ScopeAccessPath))
			{
				UE_LOGF(LogVersePathRegistry, Warning,
					"Failed to register Verse path '%ls' with invalid ConstructorScopeAccess Verse path '%ls'", *PathDesc.Path, *ScopeAccessPath);
				return false;
			}

			// If we have this path registered already we can confirm it matches what we will be writing
			if (const FPathDesc* ScopeAccessDesc = PathDescs.Find(ScopeAccessPath))
			{
				if (ScopeAccessDesc->Kind != EPathKind::Module)
				{
					UE_LOGF(LogVersePathRegistry, Warning,
						"Failed to register Verse path '%ls'. ConstructorScopeAccess Verse path '%ls' should be a Module but is instead a %ls ",
						*PathDesc.Path, *ScopeAccessPath, LexToString(ScopeAccessDesc->Kind));
					return false;
				}
			}
		}
	}

	return true;
}

void FRegistryBuilder::BuildHierarchy()
{
	if (!bRebuildHierarchy)
	{
		return;
	}

	Roots.Empty();
	ParentToChildren.Empty();
	for (TSet<FPathDesc, FPathDescKeyFuncs>::TConstIterator It = PathDescs.CreateConstIterator(); It; ++It)
	{
		const FSetElementId Id = It.GetId();
		const FPathDesc& PathDesc = *It;
		const FSetElementId ParentId = PathDescs.FindId(PathDesc.Parent);
		if (!ParentId.IsValidId())
		{
			Roots.Add(Id);
		}
		else
		{
			const uint32 Hash = GetTypeHash(ParentId.AsInteger());
			TSet<FSetElementId, FSetElementIdFuncs>* Children = ParentToChildren.FindByHash(Hash, ParentId);
			if (!Children)
			{
				Children = &ParentToChildren.AddByHash(Hash, ParentId);
			}
			Children->Add(Id);
		}
	}
	bRebuildHierarchy = false;
}

bool FRegistryBuilder::RegisterPath(const FPathDesc& PathDesc)
{
	// This type of validation is only necessary when in editor
#if WITH_EDITOR
	if (!ValidatePath(PathDesc))
	{
		return false;
	}
#endif
	bRebuildHierarchy = true;
	PathDescs.Add(PathDesc);
	return true;
}

} // namespace VersePathRegistry
