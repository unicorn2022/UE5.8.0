// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "OBSMessages.h"
#include "OBSTypes.h"
#include "Templates/ValueOrError.h"
#include "TickableEditorObject.h"

class IWebSocket;

namespace OBS
{
	class FOBSClient 
		: public TSharedFromThis<FOBSClient>
		, public FTickableEditorObject
	{
	public:
		virtual ~FOBSClient() override;

		/** Gets the WebSocket URL based on the current Address and Port. */
		FString GetUrl() const;

		/** Connects to the OBS WebSocket server. */
		TFuture<bool> Connect();

		/** Closes the connection to the OBS WebSocket server. */
		bool Close();

		/** Returns true if the client is currently connected. */
		bool IsConnected() const;

		//~ Begin FTickableEditorObject
		virtual void Tick(float DeltaTime) override;
		virtual ETickableTickType GetTickableTickType() const override;
		virtual bool IsTickable() const override;
		virtual TStatId GetStatId() const override;
		//~ End FTickableEditorObject

		/** Gets the status of the record output. */
		TFuture<TValueOrError<FGetRecordStatusResponse, void>> GetRecordStatus();

		/** Starts recording output. */
		TFuture<bool> StartRecord();

		/** Stops recording output. */
		TFuture<bool> StopRecord();

		/** Pauses recording output. */
		TFuture<bool> PauseRecord();

		/** Resumes recording output. */
		TFuture<bool> ResumeRecord();

		/** Gets a profile parameter by category and name. */
		TFuture<TValueOrError<FString, void>> GetProfileParameter(const FString& InCategory, const FString& InName);

		/** Sets a profile parameter by category and name. */
		TFuture<bool> SetProfileParameter(const FString& InCategory, const FString& InName, const FString& InValue);

		/** Gets multiple profile parameters. */
		TFuture<TValueOrError<TArray<FParameter>, void>> GetProfileParameters(TConstArrayView<FParameter> InParameters);

		/** Sets multiple profile parameters. */
		TFuture<bool> SetProfileParameters(TConstArrayView<FParameter> InParameters);

		/** Gets the list of scenes. */
		TFuture<TValueOrError<FGetSceneListResponse, void>> GetSceneList();

		/** Creates a new scene with the given name. */
		TFuture<TValueOrError<FScene, void>> CreateScene(const FString& InSceneName);

		/** Gets the current program scene. */
		TFuture<TValueOrError<FScene, void>> GetCurrentProgramScene();

		/** Sets the current program scene. */
		TFuture<bool> SetCurrentProgramScene(const FScene& InScene);

		/** Gets the list of scene items in the given scene. */
		TFuture<TValueOrError<TArray<FSceneItem>, void>> GetSceneItemList(const FScene& InScene);

		/** Gets the list of inputs of the given kind. */
		TFuture<TValueOrError<TArray<TSharedPtr<FJsonObject>>, void>> GetInputList(const FString& InInputKind);

		/** Gets the settings for the given input. */
		TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> GetInputSettings(const FString& InInputName, const FString& InInputUniqueId);

		/** Sets the settings for the given input. */
		TFuture<bool> SetInputSettings(const FString& InInputName, const FString& InInputUniqueId, const TSharedPtr<FJsonObject>& InInputSettings);

		/** Gets the default settings for the given input kind. */
		TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> GetInputDefaultSettings(const FString& InInputKind);

		/** Creates a new input in the given scene. */
		TFuture<TValueOrError<FSceneItem, void>> CreateInput(const FScene& InScene, const FString& InInputName, const FString& InInputKind, const TSharedPtr<FJsonObject>& InInputSettings, const bool bIsSceneItemEnabled = true);

		/** Sets the transform for the given scene item in the given scene. */
		TFuture<bool> SetSceneItemTransform(const FScene& InScene, const FSceneItem& InSceneItem, const FSceneItem::FSceneItemTransform& InTransform);

		/** Creates a chapter with the given name at the current recording time. */
		TFuture<bool> CreateRecordChapter(const FString& InChapterName);

		/** Currently only supports the same request/response types, ie. GetProfileParameter. Returns a Map of RequestId, Data. */
		template <typename RequestType, typename ResultType>
		TFuture<TValueOrError<TMap<int32, ResultType>, void>> SendBatch(TArray<RequestType>& InRequests);

		/** Currently only supports the same request/response types, ie. GetProfileParameter. */
		template <typename RequestType>
		TFuture<bool> SendBatch(TArray<RequestType>& InRequests);

	private:
		static int32 GenerateRequestId();

		/** If "force" is false, only timed out messages are cleared. */
		void CleanupMessages(const bool bInForceAll = false);

		template <typename RequestType, typename ResultType>
		TFuture<TValueOrError<ResultType, void>> Send(RequestType&& InRequest);

		template <typename RequestType>
		TFuture<bool> Send(RequestType&& InRequest);

		/** Returns when a message with the given opcode and request id is sent. */
		TFuture<bool> WhenMessageSent(const EOperationCode InOperationCode, const int32 InRequestId);

		/** Returns when a message with the given opcode is received. */
		TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> WhenMessageReceived(const EOperationCode InOperationCode);

		/** Returns when a message with the given opcode and request id is received and matched. */
		TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> WhenMessageReceived(const EOperationCode InOperationCode, const int32 InRequestId);

		/** Returns when a message with the given opcode, op name and request id is received and matched. */
		TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> WhenMessageReceived(const EOperationCode InOperationCode, const FString& InOperationName, const int32 InRequestId);

		/** Shared between WhenMessageReceived implementations above. */
		TFuture<TValueOrError<TSharedPtr<FJsonObject>, void>> WhenMessageReceivedInternal(TFunction<bool(const TSharedPtr<FJsonObject>)> InPredicateFunc);

	private:
		static std::atomic_int RequestIdCounter;

		FIPv4Address Address = FIPv4Address(127, 0, 0, 1);
		uint32 Port = 4455;

		TSharedPtr<IWebSocket> WebSocket = nullptr;

		std::atomic_bool bIsConnecting = false;

		using FOnConnectedDelegate = TMulticastDelegate<void()>;
		FOnConnectedDelegate OnConnectedDelegate;

		using FOnConnectionErrorDelegate = TMulticastDelegate<void(const FString&)>;
		FOnConnectionErrorDelegate OnConnectionErrorDelegate;

		using FOnMessageSentDelegate = TMulticastDelegate<void(const FString&)>;
		FOnMessageSentDelegate OnMessageSentDelegate;

		using FOnMessageReceivedDelegate = TMulticastDelegate<void(const FString&)>;
		FOnMessageReceivedDelegate OnMessageReceivedDelegate;
		
		/** (Base) handle that encapsulates a request and expected response/confirmation, with timeout. */
		struct FMessageHandle
		{
			explicit FMessageHandle(const int32 InRequestId = INDEX_NONE);
			virtual ~FMessageHandle() = default;

			// Prevent Copy
			FMessageHandle(const FMessageHandle&) = delete;
			FMessageHandle& operator=(const FMessageHandle&) = delete;

			/** The RequestId is for debug purposes only (and is optional). */
			int32 RequestId = INDEX_NONE;
			uint64 Timestamp = INDEX_NONE;
			TSharedPtr<FDelegateHandle> DelegateHandle = nullptr;

			std::atomic_bool bIsCompleted = false;

			/** Removes the handle from the given delegate and fulfills the promise, indicating failure. */
			virtual void Fail(TMulticastDelegate<void(const FString&)>& InDelegate) = 0;

			bool operator==(const FMessageHandle& InOther) const
			{
				return RequestId == InOther.RequestId
					&& Timestamp == InOther.Timestamp;
			}
			
		protected:
			FCriticalSection PromiseLock;
		};

		/** Handle that encapsulates a request and expected response/confirmation, with timeout. */
		template <typename ReturnType>
		struct TMessageHandle : FMessageHandle
		{
			explicit TMessageHandle(const int32 InRequestId = INDEX_NONE);

			// Prevent Copy
			TMessageHandle(const TMessageHandle&) = delete;
			TMessageHandle& operator=(const TMessageHandle&) = delete;

			/** Completes the message with the given result, indicating success. */
			void Complete(TMulticastDelegate<void(const FString&)>& InDelegate, ReturnType&& InResult);

			/** Removes the handle from the given delegate and fulfills the promise, indicating failure. */
			virtual void Fail(TMulticastDelegate<void(const FString&)>& InDelegate) override;

			TSharedPtr<TPromise<ReturnType>> Promise = nullptr;
		};

		using FSentMessageHandle = TMessageHandle<bool>;
		using FReceivedMessageHandle = TMessageHandle<TValueOrError<TSharedPtr<FJsonObject>, void>>;

		static constexpr double MessageWaitTimeoutSeconds = 10.0;
		static const uint64 MessageWaitTimeoutCycles;

		uint64 LastTickTime = 0;

		FCriticalSection HandleLock;
		TArray<TSharedPtr<FSentMessageHandle>> MessageSentDelegateHandles;
		TArray<TSharedPtr<FReceivedMessageHandle>> MessageReceivedDelegateHandles;
	};
}
