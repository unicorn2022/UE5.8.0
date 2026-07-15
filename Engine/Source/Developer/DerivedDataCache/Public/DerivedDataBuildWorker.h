// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringFwd.h"
#include "DerivedDataBuildKey.h"
#include "DerivedDataRequestTypes.h"
#include "Features/IModularFeature.h"
#include "IO/IoHash.h"
#include "Misc/NotNull.h"
#include "Misc/OptionalFwd.h"
#include "Serialization/CompactBinary.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

#define UE_API DERIVEDDATACACHE_API

class FCompressedBuffer;
struct FGuid;

template <typename FuncType> class TFunctionRef;
template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FBuildAction; }
namespace UE::DerivedData { class FBuildPolicy; }
namespace UE::DerivedData { class FBuildWorker; }
namespace UE::DerivedData { class FOptionalBuildInputs; }
namespace UE::DerivedData { class FOptionalBuildOutput; }
namespace UE::DerivedData { class IBuild; }
namespace UE::DerivedData { class IBuildWorkerRegistry; }
namespace UE::DerivedData { class IBuildWorkerResolver; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::DerivedData { struct FBuildWorkerActionCompleteParams; }
namespace UE::DerivedData { struct FBuildWorkerResolvedParams; }
namespace UE::DerivedData::Private { class FBuildWorkerBuilderInternal; }

namespace UE::DerivedData::Private
{

class IBuildWorkerBuilderInternal
{
public:
	virtual ~IBuildWorkerBuilderInternal() = default;
	virtual void SetName(FUtf8StringView Name) = 0;
	virtual void SetPath(FUtf8StringView Path) = 0;
	virtual void SetHostPlatform(FUtf8StringView Name) = 0;
	virtual void SetBuildSystemVersion(const FGuid& Version) = 0;
	virtual void AddFunction(FUtf8StringView Name, const FGuid& Version) = 0;
	virtual void AddDirectory(FUtf8StringView Path) = 0;
	virtual void AddFile(FUtf8StringView Path, const FCompressedBuffer& Data) = 0;
	virtual void AddExecutable(FUtf8StringView Path, const FCompressedBuffer& Data) = 0;
	virtual void SetEnvironment(FUtf8StringView Name, FUtf8StringView Value) = 0;
	virtual FBuildWorker Build(FStringView PackagePath) = 0;
};

} // UE::DerivedData::Private

namespace UE::DerivedData
{

using FOnBuildWorkerActionComplete = TUniqueFunction<void (FBuildWorkerActionCompleteParams&& Params)>;
using FOnBuildWorkerResolved = TUniqueFunction<void (FBuildWorkerResolvedParams&& Params)>;

/** A key that uniquely identifies a build worker. */
struct FBuildWorkerKey final
{
	FIoHash Hash;

	friend inline bool operator==(const FBuildWorkerKey& A, const FBuildWorkerKey& B)
	{
		return A.Hash == B.Hash;
	}

	friend inline bool operator<(const FBuildWorkerKey& A, const FBuildWorkerKey& B)
	{
		return A.Hash < B.Hash;
	}

	friend inline uint32 GetTypeHash(const FBuildWorkerKey& Key)
	{
		return GetTypeHashHelper(Key.Hash);
	}

	template <typename CharType>
	friend inline TStringBuilderBase<CharType>& operator<<(TStringBuilderBase<CharType>& Builder, const FBuildWorkerKey& Key)
	{
		return Builder << ANSITEXTVIEW("BuildWorker/") << Key.Hash;
	}
};

/**
 * A build worker is a reference to an immutable build worker program and its dependencies.
 *
 * The purpose of a build worker is to capture which build functions a program contains and refer
 * to everything required to execute the program, including files and environment.
 *
 * The key for the worker uniquely identifies the worker using a hash of the serialized compact
 * binary representation of the worker.
 *
 * A build worker resolver is used to resolve file hashes to the referenced file data.
 */
class FBuildWorker final
{
public:
	[[nodiscard]] inline FBuildWorkerKey GetKey() const
	{
		return Key;
	}

	[[nodiscard]] UE_API FUtf8StringView GetName() const;
	[[nodiscard]] UE_API FUtf8StringView GetPath() const;
	[[nodiscard]] UE_API FUtf8StringView GetHostPlatform() const;
	[[nodiscard]] UE_API FGuid GetBuildSystemVersion() const;

	UE_API void IterateFunctions(TFunctionRef<void (FUtf8StringView Name, const FGuid& Version)> Visitor) const;
	UE_API void IterateDirectories(TFunctionRef<void (FUtf8StringView Path)> Visitor) const;
	UE_API void IterateFiles(TFunctionRef<void (FUtf8StringView Path, const FIoHash& RawHash, uint64 RawSize)> Visitor) const;
	UE_API void IterateExecutables(TFunctionRef<void (FUtf8StringView Path, const FIoHash& RawHash, uint64 RawSize)> Visitor) const;
	UE_API void IterateEnvironment(TFunctionRef<void (FUtf8StringView Name, FUtf8StringView Value)> Visitor) const;

	/** Resolves files and executables referenced by this build worker, by raw hash. */
	UE_API void Resolve(TConstArrayView<FIoHash> RawHashes, IRequestOwner& Owner, FOnBuildWorkerResolved&& OnResolved) const;

	/** Extracts the build worker into a directory, which must be empty. */
	UE_API bool Extract(FStringView OutputPath) const;

	/** Saves the build worker to a compact binary object, excluding file data. */
	[[nodiscard]] inline const FCbObject& Save() const
	{
		return Object;
	}

	/** Path to the package that this worker was loaded from. Empty when not loaded from a package. */
	[[nodiscard]] UE_API FStringView GetPackagePath() const;

	/** Loads a build worker from a package created by FBuildWorkerBuilder::Build. */
	[[nodiscard]] UE_API static TOptional<FBuildWorker> Load(FStringView PackagePath);

	/**
	 * Loads a build worker from a compact binary object.
	 *
	 * @param Object     An object saved from a build worker. Holds a reference and is cloned if not owned.
	 * @param Resolver   A resolver used to find file data referenced by the build worker.
	 */
	[[nodiscard]] UE_API static FBuildWorker Load(FCbObject Object, TNotNull<TUniquePtr<IBuildWorkerResolver>> Resolver);

	constexpr static bool bHasIntrusiveUnsetOptionalState = true;
	using IntrusiveUnsetOptionalStateType = FBuildWorker;
	[[nodiscard]] inline explicit FBuildWorker(FIntrusiveUnsetOptionalState) {}
	[[nodiscard]] inline bool operator==(FIntrusiveUnsetOptionalState) const { return !Resolver; }

private:
	FBuildWorker() = default;

	FCbObject Object;
	TUniquePtr<IBuildWorkerResolver> Resolver;
	FBuildWorkerKey Key;
};

/** A builder to construct a build worker. */
class FBuildWorkerBuilder final
{
public:
	[[nodiscard]] UE_API FBuildWorkerBuilder();

	inline void SetName(FUtf8StringView Name)
	{
		Builder->SetName(Name);
	}

	inline void SetPath(FUtf8StringView Path)
	{
		Builder->SetPath(Path);
	}

	inline void SetHostPlatform(FUtf8StringView Name)
	{
		Builder->SetHostPlatform(Name);
	}

	inline void SetBuildSystemVersion(const FGuid& Version)
	{
		Builder->SetBuildSystemVersion(Version);
	}

	inline void AddFunction(FUtf8StringView Name, const FGuid& Version)
	{
		Builder->AddFunction(Name, Version);
	}

	inline void AddDirectory(FUtf8StringView Path)
	{
		Builder->AddDirectory(Path);
	}

	inline void AddFile(FUtf8StringView Path, const FCompressedBuffer& Data)
	{
		Builder->AddFile(Path, Data);
	}

	inline void AddExecutable(FUtf8StringView Path, const FCompressedBuffer& Data)
	{
		Builder->AddExecutable(Path, Data);
	}

	inline void SetEnvironment(FUtf8StringView Name, FUtf8StringView Value)
	{
		Builder->SetEnvironment(Name, Value);
	}

	[[nodiscard]] inline FBuildWorker Build(FStringView PackagePath)
	{
		return Builder->Build(PackagePath);
	}

private:
	TUniquePtr<Private::IBuildWorkerBuilderInternal> Builder;
};

/** Interface to resolve file data for a build worker. */
class IBuildWorkerResolver
{
public:
	virtual ~IBuildWorkerResolver() = default;

	/**
	 * Resolve the compressed data for the raw hashes.
	 *
	 * @param RawHashes   The raw hashes of the data to resolve.
	 * @param Owner   Owner provides priority and manages requests when resolving is asynchronous.
	 * @param OnResolved   Called exactly once when data is resolved. May return partial results if Status is Error.
	 */
	virtual void Resolve(TConstArrayView<FIoHash> RawHashes, IRequestOwner& Owner, FOnBuildWorkerResolved&& OnResolved) = 0;

	/** Returns the path to the package that this is resolving from, or empty if not backed by a package. */
	[[nodiscard]] virtual FStringView GetPackagePath() const = 0;
};

/** Interface to create and register build workers with the build worker registry. */
class IBuildWorkerFactory : public IModularFeature
{
public:
	static inline const FLazyName FeatureName{"BuildWorkerFactory"};

	virtual ~IBuildWorkerFactory() = default;

	/**
	 * Called by the build system when it is created, or when the factory is registered later.
	 *
	 * Async worker creation is allowed and must be finished before AbortCreateWorkers() returns.
	 */
	virtual void CreateWorkers(IBuildWorkerRegistry& WorkerRegistry) = 0;

	/**
	 * Called by the build system when it is about to be destroyed.
	 *
	 * Any async worker creation must be completed or aborted before returning from this function.
	 */
	virtual void AbortCreateWorkers() = 0;
};

/** Interface to execute a build action using a build worker. */
class IBuildWorkerExecutor : public IModularFeature
{
public:
	static inline const FLazyName FeatureName{"BuildWorkerExecutor"};

	virtual ~IBuildWorkerExecutor() = default;

	virtual void Build(
		const FBuildAction& Action,
		const FOptionalBuildInputs& Inputs,
		const FBuildPolicy& Policy,
		const FBuildWorker& Worker,
		IBuild& BuildSystem,
		IRequestOwner& Owner,
		FOnBuildWorkerActionComplete&& OnComplete) = 0;

	virtual TConstArrayView<FUtf8StringView> GetHostPlatforms() const = 0;
};

struct FBuildWorkerActionCompleteParams final
{
	FBuildActionKey Key;
	FOptionalBuildOutput&& Output;
	TConstArrayView<FUtf8StringView> MissingInputs;
	EStatus Status = EStatus::Error;
};

struct FBuildWorkerResolvedParams final
{
	TConstArrayView<FCompressedBuffer> Files;
	EStatus Status = EStatus::Error;
};

} // UE::DerivedData

#undef UE_API
