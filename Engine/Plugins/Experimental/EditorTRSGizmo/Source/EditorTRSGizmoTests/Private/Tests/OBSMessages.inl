// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/Transform.h"
#include "OBSMessages.h"
#include "OBSUtils.h"

namespace OBS
{
	template <typename RequestType UE_REQUIRES(std::is_base_of_v<FOperation, RequestType>)>
	TBatchRequest<RequestType>::TBatchRequest(const TArray<RequestType>& InRequests)
		: FOperation(EOperationCode::RequestBatch, TEXT("BatchRequest"))
		, Requests(InRequests)
	{
	}

	template <typename RequestType UE_REQUIRES(std::is_base_of_v<FOperation, RequestType>)>
	FString TBatchRequest<RequestType>::Encode() const
	{
		TSharedPtr<FJsonObject> DataObject = nullptr; // "d"
		const TSharedPtr<FJsonObject> RootObject = EncodeInternal(nullptr, DataObject);
	
		if (!ensureAlways(DataObject.IsValid()))
		{
			return FString();
		}

		TArray<TSharedPtr<FJsonValue>> RequestObjects;
		RequestObjects.Reserve(Requests.Num());

		Algo::Transform(Requests, RequestObjects, [](const RequestType& InRequest) -> TSharedPtr<FJsonValue>
		{
			const TSharedPtr<FJsonObject> RequestObject = MakeShared<FJsonObject>();

			RequestObject->SetNumberField(TEXT("requestId"), InRequest.Id);
			RequestObject->SetStringField(TEXT("requestType"), InRequest.OperationName);
		
			const TSharedPtr<FJsonObject> RequestDataObject = MakeShared<FJsonObject>();
			InRequest.EncodeToObject(RequestDataObject);

			RequestObject->SetObjectField(TEXT("requestData"), RequestDataObject);

			return MakeShared<FJsonValueObject>(RequestObject);
		});

		constexpr const TCHAR* RequestsFieldName = TEXT("requests");
		DataObject->SetArrayField(RequestsFieldName, RequestObjects);

		return JsonToString(RootObject.ToSharedRef());
	}
}
