// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "DerivedDataSerialization.h"

#include "DerivedDataCache.h"
#include "Misc/Optional.h"
#include "Misc/ReverseIterate.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
#include "TestHarness.h"

#include <catch2/generators/catch_generators.hpp>

namespace UE::DerivedData
{

static FCbField ReverseObjectFields(const FCbField& MaybeObject)
{
	if (!MaybeObject.IsObject())
	{
		return MaybeObject;
	}

	TArray<FCbField> Fields;
	for (const FCbField& Field : MaybeObject)
	{
		Fields.Add(Field);
	}
	FCbWriter Writer;
	Writer.BeginObject();
	for (const FCbField& Field : ReverseIterate(Fields))
	{
		Writer.AddField(Field.GetName(), ReverseObjectFields(Field));
	}
	Writer.EndObject();
	return Writer.Save();
}

TEST_CASE("DerivedData::Serialization::CompactBinary", "[DerivedData]")
{
	const FSharedString Name(TEXT("Name"));
	const FCacheKey Key{FCacheBucket(ANSITEXTVIEW("Bucket")), FIoHash::HashBuffer(MakeMemoryView<uint32>({0xf9332932, 0x68c04d2e, 0xaea27221, 0xb190704a}))};
	const FCbObject Meta = []
	{
		TCbWriter<64> Writer;
		Writer.BeginObject();
		Writer.AddString(ANSITEXTVIEW("Key"), ANSITEXTVIEW("Value"));
		Writer.AddString(ANSITEXTVIEW("Name"), ANSITEXTVIEW("Test"));
		Writer.EndObject();
		return Writer.Save().AsObject();
	}();
	const FIoHash MetaHash = Meta.GetHash();
	const FValueId ValueId = FValueId::FromName(TEXT("Value"));
	const FValue Value = FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({0, 1, 2, 3, 4, 5, 6, 7})));
	const FCacheRecord Record = [&]
	{
		FCacheRecordBuilder Builder(Key);
		Builder.SetMeta(CopyTemp(Meta));
		Builder.AddValue(ValueId, Value);
		return Builder.Build();
	}();
	const ECachePolicy ValuePolicy = ECachePolicy::Default | ECachePolicy::SkipData;
	const FCacheRecordPolicy RecordPolicy = [&]
	{
		FCacheRecordPolicyBuilder Builder(ECachePolicy::Local | ECachePolicy::SkipMeta | ECachePolicy::KeepAlive);
		Builder.AddValuePolicy(ValueId, ValuePolicy);
		return Builder.Build();
	}();
	const uint64 UserData = 0xfedcba9876543210;
	const FSharedBuffer EmptyBuffer = FUniqueBuffer::Alloc(0).MoveToShared();
	const FIoHash EmptyBufferHash = FIoHash::HashBuffer(EmptyBuffer);

	const FCbObject DiffMeta = ReverseObjectFields(Meta.AsField()).AsObject();
	const FIoHash DiffMetaHash = DiffMeta.GetHash();
	const FValue DiffValue = FValue::Compress(FSharedBuffer::MakeView(MakeMemoryView<uint8>({0, 1, 2, 3, 4, 5, 6, 7, 8})));
	const FCacheRecord DiffRecord = [&]
	{
		FCacheRecordBuilder Builder(Key);
		Builder.SetMeta(CopyTemp(DiffMeta));
		Builder.AddValue(ValueId, DiffValue);
		return Builder.Build();
	}();

	const auto LoadAttachment = [&](const FIoHash& Hash) -> FCbAttachment
	{
		if (Hash == Value.GetRawHash())
		{
			return FCbAttachment(Value.GetData());
		}
		if (Hash == MetaHash)
		{
			return FCbAttachment(Meta);
		}
		if (Hash == EmptyBufferHash)
		{
			return FCbAttachment(EmptyBuffer);
		}
		if (Hash == DiffValue.GetRawHash())
		{
			return FCbAttachment(DiffValue.GetData());
		}
		if (Hash == DiffMetaHash)
		{
			return FCbAttachment(DiffMeta);
		}
		return FCbAttachment();
	};

	const auto SaveAttachment = [&](FCbAttachment&& Attachment)
	{
		const FIoHash Hash = Attachment.GetHash();
		CHECK((Hash == Value.GetRawHash() || Hash == MetaHash || Hash == EmptyBufferHash));
	};

	SECTION("CachePutRequest")
	{
		const FCachePutRequest Request{Name, Record, RecordPolicy, UserData};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, nullptr, SaveAttachment);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		{
			TOptional<FCachePutRequest> LoadedRequest;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest));
			CHECKED_IF(LoadedRequest)
			{
				CHECK(LoadedRequest->Name == Request.Name);
				CHECK(LoadedRequest->Record.GetKey() == Request.Record.GetKey());
				CHECK_FALSE(LoadedRequest->Record.GetMeta());
				CHECK(LoadedRequest->Record.GetValue(ValueId) == Value);
				CHECK_FALSE(LoadedRequest->Record.GetValue(ValueId).HasData());
				CHECK(LoadedRequest->UserData == Request.UserData);
			}
		}

		{
			TOptional<FCachePutRequest> LoadedRequest;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, nullptr, LoadAttachment));
			CHECKED_IF(LoadedRequest)
			{
				CHECK(LoadedRequest->Name == Request.Name);
				CHECK(LoadedRequest->Record.GetKey() == Request.Record.GetKey());
				CHECK(LoadedRequest->Record.GetMeta().Equals(Meta));
				CHECK(LoadedRequest->Record.GetValue(ValueId) == Value);
				CHECK(LoadedRequest->Record.GetValue(ValueId).HasData());
				CHECK(LoadedRequest->UserData == Request.UserData);
			}
		}
	}

	SECTION("CachePutResponse")
	{
		const FCachePutRequest Request{Name, Record, RecordPolicy, UserData};
		PRAGMA_DISABLE_DEPRECATION_WARNINGS;
		const FCachePutResponse Response{Name, Key, Record, UserData, EStatus::Canceled};
		PRAGMA_ENABLE_DEPRECATION_WARNINGS;

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		{
			TOptional<FCachePutRequest> LoadedRequest;
			TOptional<FCachePutResponse> LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse));
			CHECKED_IF(LoadedResponse)
			{
				CHECK(LoadedResponse->Name == Response.Name);
				CHECK(LoadedResponse->Record.GetKey() == Response.Record.GetKey());
				CHECK_FALSE(LoadedResponse->Record.GetMeta());
				CHECK(LoadedResponse->Record.GetValue(ValueId) == Value);
				CHECK_FALSE(LoadedResponse->Record.GetValue(ValueId).HasData());
				CHECK(LoadedResponse->UserData == Response.UserData);
				CHECK(LoadedResponse->Status == Response.Status);
			}
		}

		{
			TOptional<FCachePutRequest> LoadedRequest;
			TOptional<FCachePutResponse> LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
			CHECKED_IF(LoadedResponse)
			{
				CHECK(LoadedResponse->Name == Response.Name);
				CHECK(LoadedResponse->Record.GetKey() == Response.Record.GetKey());
				CHECK(LoadedResponse->Record.GetMeta().Equals(Response.Record.GetMeta()));
				CHECK(LoadedResponse->Record.GetValue(ValueId) == Value);
				CHECK(LoadedResponse->Record.GetValue(ValueId).HasData());
				CHECK(LoadedResponse->UserData == Response.UserData);
				CHECK(LoadedResponse->Status == Response.Status);
			}
		}
	}

	SECTION("CachePutRequestResponse")
	{
		const FCachePutRequest Request{Name, Record, RecordPolicy, UserData};
		PRAGMA_DISABLE_DEPRECATION_WARNINGS;
		const FCachePutResponse Response{Name, Key, DiffRecord, UserData, EStatus::Canceled};
		PRAGMA_ENABLE_DEPRECATION_WARNINGS;

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		TOptional<FCachePutRequest> LoadedRequest;
		TOptional<FCachePutResponse> LoadedResponse;
		CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
		CHECKED_IF(LoadedRequest)
		{
			CHECK(LoadedRequest->Name == Request.Name);
			CHECK(LoadedRequest->Record.GetKey() == Request.Record.GetKey());
			CHECK(LoadedRequest->Record.GetMeta().Equals(Meta));
			CHECK(LoadedRequest->Record.GetValue(ValueId) == Value);
			CHECK(LoadedRequest->Record.GetValue(ValueId).HasData());
			CHECK(LoadedRequest->UserData == Request.UserData);
		}
		CHECKED_IF(LoadedResponse)
		{
			CHECK(LoadedResponse->Name == Response.Name);
			CHECK(LoadedResponse->Record.GetKey() == Response.Record.GetKey());
			CHECK(LoadedResponse->Record.GetMeta().Equals(Response.Record.GetMeta()));
			CHECK(LoadedResponse->Record.GetValue(ValueId) == DiffValue);
			CHECK(LoadedResponse->Record.GetValue(ValueId).HasData());
			CHECK(LoadedResponse->UserData == Response.UserData);
			CHECK(LoadedResponse->Status == Response.Status);
		}
	}

	SECTION("CacheGetRequest")
	{
		const FCacheGetRequest Request{Name, Key, RecordPolicy, UserData};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		FCacheGetRequest LoadedRequest;
		CHECK(LoadFromCompactBinary(Field, LoadedRequest));
		CHECK(LoadedRequest.Name == Request.Name);
		CHECK(LoadedRequest.Key == Request.Key);
		CHECK(LoadedRequest.Policy.GetBasePolicy() == Request.Policy.GetBasePolicy());
		CHECK(LoadedRequest.Policy.GetRecordPolicy() == Request.Policy.GetRecordPolicy());
		CHECK(LoadedRequest.Policy.GetValuePolicy(ValueId) == Request.Policy.GetValuePolicy(ValueId));
		CHECK(LoadedRequest.UserData == Request.UserData);
	}

	SECTION("CacheGetResponse")
	{
		const FCacheGetRequest Request{Name, Key, RecordPolicy, UserData};
		const FCacheGetResponse Response{Name, Record, UserData, EStatus::Canceled};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response, SaveAttachment);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		{
			FCacheGetRequest LoadedRequest;
			TOptional<FCacheGetResponse> LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse));
			CHECKED_IF(LoadedResponse)
			{
				CHECK(LoadedResponse->Name == Response.Name);
				CHECK(LoadedResponse->Record.GetKey() == Response.Record.GetKey());
				CHECK_FALSE(LoadedResponse->Record.GetMeta());
				CHECK(LoadedResponse->Record.GetValue(ValueId) == Value);
				CHECK_FALSE(LoadedResponse->Record.GetValue(ValueId).HasData());
				CHECK(LoadedResponse->UserData == Response.UserData);
				CHECK(LoadedResponse->Status == Response.Status);
			}
		}

		{
			FCacheGetRequest LoadedRequest;
			TOptional<FCacheGetResponse> LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
			CHECKED_IF(LoadedResponse)
			{
				CHECK(LoadedResponse->Name == Response.Name);
				CHECK(LoadedResponse->Record.GetKey() == Response.Record.GetKey());
				CHECK(LoadedResponse->Record.GetMeta().Equals(Meta));
				CHECK(LoadedResponse->Record.GetValue(ValueId) == Value);
				CHECK(LoadedResponse->Record.GetValue(ValueId).HasData());
				CHECK(LoadedResponse->UserData == Response.UserData);
				CHECK(LoadedResponse->Status == Response.Status);
			}
		}
	}

	SECTION("CachePutValueRequest")
	{
		const FCachePutValueRequest Request{Name, Key, Value, ValuePolicy, UserData};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, nullptr, SaveAttachment);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);
		{
			FCachePutValueRequest LoadedRequest;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest));
			CHECK(LoadedRequest.Name == Request.Name);
			CHECK(LoadedRequest.Key == Request.Key);
			CHECK(LoadedRequest.Value == Request.Value);
			CHECK_FALSE(LoadedRequest.Value.HasData());
			CHECK(LoadedRequest.UserData == Request.UserData);
		}

		{
			FCachePutValueRequest LoadedRequest;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, nullptr, LoadAttachment));
			CHECK(LoadedRequest.Name == Request.Name);
			CHECK(LoadedRequest.Key == Request.Key);
			CHECK(LoadedRequest.Value == Request.Value);
			CHECK(LoadedRequest.Value.HasData());
			CHECK(LoadedRequest.UserData == Request.UserData);
		}
	}

	SECTION("CachePutValueResponse")
	{
		const FCachePutValueRequest Request{Name, Key, Value, ValuePolicy, UserData};
		const FCachePutValueResponse Response{Name, Key, Value.RemoveData(), UserData, EStatus::Canceled};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		{
			FCachePutValueRequest LoadedRequest;
			FCachePutValueResponse LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse));
			CHECK(LoadedResponse.Name == Response.Name);
			CHECK(LoadedResponse.Key == Response.Key);
			CHECK(LoadedResponse.Value == Response.Value);
			CHECK_FALSE(LoadedResponse.Value.HasData());
			CHECK(LoadedResponse.UserData == Response.UserData);
			CHECK(LoadedResponse.Status == Response.Status);
		}

		{
			FCachePutValueRequest LoadedRequest;
			FCachePutValueResponse LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
			CHECK(LoadedResponse.Name == Response.Name);
			CHECK(LoadedResponse.Key == Response.Key);
			CHECK(LoadedResponse.Value == Response.Value);
			CHECK(LoadedResponse.Value.HasData());
			CHECK(LoadedResponse.UserData == Response.UserData);
			CHECK(LoadedResponse.Status == Response.Status);
		}
	}

	SECTION("CachePutValueRequestResponse")
	{
		const FCachePutValueRequest Request{Name, Key, Value, ValuePolicy, UserData};
		const FCachePutValueResponse Response{Name, Key, DiffValue, UserData, EStatus::Canceled};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		FCachePutValueRequest LoadedRequest;
		FCachePutValueResponse LoadedResponse;
		CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
		CHECK(LoadedRequest.Name == Request.Name);
		CHECK(LoadedRequest.Key == Request.Key);
		CHECK(LoadedRequest.Value == Request.Value);
		CHECK(LoadedRequest.Value.HasData());
		CHECK(LoadedRequest.UserData == Request.UserData);
		CHECK(LoadedResponse.Name == Response.Name);
		CHECK(LoadedResponse.Key == Response.Key);
		CHECK(LoadedResponse.Value == Response.Value);
		CHECK(LoadedResponse.Value.HasData());
		CHECK(LoadedResponse.UserData == Response.UserData);
		CHECK(LoadedResponse.Status == Response.Status);
	}

	SECTION("CacheGetValueRequest")
	{
		const FCacheGetValueRequest Request{Name, Key, ValuePolicy, UserData};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		FCacheGetValueRequest LoadedRequest;
		CHECK(LoadFromCompactBinary(Field, LoadedRequest));
		CHECK(LoadedRequest.Name == Request.Name);
		CHECK(LoadedRequest.Key == Request.Key);
		CHECK(LoadedRequest.Policy == Request.Policy);
		CHECK(LoadedRequest.UserData == Request.UserData);
	}

	SECTION("CacheGetValueResponse")
	{
		const FCacheGetValueRequest Request{Name, Key, ValuePolicy, UserData};
		const FCacheGetValueResponse Response{Name, Key, Value, UserData, EStatus::Canceled};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response, SaveAttachment);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		{
			FCacheGetValueRequest LoadedRequest;
			FCacheGetValueResponse LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse));
			CHECK(LoadedResponse.Name == Response.Name);
			CHECK(LoadedResponse.Key == Response.Key);
			CHECK(LoadedResponse.Value == Response.Value);
			CHECK_FALSE(LoadedResponse.Value.HasData());
			CHECK(LoadedResponse.UserData == Response.UserData);
			CHECK(LoadedResponse.Status == Response.Status);
		}

		{
			FCacheGetValueRequest LoadedRequest;
			FCacheGetValueResponse LoadedResponse;
			CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
			CHECK(LoadedResponse.Name == Name);
			CHECK(LoadedResponse.Key == Key);
			CHECK(LoadedResponse.Value == Value);
			CHECK(LoadedResponse.Value.HasData());
			CHECK(LoadedResponse.UserData == UserData);
			CHECK(LoadedResponse.Status == Response.Status);
		}
	}

	SECTION("CacheGetChunkRequest")
	{
		const FCacheGetChunkRequest Request{Name, Key, ValueId, 1024, 512, Value.GetRawHash(), ValuePolicy, UserData};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		FCacheGetChunkRequest LoadedRequest;
		CHECK(LoadFromCompactBinary(Field, LoadedRequest));
		CHECK(LoadedRequest.Name == Request.Name);
		CHECK(LoadedRequest.Key == Request.Key);
		CHECK(LoadedRequest.Id == Request.Id);
		CHECK(LoadedRequest.RawOffset == Request.RawOffset);
		CHECK(LoadedRequest.RawSize == Request.RawSize);
		CHECK(LoadedRequest.RawHash == Request.RawHash);
		CHECK(LoadedRequest.Policy == Request.Policy);
		CHECK(LoadedRequest.UserData == Request.UserData);
	}

	SECTION("CacheGetChunkResponse")
	{
		const FCacheGetChunkRequest Request{Name, Key, ValueId, 1024, 512, Value.GetRawHash(), ValuePolicy, UserData};
		const FCacheGetChunkResponse Response{Name, Key, ValueId, 1024, 256, Value.GetRawHash(), EmptyBuffer, UserData, EStatus::Canceled};

		const FCbField OriginalField = [&]
		{
			TCbWriter<512> Writer;
			SaveToCompactBinary(Writer, Request, &Response, SaveAttachment);
			return Writer.Save();
		}();
		const FCbField ReversedField = ReverseObjectFields(OriginalField);
		const FCbField Field = GENERATE_COPY(OriginalField, ReversedField);

		FCacheGetChunkRequest LoadedRequest;
		FCacheGetChunkResponse LoadedResponse;
		CHECK(LoadFromCompactBinary(Field, LoadedRequest, &LoadedResponse, LoadAttachment));
		CHECK(LoadedResponse.Name == Response.Name);
		CHECK(LoadedResponse.Key == Response.Key);
		CHECK(LoadedResponse.Id == Response.Id);
		CHECK(LoadedResponse.RawOffset == Response.RawOffset);
		CHECK(LoadedResponse.RawSize == Response.RawSize);
		CHECK(LoadedResponse.RawHash == Response.RawHash);
		CHECK(LoadedResponse.RawData);
		CHECK(LoadedResponse.RawData.GetSize() == 0);
		CHECK(LoadedResponse.UserData == Response.UserData);
		CHECK(LoadedResponse.Status == Response.Status);
	}

	SECTION("CacheRecord")
	{
		const FCbPackage Package = Record.Save();
		const FOptionalCacheRecord LoadedRecord = FCacheRecord::Load(Package);
		CHECKED_IF(LoadedRecord)
		{
			CHECK(LoadedRecord.Get().GetKey() == Key);
			CHECK(LoadedRecord.Get().GetMeta().Equals(Meta));
			CHECK(LoadedRecord.Get().GetValue(ValueId) == Value);
			CHECK(LoadedRecord.Get().GetValue(ValueId).HasData());
		}
	}
}

} // UE::DerivedData

#endif // WITH_LOW_LEVEL_TESTS
