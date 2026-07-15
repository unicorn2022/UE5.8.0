// Copyright Epic Games, Inc. All Rights Reserved.

#include "OBSClient.h"
#include "OBSClient.inl"

#include "Algo/Transform.h"
#include "HAL/PlatformTime.h"
#include "IWebSocket.h"
#include "JsonUtils/JsonConversion.h"
#include "OBSMessages.h"
#include "OBSMessages.inl"
#include "OBSUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WebSocketsModule.h"

namespace OBS
{
	std::atomic_int FOBSClient::RequestIdCounter = 1;
	const uint64 FOBSClient::MessageWaitTimeoutCycles = MessageWaitTimeoutSeconds * (1.0 / FPlatformTime::GetSecondsPerCycle64());

	FOBSClient::FMessageHandle::FMessageHandle(const int32 InRequestId)
		: RequestId(InRequestId)
		, Timestamp(FPlatformTime::Cycles64())
		, DelegateHandle(MakeShared<FDelegateHandle>())
	{
	}

	FOBSClient::~FOBSClient()
	{
		Close();
	}

	FString FOBSClient::GetUrl() const
	{
		return FString::Format(TEXT("ws://{0}:{1}"), {
			Address.ToString(),
			Port });
	}

	bool FOBSClient::IsConnected() const
	{
		return WebSocket.IsValid() && WebSocket->IsConnected();
	}

	void FOBSClient::Tick(float DeltaTime)
	{
		// Run cleanup every MessageWaitTimeoutSeconds
		if (FPlatformTime::Cycles64() > LastTickTime + MessageWaitTimeoutCycles)
		{
			CleanupMessages(false);

			LastTickTime = FPlatformTime::Cycles64();
		}
	}

	ETickableTickType FOBSClient::GetTickableTickType() const
	{
		return ETickableTickType::Conditional;
	}

	bool FOBSClient::IsTickable() const
	{
		return !(MessageSentDelegateHandles.IsEmpty() && MessageReceivedDelegateHandles.IsEmpty());
	}

	TStatId FOBSClient::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FOBSClient, STATGROUP_Tickables);
	}

	int32 FOBSClient::GenerateRequestId()
	{
		return ++RequestIdCounter;
	}

	void FOBSClient::CleanupMessages(const bool bInForceAll)
	{
		if (MessageSentDelegateHandles.IsEmpty() && MessageReceivedDelegateHandles.IsEmpty())
		{
			return;
		}
		
		const uint64 CurrentTime = FPlatformTime::Cycles64();

		auto TimedOut = [CurrentTime, bInForceAll](const TSharedPtr<FMessageHandle>& InHandle) -> bool
		{
			return bInForceAll || (CurrentTime - InHandle->Timestamp) > MessageWaitTimeoutCycles;
		};

		TArray<TSharedPtr<FSentMessageHandle>> LocalMessageSentHandles;
		TArray<TSharedPtr<FReceivedMessageHandle>> LocalMessageReceivedHandles;
		
		{
			FScopeLock Lock(&HandleLock);
			LocalMessageSentHandles = MessageSentDelegateHandles;
			LocalMessageReceivedHandles = MessageReceivedDelegateHandles;	
		}

		Algo::ForEachIf(
		LocalMessageSentHandles,
		TimedOut,
		[&](const TSharedPtr<FSentMessageHandle>& InHandle)
		{
			InHandle->Fail(OnMessageSentDelegate);
		});

		Algo::ForEachIf(
		LocalMessageReceivedHandles,
		TimedOut,
		[&](const TSharedPtr<FReceivedMessageHandle>& InHandle)
		{
			InHandle->Fail(OnMessageReceivedDelegate);
		});

		auto TimedOutOrCompleted = [bInForceAll, TimedOut](const TSharedPtr<FMessageHandle>& InHandle) -> bool
		{
			return bInForceAll || TimedOut(InHandle) || InHandle->bIsCompleted.load();
		};

		{
			FScopeLock Lock(&HandleLock);
			// Could probably re-use the removal filtering from above
			MessageSentDelegateHandles.RemoveAllSwap(TimedOutOrCompleted);
			MessageReceivedDelegateHandles.RemoveAllSwap(TimedOutOrCompleted);
		}
	}

	TFuture<TValueOrError<FGetRecordStatusResponse, void>> FOBSClient::GetRecordStatus()
	{
		return Send<FGetRecordStatusRequest, TSharedPtr<FJsonObject>>(FGetRecordStatusRequest{})
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<FGetRecordStatusResponse, void>
		{
			if (InResponse.HasValue())
			{
				FGetRecordStatusResponse Response = FGetRecordStatusResponse::Decode(InResponse.GetValue());
				return MakeValue(Response);
			}

			return MakeError();
		});
	}

	TFuture<bool> FOBSClient::StartRecord()
	{
		return Send<FStartRecordRequest>(FStartRecordRequest{});
	}

	TFuture<bool> FOBSClient::StopRecord()
	{
		return Send<FStopRecordRequest>(FStopRecordRequest());
	}

	TFuture<bool> FOBSClient::PauseRecord()
	{
		return Send<FPauseRecordRequest>(FPauseRecordRequest());
	}

	TFuture<bool> FOBSClient::ResumeRecord()
	{
		return Send<FResumeRecordRequest>(FResumeRecordRequest());
	}

	TFuture<TValueOrError<FString, void>> FOBSClient::GetProfileParameter(const FString& InCategory, const FString& InName)
	{
		return Send<FGetProfileParameterRequest, TSharedPtr<FJsonObject>>(FGetProfileParameterRequest(InCategory, InName))
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<FString, void>
		{
			if (InResponse.HasValue())
			{
				FString ParameterValue;
				InResponse.GetValue()->TryGetStringField(TEXT("parameterValue"), ParameterValue);

				return MakeValue(ParameterValue);
			}

			return MakeError();
		});
	}

	TFuture<bool> FOBSClient::SetProfileParameter(const FString& InCategory, const FString& InName, const FString& InValue)
	{
		return Send<FSetProfileParameterRequest>(FSetProfileParameterRequest(InCategory, InName, InValue));
	}

	TFuture<TValueOrError<TArray<FParameter>, void>> FOBSClient::GetProfileParameters(TConstArrayView<FParameter> InParameters)
	{
		TArray<FGetProfileParameterRequest> Requests;
		Requests.Reserve(InParameters.Num());
	
		Algo::Transform(InParameters, Requests, [](const FParameter& InParameter)
		{
			return FGetProfileParameterRequest(InParameter.Category, InParameter.Name);
		});

		return SendBatch<FGetProfileParameterRequest, TSharedPtr<FJsonObject>>(Requests)
		.Next([RequestedParameters = TArray<FParameter>(InParameters), Requests](const TValueOrError<TMap<int32, TSharedPtr<FJsonObject>>, void>& InResponse) -> TValueOrError<TArray<FParameter>, void>
		{
			if (InResponse.HasValue())
			{
				TArray<FParameter> ResponseParameters;
				ResponseParameters.Reserve(RequestedParameters.Num());

				for (const TPair<int32, TSharedPtr<FJsonObject>>& ResponseObjectPair : InResponse.GetValue())
				{
					const TSharedPtr<FJsonObject>& ResponseObject = ResponseObjectPair.Value;

					FString ParameterValue;
					ResponseObject->TryGetStringField(TEXT("parameterValue"), ParameterValue);

					if (const FGetProfileParameterRequest* FoundRequest = Requests.FindByPredicate(
						[RequestId = ResponseObjectPair.Key](const FGetProfileParameterRequest& InRequest)
						{
							return InRequest.Id == RequestId;
						}))
					{
						FParameter Parameter = FoundRequest->Parameter;
						Parameter.Value = ParameterValue;
						ResponseParameters.Emplace(Parameter);
					}
				}

				return MakeValue(ResponseParameters);
			}

			return MakeError();
		});
	}

	TFuture<bool> FOBSClient::SetProfileParameters(TConstArrayView<FParameter> InParameters)
	{
		TArray<FSetProfileParameterRequest> Requests;
		Requests.Reserve(InParameters.Num());
	
		Algo::Transform(InParameters, Requests, [](const FParameter& InParameter)
		{
			return FSetProfileParameterRequest(InParameter.Category, InParameter.Name, InParameter.Value.Get(FString()));
		});

		return SendBatch<FSetProfileParameterRequest>(Requests);
	}

	TFuture<TValueOrError<FGetSceneListResponse, void>> FOBSClient::GetSceneList()
	{
		return Send<FGetSceneListRequest, FGetSceneListResponse>({});
	}

	TFuture<TValueOrError<FScene, void>> FOBSClient::CreateScene(const FString& InSceneName)
	{
		return Send<FCreateSceneRequest, TSharedPtr<FJsonObject>>(FCreateSceneRequest(InSceneName))
		.Next([InSceneName](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<FScene, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}

			FScene Scene;
			Scene.Name = InSceneName;

			InResponse.GetValue()->TryGetStringField(TEXT("sceneUuid"), Scene.UniqueId);

			return MakeValue(Scene);
		});
	}

	TFuture<TValueOrError<FScene, void>> FOBSClient::GetCurrentProgramScene()
	{
		return Send<FGetCurrentProgramSceneRequest, TSharedPtr<FJsonObject>>(FGetCurrentProgramSceneRequest())
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<FScene, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}

			FScene Scene;

			InResponse.GetValue()->TryGetStringField(TEXT("sceneName"), Scene.Name);
			InResponse.GetValue()->TryGetStringField(TEXT("sceneUuid"), Scene.UniqueId);

			return MakeValue(Scene);
		});
	}

	TFuture<bool> FOBSClient::SetCurrentProgramScene(const FScene& InScene)
	{
		return Send<FSetCurrentProgramSceneRequest>(FSetCurrentProgramSceneRequest(InScene));
	}

	TFuture<TValueOrError<TArray<FSceneItem>, void>> FOBSClient::GetSceneItemList(const FScene& InScene)
	{
		return Send<FGetSceneItemListRequest, TSharedPtr<FJsonObject>>(FGetSceneItemListRequest(InScene))
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<TArray<FSceneItem>, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}

			TArray<FSceneItem> SceneItems;

			const TArray<TSharedPtr<FJsonValue>>* ResponseItems;
			if (InResponse.GetValue()->TryGetArrayField(TEXT("sceneItems"), ResponseItems))
			{
				SceneItems.Reserve(ResponseItems->Num());
				for (const TSharedPtr<FJsonValue>& ResponseItem : *ResponseItems)
				{
					FSceneItem SceneItem = FSceneItem::Decode(ResponseItem->AsObject());
					SceneItems.Emplace(SceneItem);
				}
			}

			return MakeValue(SceneItems);
		});
	}

	TFuture<TValueOrError<TArray<TSharedPtr<FJsonObject>>, void>> FOBSClient::GetInputList(const FString& InInputKind)
	{
		return Send<FGetInputListRequest, TSharedPtr<FJsonObject>>(FGetInputListRequest(InInputKind))
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<TArray<TSharedPtr<FJsonObject>>, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}

			const TArray<TSharedPtr<FJsonValue>>* InputItems;
			if (InResponse.GetValue()->TryGetArrayField(TEXT("inputs"), InputItems))
			{
				TArray<TSharedPtr<FJsonObject>> InputItemsArray;
				InputItemsArray.Reserve(InputItems->Num());
				for (const TSharedPtr<FJsonValue>& InputItem : *InputItems)
				{
					InputItemsArray.Emplace(InputItem->AsObject());
				}
			
				return MakeValue(InputItemsArray);
			}

			return MakeError();		
		});
	}

	TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> FOBSClient::GetInputSettings(const FString& InInputName, const FString& InInputUniqueId)
	{
		return Send<FGetInputSettingsRequest, TSharedPtr<FJsonObject>>(FGetInputSettingsRequest(InInputName, InInputUniqueId))
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<TSharedPtr<FJsonObject>, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}
			
			return MakeValue(InResponse.GetValue());
		});
	}

	TFuture<bool> FOBSClient::SetInputSettings(const FString& InInputName, const FString& InInputUniqueId, const TSharedPtr<FJsonObject>& InInputSettings)
	{
		return Send<FSetInputSettingsRequest, void>(FSetInputSettingsRequest(InInputName, InInputUniqueId, InInputSettings))
		.Next([](const TValueOrError<void, void>& InResponse)
		{
			return !InResponse.HasError();
		});
	}

	TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> FOBSClient::GetInputDefaultSettings(const FString& InInputKind)
	{
		return Send<FGetInputDefaultSettingsRequest, TSharedPtr<FJsonObject>>(FGetInputDefaultSettingsRequest(InInputKind))
		.Next([](const TValueOrError<TSharedPtr<FJsonObject>, void>& InResponse) -> TValueOrError<TSharedPtr<FJsonObject>, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}

			return MakeValue(InResponse.GetValue());
		});
	}

	TFuture<TValueOrError<FSceneItem, void>> FOBSClient::CreateInput(
		const FScene& InScene,
		const FString& InInputName,
		const FString& InInputKind,
		const TSharedPtr<FJsonObject>& InInputSettings,
		const bool bIsSceneItemEnabled)
	{
		return Send<FCreateInputRequest, FSceneItem>(FCreateInputRequest(InScene, InInputName, InInputKind, InInputSettings, bIsSceneItemEnabled))
		.Next([](const TValueOrError<FSceneItem, void>& InResponse) -> TValueOrError<FSceneItem, void>
		{
			if (InResponse.HasError())
			{
				return MakeError();
			}

			return MakeValue(InResponse.GetValue());
		});
	}

	TFuture<bool> FOBSClient::SetSceneItemTransform(const FScene& InScene, const FSceneItem& InSceneItem, const FSceneItem::FSceneItemTransform& InTransform)
	{
		return Send<FSetSceneItemTransformRequest, void>(FSetSceneItemTransformRequest(InScene, InSceneItem, InTransform))
		.Next([](const TValueOrError<void, void>& InResponse)
		{
			return !InResponse.HasError();
		});
	}

	TFuture<bool> FOBSClient::CreateRecordChapter(const FString& InChapterName)
	{
		return Send<FCreateRecordChapterRequest>(FCreateRecordChapterRequest(InChapterName));
	}

	TFuture<bool> FOBSClient::Connect()
	{
		if (IsConnected())
		{
			// Already connected
			return MakeFulfilledPromise<bool>(true).GetFuture();
		}
	
		if (bIsConnecting)
		{
			UE_LOGF(LogOBSClient, Warning, "Already connecting to %ls", *GetUrl());
			return MakeFulfilledPromise<bool>(false).GetFuture();
		}

		bIsConnecting.store(true);

		TSharedPtr<TPromise<bool>> ConnectPromise = MakeShared<TPromise<bool>>();
		if (!WebSocket.IsValid())
		{
			WebSocket = FWebSocketsModule::Get().CreateWebSocket(GetUrl());

			WebSocket->OnConnected().AddLambda([WeakThis = AsWeak()]()
			{
				if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
				{
					StrongThis->OnConnectedDelegate.Broadcast();	
				}
			});

			WebSocket->OnConnectionError().AddLambda([WeakThis = AsWeak()](const FString& InError)
			{
				if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
				{
					StrongThis->OnConnectionErrorDelegate.Broadcast(InError);
				}
			});

			WebSocket->OnMessageSent().AddLambda([WeakThis = AsWeak()](const FString& InMessage)
			{
				if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
				{
					StrongThis->OnMessageSentDelegate.Broadcast(InMessage);
				}
			});

			WebSocket->OnMessage().AddLambda([WeakThis = AsWeak()](const FString& InMessage)
			{
				if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
				{
					StrongThis->OnMessageReceivedDelegate.Broadcast(InMessage);
				}
			});
		}

		if (!WebSocket->IsConnected())
		{
			struct FConnectionHandles
			{
			    FCriticalSection Lock;
			    FDelegateHandle ConnectedHandle;
			    FDelegateHandle ConnectionErrorHandle;
			};
			
			// Lock to store both handles safely and prevent race conditions
			TSharedRef<FConnectionHandles> ConnectionHandles = MakeShared<FConnectionHandles>();

			// Error Handler
			{
			    const FDelegateHandle ErrorHandle = OnConnectionErrorDelegate.AddLambda(
			        [WeakThis = AsWeak(), ConnectPromise, ConnectionHandles](const FString& InError)
			    {
			        FDelegateHandle LocalErrorHandle;
			        FDelegateHandle LocalConnectedHandle;

			        {
			            FScopeLock Lock(&ConnectionHandles->Lock);
			            LocalErrorHandle = ConnectionHandles->ConnectionErrorHandle;
			            LocalConnectedHandle = ConnectionHandles->ConnectedHandle;
			        }

			        if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
			        {
			            ConnectPromise->SetValue(false);
			            StrongThis->bIsConnecting.store(false);

			            if (LocalErrorHandle.IsValid())
			            {
			                StrongThis->OnConnectionErrorDelegate.Remove(LocalErrorHandle);
			            }
			            if (LocalConnectedHandle.IsValid())
			            {
			                StrongThis->OnConnectedDelegate.Remove(LocalConnectedHandle);
			            }
			        }
			    });

			    {
			        FScopeLock Lock(&ConnectionHandles->Lock);
			        ConnectionHandles->ConnectionErrorHandle = ErrorHandle;
			    }
			}

			// Connection Handler
			{
			    const FDelegateHandle ConnectedHandle = OnConnectedDelegate.AddLambda(
			        [WeakThis = AsWeak(), ConnectPromise, ConnectionHandles]()
			    {
			        FDelegateHandle LocalErrorHandle;
			        FDelegateHandle LocalConnectedHandle;

			        {
			            FScopeLock Lock(&ConnectionHandles->Lock);
			            LocalErrorHandle = ConnectionHandles->ConnectionErrorHandle;
			            LocalConnectedHandle = ConnectionHandles->ConnectedHandle;
			        }

			        if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
			        {
			            StrongThis->WhenMessageReceived(EOperationCode::Identified)
			            .Next([WeakThis, ConnectPromise](const TValueOrError<TSharedPtr<FJsonObject>, void>& InMessage)
			            {
			                const TSharedPtr<FOBSClient> StrongerThis = WeakThis.Pin();
			                if (!StrongerThis.IsValid() || InMessage.HasError())
			                {
			                    ConnectPromise->SetValue(false);
			                    return;
			                }

			                ConnectPromise->SetValue(true);
			                StrongerThis->bIsConnecting.store(false);
			            });

			            StrongThis->WhenMessageReceived(EOperationCode::Hello)
			            .Next([WeakThis, ConnectPromise](const TValueOrError<TSharedPtr<FJsonObject>, void>& InMessage)
			            {
			                const TSharedPtr<FOBSClient> StrongerThis = WeakThis.Pin();
			                if (!StrongerThis.IsValid() || InMessage.HasError())
			                {
			                    ConnectPromise->SetValue(false);
			                    return;
			                }

			                StrongerThis->Send<FIdentifyRequest>({});
			            });

			            if (LocalConnectedHandle.IsValid())
			            {
			                StrongThis->OnConnectedDelegate.Remove(LocalConnectedHandle);
			            }

			            if (LocalErrorHandle.IsValid())
			            {
			                StrongThis->OnConnectionErrorDelegate.Remove(LocalErrorHandle);
			            }
			        }
			    });

			    {
			        FScopeLock Lock(&ConnectionHandles->Lock);
			        ConnectionHandles->ConnectedHandle = ConnectedHandle;
			    }
			}

			WebSocket->Connect();
		}
		else
		{
			ConnectPromise->SetValue(true); // Somehow, already connected
			bIsConnecting.store(false); // And therefore not in the process of connecting
		}

		return ConnectPromise->GetFuture();
	}

	bool FOBSClient::Close()
	{
		bool bClosed = false;
		if (WebSocket.IsValid())
		{
			if (WebSocket->IsConnected())
			{
				CleanupMessages(true);
				WebSocket->Close();
				bIsConnecting.store(false);
				bClosed = true;
			}

			WebSocket.Reset();
		}

		return bClosed;
	}

	TFuture<bool> FOBSClient::WhenMessageSent(const EOperationCode InOperationCode, const int32 InRequestId)
	{
		if (!WebSocket.IsValid() || !WebSocket->IsConnected())
		{
			return MakeFulfilledPromise<bool>(false).GetFuture();
		}
		
		TSharedPtr<FSentMessageHandle> MessageHandle = nullptr;
		
		{
			FScopeLock Lock(&HandleLock);
			MessageHandle = MessageSentDelegateHandles.Emplace_GetRef(MakeShared<FSentMessageHandle>(InRequestId));
		}

		*MessageHandle->DelegateHandle = OnMessageSentDelegate.AddLambda
		([WeakThis = AsWeak(), MessageHandle, InOperationCode, InRequestId](const FString& InMessage)
		{
			if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
			{
				const TSharedPtr<FJsonObject> SentMessageObject = StringToJson(InMessage);
				if (SentMessageObject.IsValid())
				{
					int32 SentOperationCode = INDEX_NONE;
					if (SentMessageObject->TryGetNumberField(TEXT("op"), SentOperationCode))
					{
						if (SentOperationCode != static_cast<uint8>(InOperationCode))
						{
							return; // Doesn't match, keep listening
						}
						// The op code can sometimes be enough to identify the message
						else if (InRequestId == INDEX_NONE)
						{
							MessageHandle->Complete(StrongThis->OnMessageSentDelegate, true);
							StrongThis->CleanupMessages();
							return;
						}
					}

					const TSharedPtr<FJsonObject>* DataJsonObject;
					if (SentMessageObject->TryGetObjectField(TEXT("d"), DataJsonObject))
					{
						int32 SentRequestId = INDEX_NONE;
						if ((*DataJsonObject)->TryGetNumberField(TEXT("requestId"), SentRequestId))
						{
							if (SentRequestId == InRequestId)
							{
								MessageHandle->Complete(StrongThis->OnMessageSentDelegate, true);
								StrongThis->CleanupMessages();
							}
						}
					}

					// Doesn't match, keep listening
				}
			}
		});

		return MessageHandle->Promise->GetFuture();
	}

	TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> FOBSClient::WhenMessageReceived(const EOperationCode InOperationCode)
	{
		return WhenMessageReceivedInternal(
			[InOperationCode](const TSharedPtr<FJsonObject>& InMessageObject) -> bool
			{
				int32 ReceivedOperationCode = INDEX_NONE;
				if (InMessageObject->TryGetNumberField(TEXT("op"), ReceivedOperationCode))
				{
					return ReceivedOperationCode == static_cast<uint8>(InOperationCode);
				}

				return false;
			});
	}

	TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> FOBSClient::WhenMessageReceived(const EOperationCode InOperationCode, const int32 InRequestId)
	{
		return WhenMessageReceivedInternal(
			[InOperationCode, InRequestId](const TSharedPtr<FJsonObject>& InMessageObject) -> bool
			{
				int32 ReceivedOperationCode = INDEX_NONE;
				if (InMessageObject->TryGetNumberField(TEXT("op"), ReceivedOperationCode))
				{
					if (ReceivedOperationCode != static_cast<uint8>(InOperationCode))
					{
						return false;
					}
				}

				const TSharedPtr<FJsonObject>* DataJsonObject;
				if (InMessageObject->TryGetObjectField(TEXT("d"), DataJsonObject))
				{
					int32 ReceivedRequestId = INDEX_NONE;
					if ((*DataJsonObject)->TryGetNumberField(TEXT("requestId"), ReceivedRequestId))
					{
						return ReceivedRequestId == InRequestId;
					}
				}

				return false;
			});
	}

	TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> FOBSClient::WhenMessageReceived(const EOperationCode InOperationCode, const FString& InOperationName, const int32 InRequestId)
	{
		return WhenMessageReceivedInternal(
			[InOperationCode, InOperationName, InRequestId](const TSharedPtr<FJsonObject>& InMessageObject) -> bool
			{
				int32 ReceivedOperationCode = INDEX_NONE;
				if (InMessageObject->TryGetNumberField(TEXT("op"), ReceivedOperationCode))
				{
					if (ReceivedOperationCode != static_cast<uint8>(InOperationCode))
					{
						return false;
					}
				}

				const TSharedPtr<FJsonObject>* DataJsonObject;
				if (InMessageObject->TryGetObjectField(TEXT("d"), DataJsonObject))
				{
					int32 RequestId = INDEX_NONE;
					if ((*DataJsonObject)->TryGetNumberField(TEXT("requestId"), RequestId))
					{
						if (RequestId != InRequestId)
						{
							return false;
						}
					}

					FString RequestType;
					if ((*DataJsonObject)->TryGetStringField(TEXT("requestType"), RequestType))
					{
						return RequestType == InOperationName;
					}
				}

				return false;
			});
	}

	TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> FOBSClient::WhenMessageReceivedInternal(TFunction<bool(const TSharedPtr<FJsonObject>)> InPredicateFunc)
	{
		if (!WebSocket.IsValid() || !WebSocket->IsConnected())
		{
			return MakeFulfilledPromise<TValueOrError<TSharedPtr<FJsonObject>, void>>(MakeError()).GetFuture();
		}

		TSharedPtr<FReceivedMessageHandle> MessageHandle = nullptr;
		{
			FScopeLock Lock(&HandleLock);
			MessageHandle = MessageReceivedDelegateHandles.Emplace_GetRef(MakeShared<FReceivedMessageHandle>());
		}

		*MessageHandle->DelegateHandle = OnMessageReceivedDelegate.AddLambda
		([WeakThis = AsWeak(), MessageHandle, PredicateFunc = MoveTemp(InPredicateFunc)](const FString& InMessage)
		{
			if (const TSharedPtr<FOBSClient> StrongThis = WeakThis.Pin())
			{
				TSharedPtr<FJsonObject> JsonObject = StringToJson(InMessage);
				if (JsonObject.IsValid())
				{
					const bool bMatchesPredicate = PredicateFunc(JsonObject);
					if (bMatchesPredicate)
					{
						MessageHandle->Complete(StrongThis->OnMessageReceivedDelegate, MakeValue(JsonObject));
						StrongThis->CleanupMessages();
					}

					// Doesn't match, keep listening
				}
			}
		});

		return MessageHandle->Promise->GetFuture();
	}
}
