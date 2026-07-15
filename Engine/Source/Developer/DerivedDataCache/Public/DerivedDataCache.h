// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Containers/SharedString.h"
#include "Containers/StringFwd.h"
#include "CoreTypes.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCachePolicy.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "IO/IoHash.h"
#include "Math/NumericLimits.h"
#include "Memory/SharedBuffer.h"
#include "Templates/Function.h"

#define UE_API DERIVEDDATACACHE_API

namespace UE::DerivedData { class ICacheStoreMaintainer; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::DerivedData { struct FCacheGetChunkRequest; }
namespace UE::DerivedData { struct FCacheGetChunkResponse; }
namespace UE::DerivedData { struct FCacheGetRequest; }
namespace UE::DerivedData { struct FCacheGetResponse; }
namespace UE::DerivedData { struct FCacheGetValueRequest; }
namespace UE::DerivedData { struct FCacheGetValueResponse; }
namespace UE::DerivedData { struct FCachePutRequest; }
namespace UE::DerivedData { struct FCachePutResponse; }
namespace UE::DerivedData { struct FCachePutValueRequest; }
namespace UE::DerivedData { struct FCachePutValueResponse; }

namespace UE::DerivedData
{

using FOnCachePutComplete = TUniqueFunction<void (FCachePutResponse&& Response)>;
using FOnCacheGetComplete = TUniqueFunction<void (FCacheGetResponse&& Response)>;
using FOnCachePutValueComplete = TUniqueFunction<void (FCachePutValueResponse&& Response)>;
using FOnCacheGetValueComplete = TUniqueFunction<void (FCacheGetValueResponse&& Response)>;
using FOnCacheGetChunkComplete = TUniqueFunction<void (FCacheGetChunkResponse&& Response)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Interface to the cache.
 *
 * Functions on this interface may be called from any thread.
 *
 * Requests may complete out of order relative to the order that they were requested.
 *
 * Callbacks may be called from any thread, including the calling thread, may be called from more
 * than one thread concurrently, and may be called before returning from the request function.
 */
class ICache
{
public:
	virtual ~ICache() = default;

	/**
	 * Asynchronous request to put records in the cache.
	 *
	 * @see FCachePutRequest
	 *
	 * @param Requests     Requests with the cache records to store. Records must have a key.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete = {}) = 0;

	/**
	 * Asynchronous request to get records from the cache.
	 *
	 * @see FCacheGetRequest
	 *
	 * @param Requests     Requests with the keys of the cache records to fetch.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) = 0;

	/**
	 * Asynchronous request to put values in the cache.
	 *
	 * @see FCachePutValueRequest
	 *
	 * @param Requests     Requests with the cache values to store. Requests must have a key.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete = {}) = 0;

	/**
	 * Asynchronous request to get values from the cache.
	 *
	 * @see FCacheGetValueRequest
	 *
	 * @param Requests     Requests with the keys of the cache values to fetch.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) = 0;

	/**
	 * Asynchronous request to get chunks, which are subsets of values, from records or values.
	 *
	 * @see FCacheGetChunkRequest
	 *
	 * @param Requests     Requests with the key, ID, offset, and size of each chunk to fetch.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) = 0;

	/** Returns the interface to the background cache store maintenance. */
	virtual ICacheStoreMaintainer& GetMaintainer() = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Parameters to request to put a cache record. */
struct FCachePutRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FSharedString Name;

	/** A record to store. */
	FCacheRecord Record;

	/** Flags to control the behavior of the request. See FCacheRecordPolicy. */
	FCacheRecordPolicy Policy = ECachePolicy::Default | ECachePolicy::SkipData;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;

	/** Make a default response for this request, with the provided status. */
	UE_API FCachePutResponse MakeResponse(EStatus Status) const;
};

/** Parameters for the completion callback for cache put requests. */
PRAGMA_DISABLE_DEPRECATION_WARNINGS; // Remove this when the deprecated Key is removed.
struct FCachePutResponse
{
	/** A copy of the name from the request. */
	FSharedString Name;

	/** A copy of the key from the request. */
	UE_DEPRECATED(5.8, "Access the key through Record.GetKey().")
	FCacheKey Key;

	/**
	 * A copy of the record from the request, or the conflicting record if there is one.
	 *
	 * A conflicting record is returned when the cache contains a record with the requested key
	 * that differs from the record in the request, and the request policy disallows overwrite.
	 */
	FCacheRecord Record;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS; // Remove this when the deprecated Key is removed.

/** Parameters to request to get a cache record. */
struct FCacheGetRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FSharedString Name;

	/** A key identifying the record to fetch. */
	FCacheKey Key;

	/** Flags to control the behavior of the request. See FCacheRecordPolicy. */
	FCacheRecordPolicy Policy;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;

	/** Make a default response for this request, with the provided status. */
	UE_API FCacheGetResponse MakeResponse(EStatus Status) const;
};

/** Parameters for the completion callback for cache get requests. */
struct FCacheGetResponse
{
	/** A copy of the name from the request. */
	FSharedString Name;

	/**
	 * Record for the request that completed or was canceled.
	 *
	 * The key is always populated. The remainder of the record is populated when Status is Ok.
	 *
	 * The metadata or the data for values may be skipped based on cache policy flags. Values for
	 * which data has been skipped will have a hash and size but null data.
	 */
	FCacheRecord Record;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request to put a cache value. */
struct FCachePutValueRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FSharedString Name;

	/** A key that will uniquely identify the value in the cache. */
	FCacheKey Key;

	/** A value to store. */
	FValue Value;

	/** Flags to control the behavior of the request. See ECachePolicy. */
	ECachePolicy Policy = ECachePolicy::Default | ECachePolicy::SkipData;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;

	/** Make a default response for this request, with the provided status. */
	UE_API FCachePutValueResponse MakeResponse(EStatus Status) const;
};

/** Parameters for the completion callback for cache value put requests. */
struct FCachePutValueResponse
{
	/** A copy of the name from the request. */
	FSharedString Name;

	/** A copy of the key from the request. */
	FCacheKey Key;

	/**
	 * A copy of the value from the request, or the conflicting value if there is one.
	 *
	 * A conflicting value is returned when the cache contains a value with the requested key
	 * that differs from the value in the request, and the request policy disallows overwrite.
	 */
	FValue Value;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request to get a cache value. */
struct FCacheGetValueRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FSharedString Name;

	/** A key identifying the value to fetch. */
	FCacheKey Key;

	/** Flags to control the behavior of the request. See ECachePolicy. */
	ECachePolicy Policy = ECachePolicy::Default;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;

	/** Make a default response for this request, with the provided status. */
	UE_API FCacheGetValueResponse MakeResponse(EStatus Status) const;
};

/** Parameters for the completion callback for cache value get requests. */
struct FCacheGetValueResponse
{
	/** A copy of the name from the request. */
	FSharedString Name;

	/** A copy of the key from the request. */
	FCacheKey Key;

	/**
	 * Value for the request that completed or was canceled.
	 *
	 * The data may be skipped based on cache policy flags. A value for which data has been skipped
	 * will have a hash and size but null data.
	 */
	FValue Value;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request a chunk, which is a subset of a value, from a cache record or cache value. */
struct FCacheGetChunkRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FSharedString Name;

	/** A key identifying the record or value to fetch the chunk from. */
	FCacheKey Key;

	/** An ID identifying the value to fetch, if fetching from a record, otherwise null. */
	FValueId Id;

	/** The offset into the raw bytes of the value at which to start fetching. */
	uint64 RawOffset = 0;

	/** The maximum number of raw bytes of the value to fetch, starting from the offset. */
	uint64 RawSize = MAX_uint64;

	/** The raw hash of the entire value to fetch, if available, otherwise zero. */
	FIoHash RawHash;

	/** Flags to control the behavior of the request. See ECachePolicy. */
	ECachePolicy Policy = ECachePolicy::Default;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;

	/** Make a default response for this request, with the provided status. */
	UE_API FCacheGetChunkResponse MakeResponse(EStatus Status) const;
};

/** Parameters for the completion callback for cache chunk requests. */
struct FCacheGetChunkResponse
{
	/** A copy of the name from the request. */
	FSharedString Name;

	/** A copy of the key from the request. */
	FCacheKey Key;

	/** A copy of the ID from the request. */
	FValueId Id;

	/** A copy of the offset from the request. */
	uint64 RawOffset = 0;

	/** The size, in bytes, of the subset of the value that was fetched, if any. */
	uint64 RawSize = 0;

	/** The hash of the entire value, even if only a subset was fetched. */
	FIoHash RawHash;

	/** Data for the subset of the value that was fetched when Status is Ok, otherwise null. */
	FSharedBuffer RawData;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Returns a reference to the cache. Asserts if not available. */
UE_API ICache& GetCache();

/** Returns a pointer to the cache. Null if not available or not created. */
UE_API ICache* TryGetCache();

/**
 * Creates a cache from a graph name or graph config.
 *
 * Must be called from the main thread.
 * Destroy the cache with operator delete. Consider using TUniquePtr.
 *
 * @param GraphNameOrConfig   Name of the graph in [DerivedDataCacheGraphs] or a graph config.
 * @return A pointer to the new cache, or null if it failed to create.
 */
UE_INTERNAL UE_API ICache* CreateCache(FStringView GraphNameOrConfig);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData

#undef UE_API

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_8
#include "DerivedDataSerialization.h"
#endif
