// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheReplay.h"

#include "Algo/AnyOf.h"
#include "Async/UniqueLock.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/StringView.h"
#include "CoreGlobals.h"
#include "DerivedDataCacheKeyFilter.h"
#include "DerivedDataCacheMethod.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataLegacyCacheStore.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataSerialization.h"
#include "DerivedDataValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Optional.h"
#include "Misc/Parse.h"
#include "Misc/Zip.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/MemoryReader.h"
#include "String/Find.h"
#include "Tasks/Pipe.h"
#include "Tasks/Task.h"
#include "Templates/Invoke.h"

namespace UE::DerivedData
{

static constexpr uint64 GCacheReplayCompressionBlockSize = 256 * 1024;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheStoreReplayWriter final : public ICacheReplayWriter
{
public:
	explicit FCacheStoreReplayWriter(const TCHAR* Path, uint64 CompressionBlockSize)
	{
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(Path, FILEWRITE_NoFail));
		Writer = CreateReplayArchiveWriter(MoveTemp(Ar), CompressionBlockSize);
	}

private:
	void SerializeBatch(const FCbWriter& Batch) final
	{
		if (UE_LOG_ACTIVE(LogDerivedDataCache, Verbose))
		{
			TStringBuilder<1024> Json;
			CompactBinaryToCompactJson(Batch.Save().AsObject(), Json);
			UE_LOGF(LogDerivedDataCache, Verbose, "Replay: %ls", *Json);
		}

		Writer->SerializeBatch(Batch);
	}

	TUniquePtr<ICacheReplayWriter> Writer;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheStoreReplay final : public ILegacyCacheStore
{
public:
	FCacheStoreReplay(
		ILegacyCacheStore* InnerCache,
		FCacheKeyFilter KeyFilter,
		FCacheMethodFilter MethodFilter,
		FString&& ReplayPath,
		uint64 CompressionBlockSize = 0);

	~FCacheStoreReplay();

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final;
	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final;
	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final;
	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final;
	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final;

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final
	{
		InnerCache->LegacyStats(OutNode);
	}

	void SetReader(FCacheReplayReader&& Reader)
	{
		ReplayReader = MoveTemp(Reader);
	}

	ICacheStore* GetInnerCache() const
	{
		return InnerCache;
	}

	template <typename RequestType>
	void SerializeRequests(
		TConstArrayView<RequestType> Requests,
		TConstArrayView<TCacheResponseFor<RequestType>> Responses,
		ECacheMethod Method,
		EPriority Priority);

private:
	void WriteBinaryToArchive(const FCompositeBuffer& RawBinary);
	void WriteToArchive(FCbWriter& Writer);
	void FlushToArchive();

	ILegacyCacheStore* InnerCache;
	FCacheKeyFilter KeyFilter;
	FCacheMethodFilter MethodFilter;
	FString ReplayPath;
	TUniquePtr<FCacheStoreReplayWriter> ReplayWriter;
	TOptional<FCacheReplayReader> ReplayReader;

#if WITH_EDITOR
private:
	// Support for multi-process replay merging.

	void WorkerCreated(const FMultiprocessCreatedContext& Context);
	void WorkerDetached(const FMultiprocessDetachedContext& Context);
	void MergeWorkerReplays();

	FDelegateHandle WorkerCreatedHandle;
	FDelegateHandle WorkerDetachedHandle;
	/** Value tracks whether the worker is attached. */
	TMap<int32, bool> WorkerIdToState;
#endif // WITH_EDITOR
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename RequestType>
struct TCacheStoreReplayBatch
{
	using FRequest = RequestType;
	using FResponse = TCacheResponseFor<FRequest>;
	using FOnComplete = TCacheOnCompleteFor<FRequest>;

	ICacheReplayWriter& ReplayWriter;
	const FCacheKeyFilter& KeyFilter;
	const TArray<FRequest> Requests;
	TArray<FResponse> Responses;
	FOnComplete OnComplete;
	std::atomic<int32> ActiveRequests;
	EPriority Priority;

	explicit TCacheStoreReplayBatch(
		ICacheStore& InCacheStore,
		ICacheReplayWriter& InReplayWriter,
		const FCacheKeyFilter& InKeyFilter,
		TConstArrayView<FRequest> InRequests,
		IRequestOwner& Owner,
		FOnComplete&& InOnComplete)
		: ReplayWriter(InReplayWriter)
		, KeyFilter(InKeyFilter)
		, Requests(InRequests)
		, OnComplete(MoveTemp(InOnComplete))
		, ActiveRequests(Requests.Num())
		, Priority(Owner.GetPriority())
	{
		Responses.SetNumUninitialized(ActiveRequests);

		TArray<FRequest, TInlineAllocator<16>> ModifiedRequests(Requests);
		int32 Index = 0;
		for (FRequest& Request : ModifiedRequests)
		{
			Request.UserData = Index++;
		}

		(InCacheStore.*CacheStoreFunctionFor<FRequest>)(ModifiedRequests, Owner, [this](FResponse&& Response)
		{
			const int32 Index = int32(Response.UserData);
			Response.UserData = Requests[Index].UserData;
			new(&Responses[Index]) FResponse(Response);

			OnComplete(MoveTemp(Response));

			if (ActiveRequests.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				TArray<FRequest, TInlineAllocator<16>> ReplayRequests;
				TArray<FResponse, TInlineAllocator<16>> ReplayResponses;
				for (const auto& Pair : Zip(Requests, Responses))
				{
					if (KeyFilter.IsMatch(GetCacheKey(Pair.template Get<0>())))
					{
						ReplayRequests.Add(Pair.template Get<0>());
						ReplayResponses.Add(Pair.template Get<1>());
					}
				}
				ReplayWriter.Write(ReplayRequests, ReplayResponses, Priority);
				delete this;
			}
		});
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheStoreReplay::FCacheStoreReplay(
	ILegacyCacheStore* InInnerCache,
	FCacheKeyFilter InKeyFilter,
	FCacheMethodFilter InMethodFilter,
	FString&& InReplayPath,
	uint64 CompressionBlockSize)
	: InnerCache(InInnerCache)
	, KeyFilter(MoveTemp(InKeyFilter))
	, MethodFilter(MoveTemp(InMethodFilter))
	, ReplayPath(MoveTemp(InReplayPath))
{
	if (!ReplayPath.IsEmpty())
	{
	#if WITH_EDITOR
		if (const int32 WorkerId = UE::GetMultiprocessId())
		{
			ReplayPath.Appendf(TEXT(".worker%d.tmp"), WorkerId);
		}
		else
		{
			FCoreDelegates::OnMultiprocessWorkerCreated.AddRaw(this, &FCacheStoreReplay::WorkerCreated);
			FCoreDelegates::OnMultiprocessWorkerDetached.AddRaw(this, &FCacheStoreReplay::WorkerDetached);
		}
	#endif // WITH_EDITOR
		ReplayWriter = MakeUnique<FCacheStoreReplayWriter>(*ReplayPath, CompressionBlockSize);
		UE_LOGFMT(LogDerivedDataCache, Display, "Replay: Saving cache replay to '{ReplayPath}'", ReplayPath);
	}
}

FCacheStoreReplay::~FCacheStoreReplay()
{
	delete InnerCache;
	
#if WITH_EDITOR
	if (!UE::GetMultiprocessId())
	{
		FCoreDelegates::OnMultiprocessWorkerDetached.Remove(WorkerDetachedHandle);
		FCoreDelegates::OnMultiprocessWorkerCreated.Remove(WorkerCreatedHandle);
		MergeWorkerReplays();
	}
#endif // WITH_EDITOR
}

void FCacheStoreReplay::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	const auto IsKeyMatch = [this](const FCachePutRequest& Request) { return KeyFilter.IsMatch(Request.Record.GetKey()); };
	if (ReplayWriter && MethodFilter.IsMatch(ECacheMethod::Put) && Algo::AnyOf(Requests, IsKeyMatch))
	{
		new TCacheStoreReplayBatch(*InnerCache, *ReplayWriter, KeyFilter, Requests, Owner, MoveTemp(OnComplete));
	}
	else
	{
		InnerCache->Put(Requests, Owner, MoveTemp(OnComplete));
	}
}

void FCacheStoreReplay::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	const auto IsKeyMatch = [this](const FCacheGetRequest& Request) { return KeyFilter.IsMatch(Request.Key); };
	if (ReplayWriter && MethodFilter.IsMatch(ECacheMethod::Get) && Algo::AnyOf(Requests, IsKeyMatch))
	{
		new TCacheStoreReplayBatch(*InnerCache, *ReplayWriter, KeyFilter, Requests, Owner, MoveTemp(OnComplete));
	}
	else
	{
		InnerCache->Get(Requests, Owner, MoveTemp(OnComplete));
	}
}

void FCacheStoreReplay::PutValue(
	const TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	const auto IsKeyMatch = [this](const FCachePutValueRequest& Request) { return KeyFilter.IsMatch(Request.Key); };
	if (ReplayWriter && MethodFilter.IsMatch(ECacheMethod::PutValue) && Algo::AnyOf(Requests, IsKeyMatch))
	{
		new TCacheStoreReplayBatch(*InnerCache, *ReplayWriter, KeyFilter, Requests, Owner, MoveTemp(OnComplete));
	}
	else
	{
		InnerCache->PutValue(Requests, Owner, MoveTemp(OnComplete));
	}
}

void FCacheStoreReplay::GetValue(
	const TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	const auto IsKeyMatch = [this](const FCacheGetValueRequest& Request) { return KeyFilter.IsMatch(Request.Key); };
	if (ReplayWriter && MethodFilter.IsMatch(ECacheMethod::GetValue) && Algo::AnyOf(Requests, IsKeyMatch))
	{
		new TCacheStoreReplayBatch(*InnerCache, *ReplayWriter, KeyFilter, Requests, Owner, MoveTemp(OnComplete));
	}
	else
	{
		InnerCache->GetValue(Requests, Owner, MoveTemp(OnComplete));
	}
}

void FCacheStoreReplay::GetChunks(
	const TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	const auto IsKeyMatch = [this](const FCacheGetChunkRequest& Request) { return KeyFilter.IsMatch(Request.Key); };
	if (ReplayWriter && MethodFilter.IsMatch(ECacheMethod::GetChunks) && Algo::AnyOf(Requests, IsKeyMatch))
	{
		new TCacheStoreReplayBatch(*InnerCache, *ReplayWriter, KeyFilter, Requests, Owner, MoveTemp(OnComplete));
	}
	else
	{
		InnerCache->GetChunks(Requests, Owner, MoveTemp(OnComplete));
	}
}

#if WITH_EDITOR
void FCacheStoreReplay::WorkerCreated(const FMultiprocessCreatedContext& Context)
{
	WorkerIdToState.FindOrAdd(Context.Id) = true;
}

void FCacheStoreReplay::WorkerDetached(const FMultiprocessDetachedContext& Context)
{
	if (!Context.bAbnormalDetach)
	{
		WorkerIdToState.FindOrAdd(Context.Id) = false;
	}
}

void FCacheStoreReplay::MergeWorkerReplays()
{
	if (WorkerIdToState.IsEmpty())
	{
		return;
	}
	FCacheReplayForwardingReader ForwardReplay(*ReplayWriter);
	for (const TPair<int32, bool> Worker : WorkerIdToState)
	{
		const int32 WorkerId = Worker.Key;
		FString WorkerReplayPath = ReplayPath;
		WorkerReplayPath.Appendf(TEXT(".worker%d.tmp"), WorkerId);
		if (Worker.Value)
		{
			UE_LOGF(LogDerivedDataCache, Warning,
				"Replay: Skipped replay file '%ls' because its worker has not detached.", *WorkerReplayPath);
			continue;
		}
		if (ReadReplayFromFile(ForwardReplay, *WorkerReplayPath))
		{
			IFileManager::Get().Delete(*WorkerReplayPath);
			UE_LOGF(LogDerivedDataCache, Display,
				"Replay: Merged replay file '%ls' from a worker process.", *WorkerReplayPath);
		}
	}
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename RequestType>
static void SaveBatchToCompactBinary(
	FCbWriter& Writer,
	const TConstArrayView<RequestType> Requests,
	const TConstArrayView<TCacheResponseFor<RequestType>> Responses,
	const ECacheMethod Method,
	const EPriority Priority)
{
	check(Requests.Num() == Responses.Num());
	Writer.BeginObject();
	Writer << ANSITEXTVIEW("Method") << Method;
	if (Priority != EPriority::Normal)
	{
		Writer << ANSITEXTVIEW("Priority") << Priority;
	}
	Writer.BeginArray(ANSITEXTVIEW("Requests"));
	int32 Index = -1;
	for (const RequestType& Request : Requests)
	{
		++Index;
		SaveToCompactBinary(Writer, Request, &Responses.GetData()[Index]);
	}
	Writer.EndArray();
	Writer.EndObject();
}

void ICacheReplayWriter::Write(TConstArrayView<FCachePutRequest> Requests, TConstArrayView<FCachePutResponse> Responses, EPriority Priority)
{
	TCbWriter<512> Writer;
	SaveBatchToCompactBinary(Writer, Requests, Responses, ECacheMethod::Put, Priority);
	SerializeBatch(Writer);
}

void ICacheReplayWriter::Write(TConstArrayView<FCacheGetRequest> Requests, TConstArrayView<FCacheGetResponse> Responses, EPriority Priority)
{
	TCbWriter<512> Writer;
	SaveBatchToCompactBinary(Writer, Requests, Responses, ECacheMethod::Get, Priority);
	SerializeBatch(Writer);
}

void ICacheReplayWriter::Write(TConstArrayView<FCachePutValueRequest> Requests, TConstArrayView<FCachePutValueResponse> Responses, EPriority Priority)
{
	TCbWriter<512> Writer;
	SaveBatchToCompactBinary(Writer, Requests, Responses, ECacheMethod::PutValue, Priority);
	SerializeBatch(Writer);
}

void ICacheReplayWriter::Write(TConstArrayView<FCacheGetValueRequest> Requests, TConstArrayView<FCacheGetValueResponse> Responses, EPriority Priority)
{
	TCbWriter<512> Writer;
	SaveBatchToCompactBinary(Writer, Requests, Responses, ECacheMethod::GetValue, Priority);
	SerializeBatch(Writer);
}

void ICacheReplayWriter::Write(TConstArrayView<FCacheGetChunkRequest> Requests, TConstArrayView<FCacheGetChunkResponse> Responses, EPriority Priority)
{
	TCbWriter<512> Writer;
	SaveBatchToCompactBinary(Writer, Requests, Responses, ECacheMethod::GetChunks, Priority);
	SerializeBatch(Writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename RequestType>
static bool LoadBatchArraysFromCompactBinary(
	const FCbFieldView Field,
	TArray<RequestType, TInlineAllocator<16>>& OutRequests,
	TArray<TCacheResponseFor<RequestType>, TInlineAllocator<16>>& OutResponses)
{
	using ResponseType = TCacheResponseFor<RequestType>;
	if constexpr (!std::is_default_constructible_v<RequestType> && !std::is_default_constructible_v<ResponseType>)
	{
		TOptional<RequestType> Request;
		TOptional<ResponseType> Response;
		if (LoadFromCompactBinary(Field, Request, &Response))
		{
			OutRequests.Emplace(MoveTemp(Request.GetValue()));
			OutResponses.Emplace(MoveTemp(Response.GetValue()));
			return true;
		}
		return false;
	}
	else if constexpr (!std::is_default_constructible_v<RequestType>)
	{
		TOptional<RequestType> Request;
		ResponseType& Response = OutResponses.AddDefaulted_GetRef();
		if (LoadFromCompactBinary(Field, Request, &Response))
		{
			OutRequests.Emplace(MoveTemp(Request.GetValue()));
			return true;
		}
		return false;
	}
	else if constexpr (!std::is_default_constructible_v<ResponseType>)
	{
		RequestType& Request = OutRequests.AddDefaulted_GetRef();
		TOptional<ResponseType> Response;
		if (LoadFromCompactBinary(Field, Request, &Response))
		{
			OutResponses.Emplace(MoveTemp(Response.GetValue()));
			return true;
		}
		return false;
	}
	else
	{
		RequestType& Request = OutRequests.AddDefaulted_GetRef();
		ResponseType& Response = OutResponses.AddDefaulted_GetRef();
		return LoadFromCompactBinary(Field, Request, &Response);
	}
}

template <typename RequestType>
static bool LoadBatchFromCompactBinary(ICacheReplayReader& Reader, const FCbObjectView Batch)
{
	EPriority Priority;
	LoadFromCompactBinary(Batch[ANSITEXTVIEW("Priority")], Priority, EPriority::Normal);

	TArray<RequestType, TInlineAllocator<16>> Requests;
	TArray<TCacheResponseFor<RequestType>, TInlineAllocator<16>> Responses;
	{
		const FCbArrayView Array = Batch[ANSITEXTVIEW("Requests")].AsArrayView();
		const int32 BatchSize = IntCastChecked<int32>(Array.Num());
		Requests.Reserve(BatchSize);
		Responses.Reserve(BatchSize);
		for (FCbFieldView Field : Array)
		{
			if (!LoadBatchArraysFromCompactBinary<RequestType>(Field, Requests, Responses))
			{
				return false;
			}
		}
	}

	Reader.Read(Requests, Responses, Priority);
	return true;
}

bool ICacheReplayReader::SerializeBatch(const FCbObjectView Batch)
{
	ECacheMethod Method{};
	if (!LoadFromCompactBinary(Batch[ANSITEXTVIEW("Method")], Method))
	{
		return false;
	}

	switch (Method)
	{
	case ECacheMethod::Put:
		return LoadBatchFromCompactBinary<FCachePutRequest>(*this, Batch);
	case ECacheMethod::Get:
		return LoadBatchFromCompactBinary<FCacheGetRequest>(*this, Batch);
	case ECacheMethod::PutValue:
		return LoadBatchFromCompactBinary<FCachePutValueRequest>(*this, Batch);
	case ECacheMethod::GetValue:
		return LoadBatchFromCompactBinary<FCacheGetValueRequest>(*this, Batch);
	case ECacheMethod::GetChunks:
		return LoadBatchFromCompactBinary<FCacheGetChunkRequest>(*this, Batch);
	default:
		return false;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheReplayArchiveWriter final : public ICacheReplayWriter
{
public:
	explicit FCacheReplayArchiveWriter(TNotNull<TUniquePtr<FArchive>> Ar, uint64 CompressionBlockSize = 256 * 1024);
	~FCacheReplayArchiveWriter() final;

	FCacheReplayArchiveWriter(const FCacheReplayArchiveWriter&) = delete;
	FCacheReplayArchiveWriter& operator=(const FCacheReplayArchiveWriter&) = delete;

	void SerializeBatch(const FCbWriter& Batch) final;

private:
	void WriteCompressedBinaryToArchive(const FSharedBuffer& RawBinary);
	void FlushCompressedBinaryToArchive();

	TUniquePtr<FArchive> Ar;
	FUniqueBuffer RawBlock;
	FMutableMemoryView RawBlockTail;
	FMutex Mutex;
};

FCacheReplayArchiveWriter::FCacheReplayArchiveWriter(TNotNull<TUniquePtr<FArchive>> InAr, uint64 InCompressionBlockSize)
	: Ar(MoveTemp(InAr))
{
	if (InCompressionBlockSize)
	{
		RawBlock = FUniqueBuffer::Alloc(InCompressionBlockSize);
		RawBlockTail = RawBlock;
	}
}

FCacheReplayArchiveWriter::~FCacheReplayArchiveWriter()
{
	FlushCompressedBinaryToArchive();
}

void FCacheReplayArchiveWriter::SerializeBatch(const FCbWriter& Batch)
{
	TUniqueLock Lock(Mutex);

	if (!RawBlock)
	{
		// Write Batch directly to the archive when not using compression.
		Batch.Save(*Ar);
		return;
	}

	const uint64 SaveSize = Batch.GetSaveSize();
	if (RawBlockTail.GetSize() < SaveSize)
	{
		// Flush buffered batches because this batch exceeds the remaining buffer size.
		FlushCompressedBinaryToArchive();
	}

	if (RawBlockTail.GetSize() < SaveSize)
	{
		// Write Batch into its own compressed block because it exceeds the block size.
		WriteCompressedBinaryToArchive(Batch.Save().AsObject().GetBuffer().ToShared());
	}
	else
	{
		// Write Batch into the buffer to be compressed with a block of batches.
		Batch.Save(RawBlockTail.Left(SaveSize));
		RawBlockTail += SaveSize;
	}
}

void FCacheReplayArchiveWriter::WriteCompressedBinaryToArchive(const FSharedBuffer& RawBinary)
{
	const FValue CompressedBinary = FValue::Compress(RawBinary, RawBlock.GetSize());
	TCbWriter<64> BinaryWriter;
	BinaryWriter.AddBinary(CompressedBinary.GetData().GetCompressed());
	BinaryWriter.Save(*Ar);
}

void FCacheReplayArchiveWriter::FlushCompressedBinaryToArchive()
{
	const FSharedBuffer RawBlockHead = FSharedBuffer::MakeView(RawBlock.GetView().LeftChop(RawBlockTail.GetSize()));
	if (RawBlockHead.GetSize() > 0)
	{
		WriteCompressedBinaryToArchive(RawBlockHead);
		RawBlockTail = RawBlock;
	}
}

TUniquePtr<ICacheReplayWriter> CreateReplayArchiveWriter(TNotNull<TUniquePtr<FArchive>> Ar, uint64 CompressionBlockSize)
{
	return MakeUnique<FCacheReplayArchiveWriter>(MoveTemp(Ar), CompressionBlockSize);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ReadReplayFromArchive(ICacheReplayReader& Reader, FArchive& Ar, FUniqueBuffer& Scratch, ECbValidateMode ValidateMode)
{
	const auto Alloc = [&Scratch](const uint64 Size) -> FMutableMemoryView
	{
		if (UNLIKELY(Scratch.GetSize() < Size))
		{
			Scratch = FUniqueBuffer::Alloc(FMath::RoundUpToPowerOfTwo64(Size));
		}
		return Scratch.GetView().Left(Size);
	};

	// A scratch buffer for decompressed blocks of fields.
	FUniqueBuffer BlockScratch;

	bool bOk = true;
	for (int64 Offset = Ar.Tell(); Offset < Ar.TotalSize(); Offset = Ar.Tell())
	{
		FCbFieldView Field = LoadAndValidateCompactBinaryView(Ar, Alloc, ValidateMode);
		if (!Field)
		{
			UE_LOG(LogDerivedDataCache, Error,
				TEXT("Replay: Failed to load compact binary at offset %" INT64_FMT ". "
					 "Archive is at offset %" INT64_FMT " and has total size %" INT64_FMT "."),
				Offset, Ar.Tell(), Ar.TotalSize());
			return false;
		}

		// A binary field is used to store a compressed buffer containing a sequence of compact binary objects.
		if (Field.IsBinary())
		{
			FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(FSharedBuffer::MakeView(Field.AsBinaryView()));
			if (!CompressedBuffer)
			{
				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("Replay: Failed to load compressed buffer from binary field at offset %" INT64_FMT ". "
						 "Archive is at offset %" INT64_FMT " and has total size %" INT64_FMT "."),
					Offset, Ar.Tell(), Ar.TotalSize());
				bOk = false;
				continue;
			}

			const uint64 RawBlockSize = CompressedBuffer.GetRawSize();
			if (BlockScratch.GetSize() < RawBlockSize)
			{
				// Allocate the max of the raw size and the compression block size to maximize reuse of the buffer.
				ECompressedBufferCompressor Compressor;
				ECompressedBufferCompressionLevel CompressionLevel;
				uint64 CompressionBlockSize = 0;
				(void)CompressedBuffer.TryGetCompressParameters(Compressor, CompressionLevel, CompressionBlockSize);
				BlockScratch = FUniqueBuffer::Alloc(FMath::Max(RawBlockSize, CompressionBlockSize));
			}

			const FMutableMemoryView RawBlockView = BlockScratch.GetView().Left(RawBlockSize);
			if (!CompressedBuffer.TryDecompressTo(RawBlockView))
			{
				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("Replay: Failed to decompress compressed buffer from binary field at offset %" INT64_FMT ". "
						 "Archive is at offset %" INT64_FMT " and has total size %" INT64_FMT "."),
					Offset, Ar.Tell(), Ar.TotalSize());
				bOk = false;
				continue;
			}

			const FIoHash RawBlockHash = FIoHash::HashBuffer(RawBlockView);
			if (RawBlockHash != CompressedBuffer.GetRawHash())
			{
				UE_LOGFMT(LogDerivedDataCache, Warning,
					"Replay: Failed to decompress compressed buffer from binary field at offset {FieldOffset}. "
					"Decompressed buffer has raw hash {ActualHash} when {ExpectedHash} was expected. "
					"Archive is at offset {ArchiveOffset} and has total size {ArchiveSize}.",
					Offset, RawBlockHash, CompressedBuffer.GetRawHash(), Ar.Tell(), Ar.TotalSize());
				bOk = false;
				continue;
			}

			// DO NOT ACCESS Field PAST THIS POINT. The recursive call overwrites it in Scratch.
			FMemoryReaderView InnerAr(RawBlockView);
			if (!ReadReplayFromArchive(Reader, InnerAr, Scratch, ECbValidateMode::None))
			{
				bOk = false;
				continue;
			}
		}

		// An object field is used to store one batch of cache requests.
		if (Field.IsObject() && !Reader.SerializeBatch(Field.AsObjectView()))
		{
			UE_LOG(LogDerivedDataCache, Warning,
				TEXT("Replay: Failed to load cache request from object field at offset %" INT64_FMT ". "
					 "Archive is at offset %" INT64_FMT " and has total size %" INT64_FMT "."),
				Offset, Ar.Tell(), Ar.TotalSize());
			bOk = false;
			continue;
		}
	}
	return bOk;
}

bool ReadReplayFromArchive(ICacheReplayReader& Reader, FArchive& Ar)
{
	// A scratch buffer for the compact binary fields.
	FUniqueBuffer Scratch = FUniqueBuffer::Alloc(256 * 1024);
	return ReadReplayFromArchive(Reader, Ar, Scratch, ECbValidateMode::Default);
}

bool ReadReplayFromFile(ICacheReplayReader& Reader, const TCHAR* Path)
{
	if (TUniquePtr<FArchive> ReplayAr{IFileManager::Get().CreateFileReader(Path, FILEREAD_Silent)})
	{
		return ReadReplayFromArchive(Reader, *ReplayAr);
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheReplayReader::FState final : public ICacheReplayReader
{
public:
	~FState() final;

	void Read(TConstArrayView<FCachePutRequest> Requests, TConstArrayView<FCachePutResponse> Responses, EPriority Priority) final
	{
		// Replay puts as gets.
		TArray<FCacheGetRequest, TInlineAllocator<16>> GetRequests;
		GetRequests.Reserve(Requests.Num());
		for (const FCachePutRequest& Request : Requests)
		{
			GetRequests.Add({Request.Name, Request.Record.GetKey(), Request.Policy, Request.UserData});
		}
		DispatchRequests(MakeConstArrayView(GetRequests), ECacheMethod::Put, Priority);
	}

	void Read(TConstArrayView<FCacheGetRequest> Requests, TConstArrayView<FCacheGetResponse> Responses, EPriority Priority) final
	{
		DispatchRequests(Requests, ECacheMethod::Get, Priority);
	}

	void Read(TConstArrayView<FCachePutValueRequest> Requests, TConstArrayView<FCachePutValueResponse> Responses, EPriority Priority) final
	{
		// Replay puts as gets.
		TArray<FCacheGetValueRequest, TInlineAllocator<16>> GetRequests;
		GetRequests.Reserve(Requests.Num());
		for (const FCachePutValueRequest& Request : Requests)
		{
			GetRequests.Add({Request.Name, Request.Key, Request.Policy, Request.UserData});
		}
		DispatchRequests(MakeConstArrayView(GetRequests), ECacheMethod::PutValue, Priority);
	}

	void Read(TConstArrayView<FCacheGetValueRequest> Requests, TConstArrayView<FCacheGetValueResponse> Responses, EPriority Priority) final
	{
		DispatchRequests(Requests, ECacheMethod::GetValue, Priority);
	}

	void Read(TConstArrayView<FCacheGetChunkRequest> Requests, TConstArrayView<FCacheGetChunkResponse> Responses, EPriority Priority) final
	{
		DispatchRequests(Requests, ECacheMethod::GetChunks, Priority);
	}

	bool SerializeBatch(FCbObjectView Batch) final;

	void WaitForAsyncReads();

	void ReadFromFileAsync(const TCHAR* ReplayPath, ICacheStoreOwner& Owner, uint64 ScratchSize);
	bool ReadFromFile(const TCHAR* ReplayPath, uint64 ScratchSize);
	bool ReadFromArchive(FArchive& ReplayAr, uint64 ScratchSize);
	bool ReadFromObject(FCbObjectView Object);

	static_assert(uint8(EPriority::Lowest) == 0);
	static_assert(uint8(EPriority::Low) == 1);
	static_assert(uint8(EPriority::Normal) == 2);
	static_assert(uint8(EPriority::High) == 3);
	static_assert(uint8(EPriority::Highest) == 4);
	static_assert(uint8(EPriority::Blocking) == 5);

	ILegacyCacheStore* TargetCache = nullptr;
	FCacheKeyFilter KeyFilter;
	FCacheMethodFilter MethodFilter;
	ECachePolicy PolicyFlagsToAdd = ECachePolicy::None;
	ECachePolicy PolicyFlagsToRemove = ECachePolicy::None;
	FRequestOwner Owners[6]
	{
		FRequestOwner(EPriority::Lowest),
		FRequestOwner(EPriority::Low),
		FRequestOwner(EPriority::Normal),
		FRequestOwner(EPriority::High),
		FRequestOwner(EPriority::Highest),
		FRequestOwner(EPriority::Blocking),
	};

private:
	template <typename RequestType>
	bool DispatchRequests(TConstArrayView<RequestType> Requests, ECacheMethod Method, EPriority Priority);

	void ApplyPolicyTransform(FCacheGetRequest& Request);
	void ApplyPolicyTransform(FCacheGetValueRequest& Request);
	void ApplyPolicyTransform(FCacheGetChunkRequest& Request);

	Tasks::FPipe ReadAsyncPipe{TEXT("CacheReplayReadAsync")};
	TArray<Tasks::FTask> BlockingTasks;
	int32 DispatchCount = 0;
	int32 DispatchScope = 0;

	class FDispatchScope;
};

class FCacheReplayReader::FState::FDispatchScope
{
public:
	explicit FDispatchScope(FState& InState)
		: State(InState)
	{
		if (State.DispatchScope++ == 0)
		{
			StartTime = FPlatformTime::Seconds();
			StartDispatchCount = State.DispatchCount;
		}
	}

	~FDispatchScope()
	{
		if (--State.DispatchScope == 0)
		{
			UE_LOGF(LogDerivedDataCache, Display, "Replay: Dispatched %d requests in %.3lf seconds.",
				State.DispatchCount - StartDispatchCount, FPlatformTime::Seconds() - StartTime);
		}
	}

private:
	FState& State;
	double StartTime = 0.0;
	int32 StartDispatchCount = 0;
};

FCacheReplayReader::FState::~FState()
{
	WaitForAsyncReads();

	TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_RequestWait);
	Tasks::Wait(BlockingTasks);

	for (FRequestOwner& Owner : Owners)
	{
		Owner.Wait();
	}
}

void FCacheReplayReader::FState::WaitForAsyncReads()
{
	if (ReadAsyncPipe.HasWork())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_DispatchWait);
		ReadAsyncPipe.WaitUntilEmpty();
	}
}

bool FCacheReplayReader::FState::SerializeBatch(FCbObjectView Batch)
{
	if (UE_LOG_ACTIVE(LogDerivedDataCache, Verbose))
	{
		ECacheMethod Method;
		if (LoadFromCompactBinary(Batch[ANSITEXTVIEW("Method")], Method) &&
			MethodFilter.IsMatch(Method) &&
			Algo::AnyOf(Batch[ANSITEXTVIEW("Requests")], [this](FCbFieldView Request)
			{ 
				FCacheKey Key;
				return LoadFromCompactBinary(Request[ANSITEXTVIEW("Key")], Key) && KeyFilter.IsMatch(Key);
			}))
		{
			TStringBuilder<1024> Text;
			CompactBinaryToCompactJson(Batch, Text);
			UE_LOGF(LogDerivedDataCache, Verbose, "Replay: %ls", *Text);
		}
	}

	return ICacheReplayReader::SerializeBatch(Batch);
}

template <typename RequestType>
bool FCacheReplayReader::FState::DispatchRequests(const TConstArrayView<RequestType> Requests, const ECacheMethod Method, const EPriority Priority)
{
	if (!MethodFilter.IsMatch(Method))
	{
		return true;
	}

	TArray<RequestType, TInlineAllocator<16>> FilteredRequests;
	for (const RequestType& Request : Requests)
	{
		if (KeyFilter.IsMatch(Request.Key))
		{
			RequestType& FilteredRequest = FilteredRequests.Emplace_GetRef(Request);
			ApplyPolicyTransform(FilteredRequest);
		}
	}

	if (!FilteredRequests.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_Dispatch);

		DispatchCount += FilteredRequests.Num();

		FRequestOwner& Owner = Owners[uint8(Priority)];
		if (Owner.GetPriority() < EPriority::Blocking)
		{
			// Owners with non-blocking priority can execute the request directly because it will be async.
			FRequestBarrier Barrier(Owner);
			Invoke(CacheStoreFunctionFor<RequestType>, TargetCache, FilteredRequests, Owner, [](TCacheResponseFor<RequestType>&&){});
		}
		else
		{
			// Owners with blocking priority launch a task to execute the blocking request to allow concurrent replay.
			BlockingTasks.Add(Tasks::Launch(TEXT("CacheReplayTask"), [this, Requests = MoveTemp(FilteredRequests)]
			{
				FRequestOwner BlockingOwner(EPriority::Blocking);
				Invoke(CacheStoreFunctionFor<RequestType>, TargetCache, Requests, BlockingOwner, [](TCacheResponseFor<RequestType>&&){});
				BlockingOwner.Wait();
			}));
		}
	}

	return true;
}

void FCacheReplayReader::FState::ApplyPolicyTransform(FCacheGetRequest& Request)
{
	Request.Policy = Request.Policy.Transform([this](ECachePolicy Policy)
	{
		EnumAddFlags(Policy, PolicyFlagsToAdd);
		EnumRemoveFlags(Policy, PolicyFlagsToRemove);
		return Policy;
	});
}

void FCacheReplayReader::FState::ApplyPolicyTransform(FCacheGetValueRequest& Request)
{
	EnumAddFlags(Request.Policy, PolicyFlagsToAdd);
	EnumRemoveFlags(Request.Policy, PolicyFlagsToRemove);
}

void FCacheReplayReader::FState::ApplyPolicyTransform(FCacheGetChunkRequest& Request)
{
	EnumAddFlags(Request.Policy, PolicyFlagsToAdd);
	EnumRemoveFlags(Request.Policy, PolicyFlagsToRemove);
}

void FCacheReplayReader::FState::ReadFromFileAsync(const TCHAR* const ReplayPath, ICacheStoreOwner& Owner, const uint64 ScratchSize)
{
	Owner.AddToAsyncTaskCounter(1);
	ReadAsyncPipe.Launch(TEXT("CacheReplayReadFromFileAsync"), [this, ReplayPath = FString(ReplayPath), &Owner, ScratchSize]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_ReadFromFileAsync);
		ReadFromFile(*ReplayPath, ScratchSize);
		Owner.AddToAsyncTaskCounter(-1);
	});
}

bool FCacheReplayReader::FState::ReadFromFile(const TCHAR* const ReplayPath, const uint64 ScratchSize)
{
	FDispatchScope Dispatch(*this);
	return ReadReplayFromFile(*this, ReplayPath);
}

bool FCacheReplayReader::FState::ReadFromArchive(FArchive& ReplayAr, const uint64 ScratchSize)
{
	FDispatchScope Dispatch(*this);
	return ReadReplayFromArchive(*this, ReplayAr);
}

bool FCacheReplayReader::FState::ReadFromObject(const FCbObjectView Object)
{
	FDispatchScope Dispatch(*this);
	return SerializeBatch(Object);
}

FCacheReplayReader::FCacheReplayReader(ILegacyCacheStore* const TargetCache)
	: State(MakePimpl<FState>())
{
	State->TargetCache = TargetCache;
}

void FCacheReplayReader::ReadFromFileAsync(const TCHAR* ReplayPath, ICacheStoreOwner& Owner, const uint64 ScratchSize)
{
	return State->ReadFromFileAsync(ReplayPath, Owner, ScratchSize);
}

bool FCacheReplayReader::ReadFromFile(const TCHAR* ReplayPath, const uint64 ScratchSize)
{
	State->WaitForAsyncReads();
	TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_ReadFromFile);
	return State->ReadFromFile(ReplayPath, ScratchSize);
}

bool FCacheReplayReader::ReadFromArchive(FArchive& ReplayAr, const uint64 ScratchSize)
{
	State->WaitForAsyncReads();
	TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_ReadFromArchive);
	return State->ReadFromArchive(ReplayAr, ScratchSize);
}

bool FCacheReplayReader::ReadFromObject(const FCbObjectView Object)
{
	State->WaitForAsyncReads();
	TRACE_CPUPROFILER_EVENT_SCOPE(ReplayDDC_ReadFromObject);
	return State->ReadFromObject(Object);
}

void FCacheReplayReader::WaitForAsyncReads()
{
	State->WaitForAsyncReads();
}

void FCacheReplayReader::SetKeyFilter(FCacheKeyFilter KeyFilter)
{
	State->KeyFilter = MoveTemp(KeyFilter);
}

void FCacheReplayReader::SetMethodFilter(FCacheMethodFilter MethodFilter)
{
	State->MethodFilter = MoveTemp(MethodFilter);
}

void FCacheReplayReader::SetPolicyTransform(ECachePolicy AddFlags, ECachePolicy RemoveFlags)
{
	State->PolicyFlagsToAdd = AddFlags;
	State->PolicyFlagsToRemove = RemoveFlags;
}

void FCacheReplayReader::SetPriorityOverride(EPriority Priority)
{
	for (FRequestOwner& Owner : State->Owners)
	{
		Owner.SetPriority(Priority);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FCacheMethodFilter ParseReplayMethodFilter(const TCHAR* const CommandLine)
{
	FCacheMethodFilter MethodFilter;
	FString MethodNames;
	if (FParse::Value(CommandLine, TEXT("-DDC-ReplayMethods="), MethodNames))
	{
		MethodFilter = FCacheMethodFilter::Parse(MethodNames);
	}
	return MethodFilter;
}

static FCacheKeyFilter ParseReplayKeyFilter(const TCHAR* const CommandLine)
{
	const bool bDefaultMatch =
		String::FindFirst(CommandLine, TEXT("-DDC-ReplayTypes="), ESearchCase::IgnoreCase) == INDEX_NONE &&
		String::FindFirst(CommandLine, TEXT("-DDC-ReplayKeys="), ESearchCase::IgnoreCase) == INDEX_NONE;
	float DefaultRate = bDefaultMatch ? 100.0f : 0.0f;
	FParse::Value(CommandLine, TEXT("-DDC-ReplayRate="), DefaultRate);

	FCacheKeyFilter KeyFilter = FCacheKeyFilter::Parse(CommandLine, TEXT("-DDC-ReplayTypes="), TEXT("-DDC-ReplayKeys="), DefaultRate);

	uint32 Salt;
	if (FParse::Value(CommandLine, TEXT("-DDC-ReplaySalt="), Salt))
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
		"Replay: Using salt -DDC-ReplaySalt=%u to filter cache keys to replay.", KeyFilter.GetSalt());

	return KeyFilter;
}

static void ParseReplayPolicyTransform(const TCHAR* const CommandLine, FCacheReplayReader& Reader)
{
	ECachePolicy FlagsToAdd = ECachePolicy::None;
	FString FlagNamesToAdd;
	if (FParse::Value(CommandLine, TEXT("-DDC-ReplayLoadAddPolicy="), FlagNamesToAdd))
	{
		TryLexFromString(FlagsToAdd, FlagNamesToAdd);
	}

	ECachePolicy FlagsToRemove = ECachePolicy::None;
	FString FlagNamesToRemove;
	if (FParse::Value(CommandLine, TEXT("-DDC-ReplayLoadRemovePolicy="), FlagNamesToRemove))
	{
		TryLexFromString(FlagsToRemove, FlagNamesToRemove);
	}

	Reader.SetPolicyTransform(FlagsToAdd, FlagsToRemove);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ILegacyCacheStore* TryCreateCacheStoreReplay(ILegacyCacheStore* InnerCache, ICacheStoreOwner& Owner)
{
	const TCHAR* const CommandLine = FCommandLine::Get();
	const bool bHasReplayLoad = String::FindFirst(CommandLine, TEXT("-DDC-ReplayLoad=")) != INDEX_NONE;

	FString ReplaySavePath;
	if (!FParse::Value(CommandLine, TEXT("-DDC-ReplaySave="), ReplaySavePath) && !bHasReplayLoad)
	{
		return nullptr;
	}

	ILegacyCacheStore* ReplayTarget = InnerCache;
	FCacheStoreReplay* ReplayStore = nullptr;

	const FCacheKeyFilter KeyFilter = ParseReplayKeyFilter(CommandLine);
	const FCacheMethodFilter MethodFilter = ParseReplayMethodFilter(CommandLine);

	if (!ReplaySavePath.IsEmpty())
	{
		// Create the replay cache store to save requests that pass the filters.
		const uint64 BlockSize = FParse::Param(CommandLine, TEXT("DDC-ReplayNoCompress")) ? 0 : GCacheReplayCompressionBlockSize;
		ReplayTarget = ReplayStore = new FCacheStoreReplay(InnerCache, KeyFilter, MethodFilter, MoveTemp(ReplaySavePath), BlockSize);
	}
	else
	{
		// Create a replay store to own the reader without saving a new replay.
		ReplayStore = new FCacheStoreReplay(InnerCache, {}, {}, {}, {});
	}

	// Load every cache replay file that was requested on the command line.
	if (bHasReplayLoad)
	{
		FCacheReplayReader Reader(ReplayTarget);
		Reader.SetKeyFilter(KeyFilter);
		Reader.SetMethodFilter(MethodFilter);
		ParseReplayPolicyTransform(CommandLine, Reader);

		EPriority ReplayLoadPriority = EPriority::Lowest;
		FString ReplayLoadPriorityName;
		if (FParse::Value(CommandLine, TEXT("-DDC-ReplayLoadPriority="), ReplayLoadPriorityName) &&
			TryLexFromString(ReplayLoadPriority, ReplayLoadPriorityName))
		{
			Reader.SetPriorityOverride(ReplayLoadPriority);
		}

		const TCHAR* Tokens = CommandLine;
		for (FString Token; FParse::Token(Tokens, Token, /*UseEscape*/ false);)
		{
			FString ReplayLoadPath;
			if (FParse::Value(*Token, TEXT("-DDC-ReplayLoad="), ReplayLoadPath))
			{
				UE_LOGF(LogDerivedDataCache, Display, "Replay: Loading cache replay from '%ls'", *ReplayLoadPath);
				Reader.ReadFromFileAsync(*ReplayLoadPath, Owner);
			}
		}

		if (FParse::Param(CommandLine, TEXT("DDC-ReplayLoadSync")))
		{
			Reader.WaitForAsyncReads();
		}

		ReplayStore->SetReader(MoveTemp(Reader));
	}

	return ReplayStore;
}

} // UE::DerivedData
