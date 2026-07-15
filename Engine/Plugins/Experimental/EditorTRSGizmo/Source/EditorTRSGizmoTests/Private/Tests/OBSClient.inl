// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/ForEach.h"
#include "IWebSocket.h"
#include "OBSClient.h"
#include "OBSUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogOBSClient, Log, All);

namespace OBS
{
	template <typename RequestType, typename ResultType>
	TFuture<TValueOrError<ResultType, void>> FOBSClient::Send(RequestType&& InRequest)
	{
		static_assert(std::is_base_of_v<FOperation, RequestType>, "RequestType must be a subclass of OBS::FOperation");

		if (!WebSocket.IsValid() || !WebSocket->IsConnected())
		{
			UE_LOGF(LogOBSClient, Error, "WebSocket is not connected");
			return MakeFulfilledPromise<TValueOrError<ResultType, void>>(MakeError()).GetFuture();
		}

		TSharedPtr<TPromise<TValueOrError<ResultType, void>>> Promise = MakeShared<TPromise<TValueOrError<ResultType, void>>>();

		constexpr bool bIsRequestResponse = !(std::is_same_v<ResultType, bool> || std::is_same_v<ResultType, void>);

		InRequest.Id = GenerateRequestId();

		const FString EncodedRequest = InRequest.Encode();
		if (EncodedRequest.IsEmpty())
		{
			UE_LOGF(LogOBSClient, Error, "Failed to encode request: %ls", *InRequest.OperationName);
			Promise->SetValue(MakeError());
			return Promise->GetFuture();
		}

		// bool or void return types don't expect a result for the given request
		if constexpr (bIsRequestResponse)
		{
			WhenMessageReceived(EOperationCode::RequestResponse, InRequest.OperationName, InRequest.Id)
			.Next([Promise](const TValueOrError<TSharedPtr<FJsonObject>, void>& InJsonMessage)
			{
				if (InJsonMessage.HasError())
				{
					Promise->SetValue(MakeError());
					return;
				}

				auto GetResponseDataObject = [JsonMessage = InJsonMessage.GetValue()]() -> TSharedPtr<FJsonObject>
				{
					// The inner data payload is d->responseData
					const TSharedPtr<FJsonObject>* DataObject = nullptr;
					if (!JsonMessage->TryGetObjectField(TEXT("d"), DataObject))
					{
						return nullptr;
					}

					const TSharedPtr<FJsonObject>* ResponseDataObject = nullptr;
					if (!(*DataObject)->TryGetObjectField(TEXT("responseData"), ResponseDataObject))
					{
						return *DataObject;
					}

					return *ResponseDataObject;
				};

				if constexpr (std::is_same_v<TSharedPtr<FJsonObject>, ResultType>)
				{
					if (const TSharedPtr<FJsonObject> ResponseDataObject = GetResponseDataObject())
					{
						Promise->SetValue(MakeValue(ResponseDataObject));
					}
					else
					{
						Promise->SetValue(MakeError());
					}
				}
				else if constexpr (requires { { ResultType::Decode(std::declval<TSharedPtr<FJsonObject>>()) } -> std::convertible_to<ResultType>; })
				{
					if (const TSharedPtr<FJsonObject> ResponseDataObject = GetResponseDataObject())
					{
						Promise->SetValue(MakeValue(ResultType::Decode(ResponseDataObject)));
					}
					else
					{
						Promise->SetValue(MakeError());
					}
				}
				else
				{
					Promise->SetValue(MakeError());
				}
			});
		}

		WhenMessageSent(InRequest.OperationCode, InRequest.Id)
		.Next([Promise](const bool bWasSent)
		{
			// Probably timed out
			if (!bWasSent)
			{
				Promise->SetValue(MakeError());
				return;
			}

			// Only flag as successful if no response expected
			if constexpr (!bIsRequestResponse)
			{
				if constexpr (std::is_same_v<ResultType, bool>)
				{
					Promise->SetValue(MakeValue(true));
				}
				else if constexpr (std::is_same_v<ResultType, void>)
				{
					Promise->SetValue(MakeValue());
				}
			}
		});

		WebSocket->Send(EncodedRequest);

		return Promise->GetFuture();
	}

	template <typename RequestType>
	TFuture<bool> FOBSClient::Send(RequestType&& InRequest)
	{
		return Send<RequestType, void>(Forward<RequestType>(InRequest))
		.Next([](const TValueOrError<void, void>& InResponse)
		{
			return !InResponse.HasError();
		});
	}

	template <typename RequestType, typename ResultType>
	TFuture<TValueOrError<TMap<int32, ResultType>, void>> FOBSClient::SendBatch(TArray<RequestType>& InRequests)
	{
		static_assert(std::is_base_of_v<FOperation, RequestType>, "RequestType must be a subclass of OBS::FOperation");

		if (!WebSocket.IsValid() || !WebSocket->IsConnected())
		{
			UE_LOGF(LogOBSClient, Error, "WebSocket is not connected");
			return MakeFulfilledPromise<TValueOrError<TMap<int32, ResultType>, void>>(MakeError()).GetFuture();
		}

		TSharedPtr<TPromise<TValueOrError<TMap<int32, ResultType>, void>>> Promise = MakeShared<TPromise<TValueOrError<TMap<int32, ResultType>, void>>>();

		constexpr bool bIsRequestResponse = !(std::is_same_v<ResultType, bool> || std::is_same_v<ResultType, void>);

		const int32 RequestId = GenerateRequestId();

		Algo::ForEach(InRequests, [](RequestType& InRequest)
		{
			InRequest.Id = GenerateRequestId();
		});

		TBatchRequest<RequestType> BatchRequest(InRequests);
		BatchRequest.Id = RequestId;

		const FString EncodedRequest = BatchRequest.Encode();
		if (EncodedRequest.IsEmpty())
		{
			UE_LOGF(LogOBSClient, Error, "Failed to encode batch request");
			Promise->SetValue(MakeError());
			return Promise->GetFuture();
		}

		// bool or void return types don't expect a result for the given request
		if constexpr (bIsRequestResponse)
		{
			WhenMessageReceived(EOperationCode::RequestBatchResponse, RequestId)
			.Next([Promise](const TValueOrError<TSharedPtr<FJsonObject>, void>& InJsonMessage)
			{
				if (InJsonMessage.HasError())
				{
					Promise->SetValue(MakeError());
					return;
				}

				// ItemRequestId, Data pair
				auto GetResponseDataMap = [JsonMessage = InJsonMessage.GetValue()]() -> TMap<int32, TSharedPtr<FJsonObject>>
				{
					// The inner data payload is d->responseData
					const TSharedPtr<FJsonObject>* DataObject = nullptr;
					if (!JsonMessage->TryGetObjectField(TEXT("d"), DataObject))
					{
						return { };
					}

					const TArray<TSharedPtr<FJsonValue>>* ResultsDataValue = nullptr;
					if (!(*DataObject)->TryGetArrayField(TEXT("results"), ResultsDataValue))
					{
						return { };
					}

					TMap<int32, TSharedPtr<FJsonObject>> ResponseDataObjects;
					ResponseDataObjects.Reserve(ResultsDataValue->Num());

					for (const TSharedPtr<FJsonValue>& ResultDataValue : *ResultsDataValue)
					{
						const TSharedPtr<FJsonObject> ValueObject = ResultDataValue->AsObject();

						int32 ItemRequestId = INDEX_NONE;
						if (ValueObject->TryGetNumberField(TEXT("requestId"), ItemRequestId))
						{
							const TSharedPtr<FJsonObject>* ResponseDataObject = nullptr;
							if (ValueObject->TryGetObjectField(TEXT("responseData"), ResponseDataObject))
							{
								ResponseDataObjects.Emplace(ItemRequestId, *ResponseDataObject);
								continue;
							}

							ResponseDataObjects.Emplace(ItemRequestId, ValueObject);
						}
					}

					return ResponseDataObjects;
				};

				if constexpr (std::is_same_v<TSharedPtr<FJsonObject>, ResultType>)
				{
					if (const TMap<int32, TSharedPtr<FJsonObject>>& ResponseDataObjects = GetResponseDataMap();
						!ResponseDataObjects.IsEmpty())
					{
						Promise->SetValue(MakeValue(ResponseDataObjects));
					}
					else
					{
						Promise->SetValue(MakeError());
					}
				}
				else if constexpr (requires { { ResultType::Decode(std::declval<TSharedPtr<FJsonObject>>()) } -> std::convertible_to<ResultType>; })
				{
					if (const TMap<int32, TSharedPtr<FJsonObject>>& ResponseDataObjects = GetResponseDataMap();
						!ResponseDataObjects.IsEmpty())
					{
						TMap<int32, ResultType> Results;
						Results.Reserve(ResponseDataObjects.Num());

						Algo::Transform(ResponseDataObjects, Results, [](const TPair<int32, TSharedPtr<FJsonObject>>& InObject) -> TPair<int32, ResultType>
						{
							return { InObject.Key, ResultType::Decode(InObject.Value) };
						});

						Promise->SetValue(MakeValue(Results));
					}
					else
					{
						Promise->SetValue(MakeError());
					}
				}
				else
				{
					Promise->SetValue(MakeError());
				}
			});
		}

		WhenMessageSent(BatchRequest.OperationCode, BatchRequest.Id)
		.Next([Promise](const bool bWasSent)
		{
			// Probably timed out
			if (!bWasSent)
			{
				Promise->SetValue(MakeError());
				return;
			}

			// Only flag as successful if no response expected
			if constexpr (!bIsRequestResponse)
			{
				if constexpr (std::is_same_v<ResultType, bool>)
				{
					Promise->SetValue(MakeValue(true));
				}
				else if constexpr (std::is_same_v<ResultType, void>)
				{
					Promise->SetValue(MakeValue());
				}
			}
		});

		WebSocket->Send(EncodedRequest);

		return Promise->GetFuture();
	}

	template <typename RequestType>
	TFuture<bool> FOBSClient::SendBatch(TArray<RequestType>& InRequests)
	{
		// TArray<void> isn't valid, so use an (unused) FJsonObject result.
		using ResultType = TSharedPtr<FJsonObject>;
		return SendBatch<RequestType, ResultType>(InRequests)
		.Next([](const TValueOrError<TMap<int32, ResultType>, void>& InResponse)
		{
			return !InResponse.HasError();
		});
	}

	template <typename ReturnType>
	FOBSClient::TMessageHandle<ReturnType>::TMessageHandle(const int32 InRequestId)
		: FMessageHandle(InRequestId)
		, Promise(MakeShared<TPromise<ReturnType>>())
	{
	}

	template <typename ReturnType>
	void FOBSClient::TMessageHandle<ReturnType>::Complete(TMulticastDelegate<void(const FString&)>& InDelegate, ReturnType&& InResult)
	{
		if (bIsCompleted.load())
		{
			return;
		}

		bIsCompleted.store(true);

		FScopeLock Lock(&PromiseLock);

		if (DelegateHandle.IsValid() && DelegateHandle->IsValid())
		{
			InDelegate.Remove(*DelegateHandle);
		}

		if (Promise.IsValid())
		{
			if constexpr (std::is_same_v<ReturnType, bool>)
			{
				Promise->SetValue(Forward<ReturnType>(InResult));
			}
			else if constexpr (std::is_same_v<std::decay_t<ReturnType>, TValueOrError<TSharedPtr<FJsonObject>, void>>)
			{
				Promise->SetValue(Forward<ReturnType>(InResult));
			}
		}
	}

	template <typename ReturnType>
	void FOBSClient::TMessageHandle<ReturnType>::Fail(TMulticastDelegate<void(const FString&)>& InDelegate)
	{
		if (bIsCompleted.load())
		{
			return;
		}

		bIsCompleted.store(true);

		FScopeLock Lock(&PromiseLock);

		if (DelegateHandle.IsValid() && DelegateHandle->IsValid())
		{
			InDelegate.Remove(*DelegateHandle);
		}

		if (Promise.IsValid())
		{
			if constexpr (std::is_same_v<ReturnType, bool>)
			{
				Promise->SetValue(false);
			}
			else if constexpr (std::is_same_v<std::decay_t<ReturnType>, TValueOrError<TSharedPtr<FJsonObject>, void>>)
			{
				Promise->SetValue(MakeError());
			}
		}
	}
}
