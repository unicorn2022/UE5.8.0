// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataPrivate.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Logging/StructuredLog.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"

#include "TestHarness.h"

namespace UE::DerivedData::CacheStoreTest
{

static FCompressedBuffer MakeSequentialIntegerBuffer(const uint32 ValueCount)
{
	FUniqueBuffer Buffer = FUniqueBuffer::Alloc(ValueCount * sizeof(uint32));
	uint32* Values = static_cast<uint32*>(Buffer.GetData());
	for (uint32 ValueIndex = 0; ValueIndex < ValueCount; ++ValueIndex)
	{
		Values[ValueIndex] = ValueIndex;
	}
	return FCompressedBuffer::Compress(Buffer.MoveToShared());
}

static bool CheckSequentialIntegerBufferRange(FMemoryView Data, const uint64 Offset)
{
	bool bOk = true;
	const uint32* Values = static_cast<const uint32*>(Data.GetData());
	const uint32 ValueBase = uint32(Offset / sizeof(uint32));
	const uint32 ValueCount = uint32(Data.GetSize() / sizeof(uint32));
	for (uint32 ValueIndex = 0; ValueIndex < ValueCount; ++ValueIndex)
	{
		bOk &= Values[ValueIndex] == ValueIndex + ValueBase;
	}
	return bOk;
}

static bool HasValueWithData(const FCacheRecord& Record, const FValueWithId& Value)
{
	const FValueWithId& ValueInRecord = Record.GetValue(Value.GetId());
	return ValueInRecord == Value &&
		ValueInRecord.GetData().GetRawHash() == Value.GetRawHash() &&
		ValueInRecord.GetData().GetRawSize() == Value.GetRawSize();
}

static bool HasValueWithNoData(const FCacheRecord& Record, const FValueWithId& Value)
{
	const FValueWithId& ValueInRecord = Record.GetValue(Value.GetId());
	return ValueInRecord == Value && !ValueInRecord.HasData();
}

static FCacheBucket GetBucket()
{
	static const FCacheBucket Bucket(ANSITEXTVIEW("Test"));
	return Bucket;
}

static FCacheKey CreateKeyFromGuid(const ANSICHAR* GuidStr, int32 Index = 0, FAnsiStringView Extra = {})
{
	const FGuid Guid(GuidStr);
	FIoHashBuilder HashBuilder;
	HashBuilder.Update(&Guid, sizeof(FGuid));
	HashBuilder.Update(&Index, sizeof(int32));
	if (!Extra.IsEmpty())
	{
		HashBuilder.Update(Extra.GetData(), Extra.Len() * sizeof(ANSICHAR));
	}
	return {GetBucket(), HashBuilder.Finalize()};
}

static FCacheKey GetMissingKey()
{
	static const FCacheKey Key = CreateKeyFromGuid("c618cf5a-ac13-42d5-8c1f-6fefbc485b65");
	return Key;
}

static FValueId GetMissingId()
{
	static const FValueId Id = FValueId::FromName(ANSITEXTVIEW("MissingId"));
	return Id;
}

static FCbObject CreateMeta(const ANSICHAR* Value)
{
	TCbWriter<64> Writer;
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("Key"), Value);
	Writer.EndObject();
	return Writer.Save().AsObject();
};

static void TestCacheChunks(ICache& Cache, const FCacheKey& Key, const FValueId ValueId, const FValue& Value)
{
	const uint64 UserData = 0xfedcba9876543210;
	const FSharedString Name(TEXTVIEW("Chunks"));

	const FIoHash ValueHash = Value.GetRawHash();
	const FIoHash DifferentHash = FIoHash::HashBuffer(&ValueHash, sizeof(ValueHash));

	{
		INFO("Get Value Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == Value.GetRawSize());
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Value Hash + Size Redundant Batch");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		std::atomic<uint64> UserDataAccumulator = 0;
		Cache.GetChunks(
			{
				{.Name = Name, .Key = Key, .Id = ValueId, .Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData},
				{.Name = Name, .Key = Key, .Id = ValueId, .Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData + 1} 
			}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			UserDataAccumulator.fetch_xor(Response.UserData, std::memory_order_relaxed);
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == Value.GetRawSize());
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
		CHECK(UserDataAccumulator == 1);
	}

	{
		INFO("Get Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == Value.GetRawSize());
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(FIoHash::HashBuffer(Response.RawData) == Response.RawHash);
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Value w/ RawHash");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawHash = ValueHash, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == Value.GetRawSize());
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(FIoHash::HashBuffer(Response.RawData) == Response.RawHash);
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Value w/ Wrong RawHash");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawHash = DifferentHash, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == 0);
			CHECK(Response.RawHash.IsZero());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Missing Key");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = GetMissingKey(), .Id = ValueId, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == GetMissingKey());
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == 0);
			CHECK(Response.RawHash.IsZero());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	if (ValueId.IsValid())
	{
		INFO("Get Missing Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = GetMissingId(), .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == GetMissingId());
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == 0);
			CHECK(Response.RawHash.IsZero());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	if (Value.GetRawSize() < 2 * 1024 * 1024)
	{
		return;
	}

	{
		INFO("Get Partial Value Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 256 * 1024, .RawSize = 512 * 1024,
			.Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 256 * 1024);
			CHECK(Response.RawSize == 512 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value Hash + Size w/ RawHash");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 256 * 1024, .RawSize = 512 * 1024,
			.RawHash = ValueHash, .Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 256 * 1024);
			CHECK(Response.RawSize == 512 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value Hash + Size w/ Wrong RawHash");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 256 * 1024, .RawSize = 512 * 1024,
			.RawHash = DifferentHash, .Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 256 * 1024);
			CHECK(Response.RawSize == 0);
			CHECK(Response.RawHash.IsZero());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 256 * 1024, .RawSize = 512 * 1024, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 256 * 1024);
			CHECK(Response.RawSize == 512 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(CheckSequentialIntegerBufferRange(Response.RawData, Response.RawOffset));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value w/ RawHash");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 256 * 1024, .RawSize = 512 * 1024, .RawHash = ValueHash, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 256 * 1024);
			CHECK(Response.RawSize == 512 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(CheckSequentialIntegerBufferRange(Response.RawData, Response.RawOffset));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value Partially Out of Bounds");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = Value.GetRawSize() - 16 * 1024, .RawSize = 32 * 1024, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == Value.GetRawSize() - 16 * 1024);
			CHECK(Response.RawSize == 16 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(CheckSequentialIntegerBufferRange(Response.RawData, Response.RawOffset));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value Hash + Size Partially Out of Bounds");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = Value.GetRawSize() - 16 * 1024, .RawSize = 32 * 1024,
			.Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == Value.GetRawSize() - 16 * 1024);
			CHECK(Response.RawSize == 16 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value Out of Bounds");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 1024 * 1024 * 1024, .RawSize = 512 * 1024, .UserData = UserData}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 1024 * 1024 * 1024);
			CHECK(Response.RawSize == 0);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData);
			CHECK(Response.RawData.GetSize() == 0);
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Partial Value Hash + Size Out of Bounds");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = 1024 * 1024 * 1024, .RawSize = 512 * 1024,
			.Policy = ECachePolicy::Default | ECachePolicy::SkipData, .UserData = UserData}},
			BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 1024 * 1024 * 1024);
			CHECK(Response.RawSize == 0);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.IsNull());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Multiple Partial Values");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks(
			{
				{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = (1 * 256 + 128) * 1024, .RawSize = 512 * 1024, .UserData = UserData},
				{.Name = Name, .Key = Key, .Id = ValueId, .RawOffset = (3 * 256 + 128) * 1024, .RawSize = 512 * 1024, .UserData = UserData + 1},
			}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK((Response.UserData & ~uint64(1)) == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == ((1 + 2 * (Response.UserData & 1)) * 256 + 128) * 1024);
			CHECK(Response.RawSize == 512 * 1024);
			CHECK(Response.RawHash == Value.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(CheckSequentialIntegerBufferRange(Response.RawData, Response.RawOffset));
		});
		BlockingOwner.Wait();
	}
}

enum class ETestCacheValuesFlags : uint32
{
	None = 0,
	SkipPutMissingData = 1 << 0,
};

ENUM_CLASS_FLAGS(ETestCacheValuesFlags);

static void TestCacheValues(ICache& Cache, ETestCacheValuesFlags Flags = ETestCacheValuesFlags::None, const uint32 ValueCount = 4 * 1024 * 1024)
{
	const FValue Value(MakeSequentialIntegerBuffer(ValueCount));

	const uint64 UserData = 0xfedcba9876543210;
	const FSharedString Name(TEXTVIEW("Values"));
	const FCacheKey Key = CreateKeyFromGuid("091714ca-5136-438d-a2c8-0304cc6c54e6", ValueCount);

	if (!EnumHasAnyFlags(Flags, ETestCacheValuesFlags::SkipPutMissingData))
	{
		INFO("Put Value w/o Data");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{Name, Key, Value.RemoveData(), ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value == Value);
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{Name, Key, Value, ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value == Value);
			CHECK(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Value SkipData");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{Name, Key, Value, ECachePolicy::Default | ECachePolicy::SkipData, UserData}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value == Value);
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Existing Value w/o Data");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{Name, Key, Value.RemoveData(), ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value == Value);
			CHECK(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Value Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetValue({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData}}, BlockingOwner, [&](FCacheGetValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value.GetRawHash() == Value.GetRawHash());
			CHECK(Response.Value.GetRawSize() == Value.GetRawSize());
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Value Hash + Size Redundant Batch");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		std::atomic<uint64> UserDataAccumulator = 0;
		Cache.GetValue(
			{
				{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData},
				{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData + 1}
			}, BlockingOwner, [&](FCacheGetValueResponse&& Response)
		{
			UserDataAccumulator.fetch_xor(Response.UserData, std::memory_order_relaxed);
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value.GetRawHash() == Value.GetRawHash());
			CHECK(Response.Value.GetRawSize() == Value.GetRawSize());
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
		CHECK(UserDataAccumulator == 1);
	}

	{
		INFO("Get Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetValue({{Name, Key, ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCacheGetValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == Key);
			CHECK(Response.Value.GetRawHash() == Value.GetRawHash());
			CHECK(Response.Value.GetRawSize() == Value.GetRawSize());
			CHECK(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Missing Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetValue({{Name, GetMissingKey(), ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCacheGetValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Key == GetMissingKey());
			CHECK(Response.Value == FValue::Null);
		});
		BlockingOwner.Wait();
	}

	INFO("TestCacheValues()");
	TestCacheChunks(Cache, Key, {}, Value);
}

enum class ETestCacheRecordsFlags : uint32
{
	None = 0,
	SkipPutMissingData = 1 << 0,
};

ENUM_CLASS_FLAGS(ETestCacheRecordsFlags);

static void TestCacheRecords(ICache& Cache, ETestCacheRecordsFlags Flags = ETestCacheRecordsFlags::None)
{
	const FValueId ValueId = FValueId::FromName(ANSITEXTVIEW("Value"));
	const FValueId Attachment1Id = FValueId::FromName(ANSITEXTVIEW("Attachment1"));
	const FValueId Attachment2Id = FValueId::FromName(ANSITEXTVIEW("Attachment2"));
	const FValueId Attachment3Id = FValueId::FromName(ANSITEXTVIEW("Attachment3"));
	const FValueId Attachment4Id = FValueId::FromName(ANSITEXTVIEW("Attachment4"));

	const FValueWithId Value(ValueId, MakeSequentialIntegerBuffer(12 * 1024 * 1024));
	const FValueWithId Attachment1(Attachment1Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({1}))));
	const FValueWithId Attachment2(Attachment2Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({2, 3}))));
	const FValueWithId Attachment3(Attachment3Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({3, 4, 5}))));
	const FValueWithId Attachment4(Attachment4Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({4, 5, 6, 7}))));

	const FCbObject Meta = CreateMeta("Value");

	const uint64 UserData = 0xfedcba9876543210;
	const FSharedString Name(TEXTVIEW("Records"));
	const FCacheKey Key = CreateKeyFromGuid("ff173633-71e9-4960-a683-470c77abaa6a");

	const FCacheRecord Record = [&]
	{
		FCacheRecordBuilder RecordBuilder(Key);
		RecordBuilder.AddValue(Value);
		RecordBuilder.AddValue(Attachment1);
		RecordBuilder.AddValue(Attachment2);
		RecordBuilder.AddValue(Attachment3);
		RecordBuilder.AddValue(Attachment4);
		RecordBuilder.SetMeta(CopyTemp(Meta));
		return RecordBuilder.Build();
	}();

	if (!EnumHasAnyFlags(Flags, ETestCacheRecordsFlags::SkipPutMissingData))
	{
		INFO("Put Record w/o Meta+Data");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, ApplySkipPolicy(Record, ECachePolicy::SkipMeta | ECachePolicy::SkipData), ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK_FALSE(Response.Record.GetMeta());
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Record");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, Record, ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithData(Response.Record, Attachment3));
			CHECK(HasValueWithData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Record SkipMeta");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, Record, ECachePolicy::Default | ECachePolicy::SkipMeta, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
				CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
				CHECK(Response.Record.GetKey() == Key);
			CHECK_FALSE(Response.Record.GetMeta());
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithData(Response.Record, Attachment3));
			CHECK(HasValueWithData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Record SkipData");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, Record, ECachePolicy::Default | ECachePolicy::SkipData, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Existing Record w/o Meta+Data");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, ApplySkipPolicy(Record, ECachePolicy::SkipMeta | ECachePolicy::SkipData), ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithData(Response.Record, Attachment3));
			CHECK(HasValueWithData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record Hash + Size Redundant Batch");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		std::atomic<uint64> UserDataAccumulator = 0;
		Cache.Get(
			{
				{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData},
				{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData + 1}
			}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			UserDataAccumulator.fetch_xor(Response.UserData, std::memory_order_relaxed);
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
		CHECK(UserDataAccumulator == 1);
	}

	{
		INFO("Get Record");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipMeta, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithData(Response.Record, Attachment3));
			CHECK(HasValueWithData(Response.Record, Attachment4));
			CHECK_FALSE(Response.Record.GetMeta());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record w/ Skipped/Ignored Values");
		FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::Default);
		PolicyBuilder.AddValuePolicy(ValueId, ECachePolicy::Default | ECachePolicy::SkipData);
		PolicyBuilder.AddValuePolicy(Attachment1Id, ECachePolicy::None);
		PolicyBuilder.AddValuePolicy(Attachment2Id, ECachePolicy::None);
		PolicyBuilder.AddValuePolicy(Attachment3Id, ECachePolicy::None);
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, PolicyBuilder.Build(), UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithData(Response.Record, Attachment4));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Missing Record");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, GetMissingKey(), ECachePolicy::Default, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == GetMissingKey());
			CHECK(Response.Record.GetValues().IsEmpty());
			CHECK_FALSE(Response.Record.GetMeta());
		});
		BlockingOwner.Wait();
	}
	
	INFO("TestCacheRecords()");
	TestCacheChunks(Cache, Key, ValueId, Value);
}

enum class ETestPartialCacheRecordsFlags : uint32
{
	None = 0,
	SkipMetaDiff = 1 << 0,
};

ENUM_CLASS_FLAGS(ETestPartialCacheRecordsFlags);

static void TestPartialCacheRecords(ICache& Cache, ETestPartialCacheRecordsFlags Flags = ETestPartialCacheRecordsFlags::None)
{
	const FValueId ValueId = FValueId::FromName(ANSITEXTVIEW("Value"));
	const FValueId Attachment1Id = FValueId::FromName(ANSITEXTVIEW("Attachment1"));
	const FValueId Attachment2Id = FValueId::FromName(ANSITEXTVIEW("Attachment2"));
	const FValueId Attachment3Id = FValueId::FromName(ANSITEXTVIEW("Attachment3"));
	const FValueId Attachment4Id = FValueId::FromName(ANSITEXTVIEW("Attachment4"));

	const FValueWithId Value(ValueId, MakeSequentialIntegerBuffer(16 * 1024 * 1024));
	const FValueWithId Attachment1(Attachment1Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({5}))));
	const FValueWithId Attachment2(Attachment2Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({6, 5}))));
	const FValueWithId Attachment3(Attachment3Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({7, 6, 5}))));
	const FValueWithId Attachment4(Attachment4Id, FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({8, 7, 6, 5}))));

	const FCbObject Meta = CreateMeta("Value");

	const uint64 UserData = 0xfedcba9876543210;
	const FSharedString Name(TEXTVIEW("PartialRecords"));
	const FCacheKey Key = CreateKeyFromGuid("5d077783-8ecc-400a-bb65-168345d4f7a7");

	{
		INFO("Put Record w/ No Values");
		FCacheRecordBuilder RecordBuilder(Key);
		RecordBuilder.AddValue(Value.RemoveData());
		RecordBuilder.AddValue(Attachment1.RemoveData());
		RecordBuilder.AddValue(Attachment2.RemoveData());
		RecordBuilder.AddValue(Attachment3.RemoveData());
		RecordBuilder.AddValue(Attachment4.RemoveData());
		RecordBuilder.SetMeta(CopyTemp(Meta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, RecordBuilder.Build(), ECachePolicy::Default | ECachePolicy::PartialRecord, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Record w/ Value+Attachment1+Attachment2");
		FCacheRecordBuilder RecordBuilder(Key);
		RecordBuilder.AddValue(Value);
		RecordBuilder.AddValue(Attachment1);
		RecordBuilder.AddValue(Attachment2);
		RecordBuilder.AddValue(Attachment3.RemoveData());
		RecordBuilder.AddValue(Attachment4.RemoveData());
		RecordBuilder.SetMeta(CopyTemp(Meta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, RecordBuilder.Build(), ECachePolicy::Default | ECachePolicy::PartialRecord, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Record w/ Attachment2+Attachment3");
		const FCbObject NewMeta = EnumHasAnyFlags(Flags, ETestPartialCacheRecordsFlags::SkipMetaDiff) ? Meta : CreateMeta("NewValue");
		FCacheRecordBuilder RecordBuilder(Key);
		RecordBuilder.AddValue(Value.RemoveData());
		RecordBuilder.AddValue(Attachment1.RemoveData());
		RecordBuilder.AddValue(Attachment2);
		RecordBuilder.AddValue(Attachment3);
		RecordBuilder.AddValue(Attachment4.RemoveData());
		RecordBuilder.SetMeta(CopyTemp(NewMeta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{Name, RecordBuilder.Build(), ECachePolicy::Default | ECachePolicy::PartialRecord, UserData}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Key == Key);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record Hash + Size w/o PartialRecord");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().IsEmpty());
			CHECK_FALSE(Response.Record.GetMeta());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record w/o PartialRecord");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipMeta, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().IsEmpty());
			CHECK_FALSE(Response.Record.GetMeta());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipData | ECachePolicy::PartialRecord, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithNoData(Response.Record, Attachment1));
			CHECK(HasValueWithNoData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, ECachePolicy::Default | ECachePolicy::SkipMeta | ECachePolicy::PartialRecord, UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().Num() == 5);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
			CHECK_FALSE(Response.Record.GetMeta());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record w/ Skipped Value + Ignored Missing Value");
		FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::Default);
		PolicyBuilder.AddValuePolicy(Attachment3Id, ECachePolicy::Default | ECachePolicy::SkipData);
		PolicyBuilder.AddValuePolicy(Attachment4Id, ECachePolicy::None);
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, PolicyBuilder.Build(), UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(HasValueWithData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record w/ Default-Ignored Values + Required Values");
		FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::None);
		PolicyBuilder.AddValuePolicy(Attachment1Id, ECachePolicy::Default);
		PolicyBuilder.AddValuePolicy(Attachment2Id, ECachePolicy::Default);
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, PolicyBuilder.Build(), UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(HasValueWithNoData(Response.Record, Value));
			CHECK(HasValueWithData(Response.Record, Attachment1));
			CHECK(HasValueWithData(Response.Record, Attachment2));
			CHECK(HasValueWithNoData(Response.Record, Attachment3));
			CHECK(HasValueWithNoData(Response.Record, Attachment4));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Record w/ Default-Ignored Values + Required Missing Value");
		FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::None);
		PolicyBuilder.AddValuePolicy(Attachment4Id, ECachePolicy::Default);
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{Name, Key, PolicyBuilder.Build(), UserData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Error);
			CHECK(Response.UserData == UserData);
			CHECK(Response.Name == Name);
			CHECK(Response.Record.GetKey() == Key);
			CHECK(Response.Record.GetValues().IsEmpty());
			CHECK_FALSE(Response.Record.GetMeta());
		});
		BlockingOwner.Wait();
	}

	INFO("TestPartialCacheRecords()");
	TestCacheChunks(Cache, Key, ValueId, Value);
}

enum class ETestCacheDeterminismFlags : uint32
{
	None = 0,
	SkipOverwrite = 1 << 0,
	UniqueByComputerName = 1 << 1,
};

ENUM_CLASS_FLAGS(ETestCacheDeterminismFlags);

static void TestCacheDeterminism(ICache& Cache, ETestCacheDeterminismFlags Flags = ETestCacheDeterminismFlags::None)
{
	const FValue Value(MakeSequentialIntegerBuffer(8 * 1024 * 1024));
	// A new guid is the most reliable random value that the engine can generate at this time.
	// Add [1, 1024] bytes to create an OtherValue that tends to differ between tests.
	const int32 OtherValueExtraSize = 1 + (FGuid::NewGuid().D & 1023);
	const FValue OtherValue(MakeSequentialIntegerBuffer(8 * 1024 * 1024 + 1024));

	const FValueId ValueId = FValueId::FromName(ANSITEXTVIEW("Value"));
	const FValueId AttachmentId = FValueId::FromName(ANSITEXTVIEW("Attachment"));

	const FCbObject Meta = CreateMeta("Value");
	const FCbObject OtherMeta = CreateMeta("OtherValue");

	const FCacheKey RecordKey = CreateKeyFromGuid("8660fd50-a31c-4ea1-ace8-eb25b21b6db6", 0,
		EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::UniqueByComputerName) ? WriteToAnsiString<64>(FPlatformProcess::ComputerName()) : FAnsiStringView{});

	const auto PutOriginalRecordWithOverwrite = [&]
	{
		INFO("Put Original Record w/ Overwrite");
		FCacheRecordBuilder RecordBuilder(RecordKey);
		RecordBuilder.AddValue(ValueId, Value);
		RecordBuilder.AddValue(AttachmentId, Value);
		FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::Store | ECachePolicy::SkipData);
		PolicyBuilder.AddValuePolicy(ValueId, ECachePolicy::Store | ECachePolicy::SkipData | ECachePolicy::NonDeterministic);
		RecordBuilder.SetMeta(CopyTemp(Meta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{{TEXTVIEW("OriginalRecord")}, RecordBuilder.Build(), PolicyBuilder.Build()}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(ValueId, Value)));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	};
	PutOriginalRecordWithOverwrite();

	{
		INFO("Put Original Record w/ New Meta");
		FCacheRecordBuilder RecordBuilder(RecordKey);
		RecordBuilder.AddValue(ValueId, Value);
		RecordBuilder.AddValue(AttachmentId, Value);
		RecordBuilder.SetMeta(CopyTemp(OtherMeta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{{TEXTVIEW("OriginalRecordCopy")}, RecordBuilder.Build()}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(ValueId, Value)));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Different Record");
		FCacheRecordBuilder RecordBuilder(RecordKey);
		RecordBuilder.AddValue(ValueId, OtherValue);
		RecordBuilder.AddValue(AttachmentId, Value);
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{{TEXTVIEW("DifferentRecord")}, RecordBuilder.Build()}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(ValueId, Value)));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Different Record w/ NonDeterministic Flag");
		FCacheRecordBuilder RecordBuilder(RecordKey);
		RecordBuilder.AddValue(ValueId, OtherValue);
		RecordBuilder.AddValue(AttachmentId, Value);
		FCacheRecordPolicyBuilder PolicyBuilder;
		PolicyBuilder.AddValuePolicy(ValueId, ECachePolicy::Default | ECachePolicy::NonDeterministic);
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{{TEXTVIEW("DifferentRecordIgnore")}, RecordBuilder.Build(), PolicyBuilder.Build()}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(Meta));
			CHECK(HasValueWithData(Response.Record, FValueWithId(ValueId, Value)));
			CHECK(HasValueWithData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Original Record");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{{TEXTVIEW("OriginalRecord")}, RecordKey}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(HasValueWithData(Response.Record, FValueWithId(ValueId, Value)));
			CHECK(Response.Record.GetMeta().Equals(Meta));
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Put Different Record w/ Overwrite");
		FCacheRecordBuilder RecordBuilder(RecordKey);
		RecordBuilder.AddValue(ValueId, OtherValue);
		RecordBuilder.AddValue(AttachmentId, Value);
		RecordBuilder.SetMeta(CopyTemp(OtherMeta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{{TEXTVIEW("DifferentRecordOverwrite")}, RecordBuilder.Build(), ECachePolicy::Store}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(OtherMeta));
			CHECK(HasValueWithData(Response.Record, FValueWithId(ValueId, OtherValue)));
			CHECK(HasValueWithData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Get Different Record");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{{TEXTVIEW("DifferentRecord")}, RecordKey}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(OtherMeta));
			CHECK(HasValueWithData(Response.Record, FValueWithId(ValueId, OtherValue)));
			CHECK(HasValueWithData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Get Different Record Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Get({{{TEXTVIEW("DifferentRecord")}, RecordKey, ECachePolicy::Default | ECachePolicy::SkipData}}, BlockingOwner, [&](FCacheGetResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(OtherMeta));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(ValueId, OtherValue)));
			CHECK(HasValueWithNoData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Get Different Record Chunk");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{{TEXTVIEW("DifferentRecordChunk")}, RecordKey, ValueId}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Key == RecordKey);
			CHECK(Response.Id == ValueId);
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == OtherValue.GetRawSize());
			CHECK(Response.RawHash == OtherValue.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(FIoHash::HashBuffer(Response.RawData) == Response.RawHash);
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Put Different Record w/o Overwrite");
		FCacheRecordBuilder RecordBuilder(RecordKey);
		RecordBuilder.AddValue(ValueId, OtherValue);
		RecordBuilder.AddValue(AttachmentId, Value);
		RecordBuilder.SetMeta(CopyTemp(OtherMeta));
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.Put({{{TEXTVIEW("DifferentRecord")}, RecordBuilder.Build(), ECachePolicy::Default}}, BlockingOwner, [&](FCachePutResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Record.GetMeta().Equals(OtherMeta));
			CHECK(HasValueWithData(Response.Record, FValueWithId(ValueId, OtherValue)));
			CHECK(HasValueWithData(Response.Record, FValueWithId(AttachmentId, Value)));
		});
		BlockingOwner.Wait();
	}

	PutOriginalRecordWithOverwrite();

	const FCacheKey ValueKey = CreateKeyFromGuid("926ae1e6-8ef8-4efa-8f15-83d6c0468909");

	const auto PutOriginalValueWithOverwrite = [&]
	{
		INFO("Put Original Value w/ Overwrite");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{{TEXTVIEW("OriginalValue")}, ValueKey, Value, ECachePolicy::Store | ECachePolicy::SkipData | ECachePolicy::NonDeterministic}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value == Value);
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	};
	PutOriginalValueWithOverwrite();

	{
		INFO("Put Original Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{{TEXTVIEW("OriginalValueCopy")}, ValueKey, Value}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value == Value);
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Different Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{{TEXTVIEW("DifferentValue")}, ValueKey, OtherValue}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value == Value);
			CHECK_FALSE(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Put Different Value w/ NonDeterministic Flag");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{{TEXTVIEW("DifferentValueIgnore")}, ValueKey, OtherValue, ECachePolicy::Default | ECachePolicy::NonDeterministic}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value == Value);
			CHECK(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	{
		INFO("Get Original Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetValue({{{TEXTVIEW("OriginalValue")}, ValueKey}}, BlockingOwner, [&](FCacheGetValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value.GetRawHash() == Value.GetRawHash());
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Put Different Value w/ Overwrite");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{{TEXTVIEW("DifferentValueOverwrite")}, ValueKey, OtherValue, ECachePolicy::Store}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value == OtherValue);
			CHECK(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Get Different Value");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetValue({{{TEXTVIEW("DifferentValue")}, ValueKey}}, BlockingOwner, [&OtherValue](FCacheGetValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value.GetRawHash() == OtherValue.GetRawHash());
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Get Different Value Hash + Size");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetValue({{{TEXTVIEW("DifferentValue")}, ValueKey, ECachePolicy::Default | ECachePolicy::SkipData}}, BlockingOwner, [&OtherValue](FCacheGetValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value.GetRawHash() == OtherValue.GetRawHash());
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Get Different Value Chunk");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.GetChunks({{{TEXTVIEW("DifferentValueChunk")}, ValueKey}}, BlockingOwner, [&](FCacheGetChunkResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Key == ValueKey);
			CHECK(Response.Id.IsNull());
			CHECK(Response.RawOffset == 0);
			CHECK(Response.RawSize == OtherValue.GetRawSize());
			CHECK(Response.RawHash == OtherValue.GetRawHash());
			CHECK(Response.RawData.GetSize() == Response.RawSize);
			CHECK(FIoHash::HashBuffer(Response.RawData) == Response.RawHash);
		});
		BlockingOwner.Wait();
	}

	if (!EnumHasAnyFlags(Flags, ETestCacheDeterminismFlags::SkipOverwrite))
	{
		INFO("Put Different Value w/o Overwrite");
		FRequestOwner BlockingOwner(EPriority::Blocking);
		Cache.PutValue({{{TEXTVIEW("DifferentValue")}, ValueKey, OtherValue, ECachePolicy::Default}}, BlockingOwner, [&](FCachePutValueResponse&& Response)
		{
			CHECK(Response.Status == EStatus::Ok);
			CHECK(Response.Value == OtherValue);
			CHECK(Response.Value.HasData());
		});
		BlockingOwner.Wait();
	}

	PutOriginalValueWithOverwrite();
}

} // UE::DerivedData::CacheStoreTest

namespace UE::DerivedData
{

TEST_CASE("DerivedData::CacheStore", "[DerivedData]")
{
	using namespace CacheStoreTest;

	SECTION("Memory")
	{
		FStringView Config = TEXTVIEW("(Memory=(Type=Memory))");
		TUniquePtr<ICache> Cache(CreateCache(Config));
		CHECKED_IF(Cache)
		{
			TestCacheValues(*Cache);
			TestCacheRecords(*Cache);
			TestPartialCacheRecords(*Cache);
			TestCacheDeterminism(*Cache);
		}
	}

	SECTION("FileSystem")
	{
		FString CachePath = FPaths::EngineSavedDir() / TEXT("TestDerivedDataCache");

		TStringBuilder<256> Config;
		Config.Append(TEXT("(Local=(Type=FileSystem,Path=\""));
		Config.Append(CachePath);
		Config.Append(TEXT("\",Flush=true,DeleteUnused=false))"));

		TUniquePtr<ICache> Cache(CreateCache(Config));
		CHECKED_IF(Cache)
		{
			// Test different value sizes because value storage is tied to size.
			TestCacheValues(*Cache, ETestCacheValuesFlags::None, 8);
			TestCacheValues(*Cache, ETestCacheValuesFlags::None, 8 * 1024);
			TestCacheValues(*Cache, ETestCacheValuesFlags::None, 8 * 1024 * 1024);

			TestCacheRecords(*Cache);
			TestPartialCacheRecords(*Cache);
			TestCacheDeterminism(*Cache);
		}

		IFileManager::Get().DeleteDirectory(*CachePath, /*bRequireExists*/ false, /*bTree*/ true);
	}

	SECTION("Zen")
	{
		FStringView Config = TEXTVIEW("(Local=(Type=Zen,Namespace=Test,Sandbox=Test,Flush=true))");
		TUniquePtr<ICache> Cache(CreateCache(Config));
		CHECKED_IF(Cache)
		{
			TestCacheValues(*Cache);
			// TODO: Zen needs to be fixed to work with no flags passed to these functions.
			TestCacheRecords(*Cache, ETestCacheRecordsFlags::SkipPutMissingData);
			TestPartialCacheRecords(*Cache, ETestPartialCacheRecordsFlags::SkipMetaDiff);
			TestCacheDeterminism(*Cache);
		}
	}

	SECTION("Cloud")
	{
		FStringView Config = TEXTVIEW("(Cloud)");
		if (TUniquePtr<ICache> Cache{CreateCache(Config)})
		{
			TestCacheValues(*Cache, ETestCacheValuesFlags::SkipPutMissingData);
			TestCacheRecords(*Cache, ETestCacheRecordsFlags::SkipPutMissingData);
			// Skip the overwrite tests because they are not reliable due to the replication delay for the refs.
			TestCacheDeterminism(*Cache, ETestCacheDeterminismFlags::SkipOverwrite);
		}
		else
		{
			UE_LOGFMT(LogDerivedDataCache, Display, "Skipping test of Cloud cache store because it could not be created.");
		}
	}

	SECTION("Throttle")
	{
		FStringView Config = TEXTVIEW("(Throttle=(Type=Memory,LatencyMS=1,MaxBytesPerSecond=1000000000))");
		TUniquePtr<ICache> Cache(CreateCache(Config));
		CHECKED_IF(Cache)
		{
			TestCacheValues(*Cache);
			TestCacheRecords(*Cache);
			TestPartialCacheRecords(*Cache);
			TestCacheDeterminism(*Cache);
		}
	}
}

// Disabled by default because the determinism tests are not reliable due to the replication delay for the refs.
// Everything here is tested by the preceding test case except for the overwrites in the determinism test.
TEST_CASE("DerivedData::CacheStore::Cloud", "[.][DerivedData]")
{
	using namespace CacheStoreTest;

	FStringView Config = TEXTVIEW("(Cloud)");
	TUniquePtr<ICache> Cache(CreateCache(Config));
	CHECKED_IF(Cache)
	{
		TestCacheValues(*Cache, ETestCacheValuesFlags::SkipPutMissingData);
		TestCacheRecords(*Cache, ETestCacheRecordsFlags::SkipPutMissingData);
		TestCacheDeterminism(*Cache, ETestCacheDeterminismFlags::UniqueByComputerName);
	}
}

} // UE::DerivedData

#endif // WITH_LOW_LEVEL_TESTS
