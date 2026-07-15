// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataLegacyCacheStore.h"

#include "Async/UniqueLock.h"

namespace UE::DerivedData
{

void ILegacyCacheStore::LegacyPut(
	const TConstArrayView<FLegacyCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCachePutComplete&& OnComplete)
{
	struct FKeyWithUserData
	{
		FLegacyCacheKey Key;
		uint64 UserData;
	};

	TArray<FKeyWithUserData, TInlineAllocator<1>> LegacyRequests;
	TArray<FCachePutValueRequest, TInlineAllocator<1>> ValueRequests;
	LegacyRequests.Reserve(Requests.Num());
	ValueRequests.Reserve(Requests.Num());

	uint64 RequestIndex = 0;
	for (const FLegacyCachePutRequest& Request : Requests)
	{
		LegacyRequests.Add({Request.Key, Request.UserData});
		ValueRequests.Add({Request.Name, Request.Key.GetKey(), Request.Value.GetValue(), Request.Policy, RequestIndex++});
	}

	PutValue(ValueRequests, Owner, [LegacyRequests = MoveTemp(LegacyRequests), OnComplete = MoveTemp(OnComplete)](FCachePutValueResponse&& Response)
	{
		const FKeyWithUserData& LegacyRequest = LegacyRequests[int32(Response.UserData)];
		OnComplete({Response.Name, LegacyRequest.Key, LegacyRequest.UserData, Response.Status});
	});
}

void ILegacyCacheStore::LegacyGet(
	const TConstArrayView<FLegacyCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCacheGetComplete&& OnComplete)
{
	struct FKeyWithUserData
	{
		FLegacyCacheKey Key;
		uint64 UserData;
	};

	TArray<FKeyWithUserData, TInlineAllocator<1>> LegacyRequests;
	TArray<FCacheGetValueRequest, TInlineAllocator<1>> ValueRequests;
	LegacyRequests.Reserve(Requests.Num());
	ValueRequests.Reserve(Requests.Num());

	uint64 RequestIndex = 0;
	for (const FLegacyCacheGetRequest& Request : Requests)
	{
		LegacyRequests.Add({Request.Key, Request.UserData});
		ValueRequests.Add({Request.Name, Request.Key.GetKey(), Request.Policy, RequestIndex++});
	}

	GetValue(ValueRequests, Owner, [LegacyRequests = MoveTemp(LegacyRequests), OnComplete = MoveTemp(OnComplete)](FCacheGetValueResponse&& Response)
	{
		const FKeyWithUserData& LegacyRequest = LegacyRequests[int32(Response.UserData)];
		OnComplete({Response.Name, LegacyRequest.Key, FLegacyCacheValue(Response.Value), LegacyRequest.UserData, Response.Status});
	});
}

void ILegacyCacheStore::LegacyDelete(
	const TConstArrayView<FLegacyCacheDeleteRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCacheDeleteComplete&& OnComplete)
{
	CompleteWithStatus(Requests, OnComplete, EStatus::Error);
}

FLegacyCacheKey::FLegacyCacheKey(const FStringView InFullKey)
	: Key(ConvertLegacyCacheKey(InFullKey))
	, FullKey(InFullKey)
{
}

FLegacyCacheValue::FLegacyCacheValue(const FValue& Value)
{
	if (Value.HasData() || !Value.GetRawHash().IsZero())
	{
		Shared = new Private::FLegacyCacheValueShared(Value);
	}
}

FLegacyCacheValue::FLegacyCacheValue(const FCompositeBuffer& RawData)
{
	if (RawData)
	{
		Shared = new Private::FLegacyCacheValueShared(RawData);
	}
}

Private::FLegacyCacheValueShared::FLegacyCacheValueShared(const FValue& InValue)
	: Value(InValue)
{
}

Private::FLegacyCacheValueShared::FLegacyCacheValueShared(const FCompositeBuffer& InRawData)
	: RawData(InRawData.MakeOwned())
{
}

const FValue& Private::FLegacyCacheValueShared::GetValue()
{
	TUniqueLock Lock(Mutex);
	if (!Value.HasData() && !RawData.IsNull())
	{
		Value = FValue::Compress(RawData);
	}
	return Value;
}

const FCompositeBuffer& Private::FLegacyCacheValueShared::GetRawData()
{
	TUniqueLock Lock(Mutex);
	if (RawData.IsNull() && Value.HasData())
	{
		RawData = Value.GetData().DecompressToComposite();
	}
	return RawData;
}

FIoHash Private::FLegacyCacheValueShared::GetRawHash() const
{
	return Value.HasData() ? Value.GetRawHash() : FIoHash::HashBuffer(RawData);
}

uint64 Private::FLegacyCacheValueShared::GetRawSize() const
{
	return Value.HasData() ? Value.GetRawSize() : RawData.GetSize();
}

FLegacyCachePutResponse FLegacyCachePutRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, UserData, Status};
}

FLegacyCacheGetResponse FLegacyCacheGetRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, {}, UserData, Status};
}

FLegacyCacheDeleteResponse FLegacyCacheDeleteRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, UserData, Status};
}

} // UE::DerivedData
