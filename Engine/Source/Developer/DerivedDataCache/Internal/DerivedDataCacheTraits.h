// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCache.h"
#include "DerivedDataCacheMethod.h"

namespace UE::DerivedData::Private
{

template <typename RequestOrResponseType>
struct TCacheRequestResponseTraits;

template <>
struct TCacheRequestResponseTraits<FCachePutRequest>
{
	using FRequest = FCachePutRequest;
	using FResponse = FCachePutResponse;
	using FOnComplete = FOnCachePutComplete;
	constexpr static ECacheMethod Method = ECacheMethod::Put;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::Put;
	static const FCacheKey& GetKey(const FCachePutRequest& Request) { return Request.Record.GetKey(); }
};

template <>
struct TCacheRequestResponseTraits<FCacheGetRequest>
{
	using FRequest = FCacheGetRequest;
	using FResponse = FCacheGetResponse;
	using FOnComplete = FOnCacheGetComplete;
	constexpr static ECacheMethod Method = ECacheMethod::Get;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::Get;
	static const FCacheKey& GetKey(const FCacheGetRequest& Request) { return Request.Key; }
};

template <>
struct TCacheRequestResponseTraits<FCachePutValueRequest>
{
	using FRequest = FCachePutValueRequest;
	using FResponse = FCachePutValueResponse;
	using FOnComplete = FOnCachePutValueComplete;
	constexpr static ECacheMethod Method = ECacheMethod::PutValue;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::PutValue;
	static const FCacheKey& GetKey(const FCachePutValueRequest& Request) { return Request.Key; }
};

template <>
struct TCacheRequestResponseTraits<FCacheGetValueRequest>
{
	using FRequest = FCacheGetValueRequest;
	using FResponse = FCacheGetValueResponse;
	using FOnComplete = FOnCacheGetValueComplete;
	constexpr static ECacheMethod Method = ECacheMethod::GetValue;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::GetValue;
	static const FCacheKey& GetKey(const FCacheGetValueRequest& Request) { return Request.Key; }
};

template <>
struct TCacheRequestResponseTraits<FCacheGetChunkRequest>
{
	using FRequest = FCacheGetChunkRequest;
	using FResponse = FCacheGetChunkResponse;
	using FOnComplete = FOnCacheGetChunkComplete;
	constexpr static ECacheMethod Method = ECacheMethod::GetChunks;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::GetChunks;
	static const FCacheKey& GetKey(const FCacheGetChunkRequest& Request) { return Request.Key; }
};

template <>
struct TCacheRequestResponseTraits<FCachePutResponse>
{
	using FRequest = FCachePutRequest;
	using FResponse = FCachePutResponse;
	using FOnComplete = FOnCachePutComplete;
	constexpr static ECacheMethod Method = ECacheMethod::Put;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::Put;
	static const FCacheKey& GetKey(const FCachePutResponse& Response) { return Response.Record.GetKey(); }
};

template <>
struct TCacheRequestResponseTraits<FCacheGetResponse>
{
	using FRequest = FCacheGetRequest;
	using FResponse = FCacheGetResponse;
	using FOnComplete = FOnCacheGetComplete;
	constexpr static ECacheMethod Method = ECacheMethod::Get;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::Get;
	static const FCacheKey& GetKey(const FCacheGetResponse& Response) { return Response.Record.GetKey(); }
};

template <>
struct TCacheRequestResponseTraits<FCachePutValueResponse>
{
	using FRequest = FCachePutValueRequest;
	using FResponse = FCachePutValueResponse;
	using FOnComplete = FOnCachePutValueComplete;
	constexpr static ECacheMethod Method = ECacheMethod::PutValue;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::PutValue;
	static const FCacheKey& GetKey(const FCachePutValueResponse& Response) { return Response.Key; }
};

template <>
struct TCacheRequestResponseTraits<FCacheGetValueResponse>
{
	using FRequest = FCacheGetValueRequest;
	using FResponse = FCacheGetValueResponse;
	using FOnComplete = FOnCacheGetValueComplete;
	constexpr static ECacheMethod Method = ECacheMethod::GetValue;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::GetValue;
	static const FCacheKey& GetKey(const FCacheGetValueResponse& Response) { return Response.Key; }
};

template <>
struct TCacheRequestResponseTraits<FCacheGetChunkResponse>
{
	using FRequest = FCacheGetChunkRequest;
	using FResponse = FCacheGetChunkResponse;
	using FOnComplete = FOnCacheGetChunkComplete;
	constexpr static ECacheMethod Method = ECacheMethod::GetChunks;
	constexpr static void (ICache::*Function)(TConstArrayView<FRequest>, IRequestOwner&, FOnComplete&&) = &ICache::GetChunks;
	static const FCacheKey& GetKey(const FCacheGetChunkResponse& Response) { return Response.Key; }
};

} // UE::DerivedData::Private

namespace UE::DerivedData
{

/** Alias for the request type corresponding to the request/response type. */
template <typename RequestOrResponseType> using TCacheRequestFor = typename Private::TCacheRequestResponseTraits<RequestOrResponseType>::FRequest;

/** Alias for the response type corresponding to the request/response type. */
template <typename RequestOrResponseType> using TCacheResponseFor = typename Private::TCacheRequestResponseTraits<RequestOrResponseType>::FResponse;

/** Alias for the completion callback type corresponding to the request/response type. */
template <typename RequestOrResponseType> using TCacheOnCompleteFor = typename Private::TCacheRequestResponseTraits<RequestOrResponseType>::FOnComplete;

/** Alias for the cache method corresponding to the request/response type. */
template <typename RequestOrResponseType> inline constexpr auto CacheMethodFor = Private::TCacheRequestResponseTraits<RequestOrResponseType>::Method;

/** Alias for the cache function corresponding to the request/response type. */
template <typename RequestOrResponseType> inline constexpr auto CacheFunctionFor = Private::TCacheRequestResponseTraits<RequestOrResponseType>::Function;

template <typename RequestOrResponseType>
inline auto GetCacheKey(const RequestOrResponseType& Request) -> decltype(Private::TCacheRequestResponseTraits<RequestOrResponseType>::GetKey(Request))
{
	return Private::TCacheRequestResponseTraits<RequestOrResponseType>::GetKey(Request);
}

} // UE::DerivedData
