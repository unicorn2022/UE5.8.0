// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Mutex.h"
#include "CoreTypes.h"
#include "DerivedDataCache.h"
#include "Misc/NotNull.h"
#include "Templates/PimplPtr.h"
#include "Templates/UniquePtr.h"

#define UE_API DERIVEDDATACACHE_API

class FArchive;
class FCbObjectView;
class FCbWriter;

namespace UE::DerivedData { class FCacheKeyFilter; }
namespace UE::DerivedData { class FCacheMethodFilter; }
namespace UE::DerivedData { class ICacheStoreOwner; }
namespace UE::DerivedData { class ILegacyCacheStore; }
namespace UE::DerivedData { enum class ECachePolicy : uint32; }
namespace UE::DerivedData { enum class EPriority : uint8; }

namespace UE::DerivedData
{

class ICacheReplayWriter
{
public:
	UE_API void Write(TConstArrayView<FCachePutRequest> Requests, TConstArrayView<FCachePutResponse> Responses, EPriority Priority);
	UE_API void Write(TConstArrayView<FCacheGetRequest> Requests, TConstArrayView<FCacheGetResponse> Responses, EPriority Priority);
	UE_API void Write(TConstArrayView<FCachePutValueRequest> Requests, TConstArrayView<FCachePutValueResponse> Responses, EPriority Priority);
	UE_API void Write(TConstArrayView<FCacheGetValueRequest> Requests, TConstArrayView<FCacheGetValueResponse> Responses, EPriority Priority);
	UE_API void Write(TConstArrayView<FCacheGetChunkRequest> Requests, TConstArrayView<FCacheGetChunkResponse> Responses, EPriority Priority);

	virtual ~ICacheReplayWriter() = default;

	// Do not call directly unless forwarding from another ICacheReplayWriter.
	virtual void SerializeBatch(const FCbWriter& Batch) = 0;
};

class ICacheReplayReader
{
public:
	virtual void Read(TConstArrayView<FCachePutRequest> Requests, TConstArrayView<FCachePutResponse> Responses, EPriority Priority) = 0;
	virtual void Read(TConstArrayView<FCacheGetRequest> Requests, TConstArrayView<FCacheGetResponse> Responses, EPriority Priority) = 0;
	virtual void Read(TConstArrayView<FCachePutValueRequest> Requests, TConstArrayView<FCachePutValueResponse> Responses, EPriority Priority) = 0;
	virtual void Read(TConstArrayView<FCacheGetValueRequest> Requests, TConstArrayView<FCacheGetValueResponse> Responses, EPriority Priority) = 0;
	virtual void Read(TConstArrayView<FCacheGetChunkRequest> Requests, TConstArrayView<FCacheGetChunkResponse> Responses, EPriority Priority) = 0;

	virtual ~ICacheReplayReader() = default;

	// Do not call directly unless forwarding from another ICacheReplayReader.
	virtual bool SerializeBatch(FCbObjectView Batch);
};

/**
 * Create a cache replay writer that serializes into an archive.
 *
 * @param Ar   Archive to serialize to, and delete when the writer is deleted.
 * @param CompressionBlockSize   Power-of-two block size or zero to disable compression.
 */
UE_API TUniquePtr<ICacheReplayWriter> CreateReplayArchiveWriter(TNotNull<TUniquePtr<FArchive>> Ar, uint64 CompressionBlockSize = 256 * 1024);

/**
 * Reads a replay from an archive that was created by CreateReplayArchiveWriter.
 *
 * @return True if the replay was serialized successfully, otherwise false.
 */
UE_API bool ReadReplayFromArchive(ICacheReplayReader& Reader, FArchive& Ar);
UE_API bool ReadReplayFromFile(ICacheReplayReader& Reader, const TCHAR* Path);

/**
 * A replay reader that forwards every batch to a writer.
 */
class FCacheReplayForwardingReader final : public ICacheReplayReader
{
public:
	inline explicit FCacheReplayForwardingReader(ICacheReplayWriter& TargetWriter)
		: Writer(TargetWriter)
	{
	}

	void Read(TConstArrayView<FCachePutRequest> Requests, TConstArrayView<FCachePutResponse> Responses, EPriority Priority) final
	{
		Writer.Write(Requests, Responses, Priority);
	}

	void Read(TConstArrayView<FCacheGetRequest> Requests, TConstArrayView<FCacheGetResponse> Responses, EPriority Priority) final
	{
		Writer.Write(Requests, Responses, Priority);
	}

	void Read(TConstArrayView<FCachePutValueRequest> Requests, TConstArrayView<FCachePutValueResponse> Responses, EPriority Priority) final
	{
		Writer.Write(Requests, Responses, Priority);
	}

	void Read(TConstArrayView<FCacheGetValueRequest> Requests, TConstArrayView<FCacheGetValueResponse> Responses, EPriority Priority) final
	{
		Writer.Write(Requests, Responses, Priority);
	}

	void Read(TConstArrayView<FCacheGetChunkRequest> Requests, TConstArrayView<FCacheGetChunkResponse> Responses, EPriority Priority) final
	{
		Writer.Write(Requests, Responses, Priority);
	}

private:
	ICacheReplayWriter& Writer;
};

class FCacheReplayReader
{
public:
	static constexpr uint64 DefaultScratchSize = 1024;

	UE_API explicit FCacheReplayReader(ILegacyCacheStore* TargetCache);

	UE_API void SetKeyFilter(FCacheKeyFilter KeyFilter);
	UE_API void SetMethodFilter(FCacheMethodFilter MethodFilter);
	UE_API void SetPolicyTransform(ECachePolicy AddFlags, ECachePolicy RemoveFlags);
	UE_API void SetPriorityOverride(EPriority Priority);

	UE_API void ReadFromFileAsync(const TCHAR* ReplayPath, ICacheStoreOwner& Owner, uint64 ScratchSize = DefaultScratchSize);
	UE_API bool ReadFromFile(const TCHAR* ReplayPath, uint64 ScratchSize = DefaultScratchSize);
	UE_API bool ReadFromArchive(FArchive& ReplayAr, uint64 ScratchSize = DefaultScratchSize);
	UE_API bool ReadFromObject(FCbObjectView Object);

	UE_API void WaitForAsyncReads();

private:
	class FState;
	TPimplPtr<FState> State;
};

} // UE::DerivedData

#undef UE_API
