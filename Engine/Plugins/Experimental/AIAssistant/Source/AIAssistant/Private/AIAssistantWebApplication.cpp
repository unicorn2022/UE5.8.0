// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantWebApplication.h"

#include "Async/UniqueLock.h"
#include "Internationalization/Culture.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "AIAssistantConfig.h"
#include "AIAssistantWebJavaScriptResultDelegate.h"
#include "AIAssistantFileLockManager.h"
#include "AIAssistantRunSequential.h"
#include "AIAssistantSubsystem.h"
#include "AIAssistantTextMessage.h"
#include "AIAssistantToolResponse.h"
#include "AIAssistantTransactionBufferManager.h"

#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"

namespace UE::AIAssistant
{
	static const TCHAR* WebApplicationShutDownMessage = TEXT("WebApplication Shut Down");

	FWebApplication::FWebApplication(
		TFunction<TSharedPtr<FWebApi>()>&& WebApiFactory,
		const FString& InDevOptionsRawJson) :
		ConversationReadyExecutor(
			[this]() -> FExecuteWhenReady::EExecuteWhenReadyState
			{
				// Extend the conversation ready executor to query the current URL and block
				// execution if an error has occurred.
				return LoadState == ELoadState::Error
					? FExecuteWhenReady::EExecuteWhenReadyState::Reject
					: IsLoaded()
						? FExecuteWhenReady::EExecuteWhenReadyState::Execute
						: FExecuteWhenReady::EExecuteWhenReadyState::Wait;
			}),
		CreateWebApi(MoveTemp(WebApiFactory)),
		DevOptionsRawJson(InDevOptionsRawJson)
	{
		TSharedPtr<FJsonObject> DevOptions;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DevOptionsRawJson);
		if (FJsonSerializer::Deserialize(Reader, DevOptions) && DevOptions.IsValid())
		{
			DevOptions->TryGetBoolField(TEXT("EnableUndoBuffer"), bEnableUndoBuffer);
		}
	}

	void FWebApplication::CreateConversation()
	{
		(void)WithWebApi(
			[this](FWebApi& WebApi) -> void
			{
				UE_LOGF(LogAIAssistant, Display, "Create conversation...");
				if (!ConversationReadyExecutor.SetCreatingConversation(true))
				{
					WebApi.CreateConversation().Next(
						[WeakThis = SharedThis(this).ToWeakPtr()](
							const TValueOrError<void, FString>& Result) -> void
						{
							auto This = WeakThis.Pin();
							if (This)
							{
								UE_LOGF(
									LogAIAssistant, Display, "Create conversation %s",
									Result.HasError() ? "failed" : "succeeded");
								This->ConversationReadyExecutor.SetCreatingConversation(false);
								(void)This->HandleErrorResult(TEXT("CreateConversation"), Result);
							}
						});
				}
			});
	}

	void FWebApplication::AddUserMessageToConversation(
		FAddMessageToConversationOptions&& Options)
	{
		UE_LOGF(LogAIAssistant, Verbose, "Add message to conversation, waiting...");
		WithWebApiWhenConversationReady(
			[WeakThis = SharedThis(this).ToWeakPtr(),
			 Options = MoveTemp(Options)](FWebApi& WebApi) -> void
			{
				auto This = WeakThis.Pin();
				UE_LOGF(
					LogAIAssistant, Verbose, "Add message to conversation %s",
					This ? "ready" : "aborted");
				if (This) WebApi.AddMessageToConversation(Options);
			});
	}

	void FWebApplication::UpdatePendingFileList(
		FUpdatePendingFileListOptions&& Options)
	{
		WithWebApiWhenConversationReady(
			[WeakThis = SharedThis(this).ToWeakPtr(),
			 Options = MoveTemp(Options)](FWebApi& WebApi) -> void
			{
				auto This = WeakThis.Pin();
				if (This) WebApi.UpdatePendingFileList(Options);
			});
	}

	void FWebApplication::OnBeforeNavigation(
		const FString& Url, const FWebNavigationRequest& Request)
	{
		(void)Url;
		(void)Request;
		UE::TUniqueLock StateLock(StateMutex);
		ResetPageState();
		LoadState = ELoadState::NotLoaded;
	}

	void FWebApplication::OnPageLoadComplete()
	{
		EnsureInitialized();
		bPageLoaded = true;

		UE_LOGF(LogAIAssistant, Log, "Page load complete, waiting...");
		EnsureWebApi()->IsAvailable().Next(
			[WeakThis = SharedThis(this).ToWeakPtr()](const auto& IsAvailable) mutable
			{
				auto This = WeakThis.Pin();
				UE_LOGF(
					LogAIAssistant, Log, "Page load complete %s", This ? "ready" : "aborted");
				if (!This) return;

				// If the assistant isn't loaded, reset the application's state.
				if (This->HandleErrorResult(TEXT("IsAvailable"), IsAvailable))
				{
					return;
				}
				if (!(IsAvailable.HasValue() && IsAvailable.GetValue()))
				{
					UE_LOGF(LogAIAssistant, Display, "Assistant not available, resetting...");
					This->ResetState();
					return;
				}

				UE::TUniqueLock StateLock(This->StateMutex);
				This->bApplicationAvailable = true;

				// Configure the assistant and execute any pending operations.
				This->OnCultureChanged();
				This->TryUpdateAgentEnvironment(This->bRequestedAgentEnvironmentIsUefn);
				This->ConversationReadyExecutor.UpdateExecuteWhenReady();

				This->RegisterForConversationUpdates();
			});
	}

	void FWebApplication::OnPageLoadError()
	{
		ResetState(ELoadState::Error);
	}

	FWebApplication::ELoadState FWebApplication::GetLoadState() const
	{
		UE::TUniqueLock StateLock(StateMutex);
		return LoadState;
	}

	void FWebApplication::OnCultureChanged()
	{
		(void)WithWebApi(
			[](FWebApi& WebApi) -> void
			{
				const FString& Language =
					FInternationalization::Get().GetCurrentLanguage()->GetName();
				UE_LOGF(LogAIAssistant, Verbose, "Update locale to %ls", *Language);
				WebApi.UpdateGlobalLocale(Language);
			});
	}

	void FWebApplication::TryUpdateAgentEnvironment(bool bUseUefnMode, bool bForce)
	{
		bRequestedAgentEnvironmentIsUefn = bUseUefnMode;
		// If the mode hasn't changed and the agent environment has been configured, do nothing.
		bool bUefnModeChanged =
			!bAgentEnvironmentIsUefn.IsSet() ||
			bAgentEnvironmentIsUefn.GetValue() != bRequestedAgentEnvironmentIsUefn;
		if (!bForce && !bUefnModeChanged && ConversationReadyExecutor.IsAgentEnvironmentConfigured()) return;

		// Block responses until the backend has the updated agent environment
		ConversationReadyExecutor.NotifyAgentEnvironmentReconfiguring();

		const uint32 AgentUpdateGeneration = ++AgentEnvironmentUpdateGeneration;

		if (!WithWebApi(
			[this, bUseUefnMode, AgentUpdateGeneration](FWebApi& WebApi) -> void
			{
				UE_LOGF(LogAIAssistant, Log, "Update agent environment...");
				// Configure the agent's environment.
				WebApi.AddAgentEnvironment(*GetAgentEnvironment(bUseUefnMode)).Next(
					[WeakThis = SharedThis(this).ToWeakPtr(), bUseUefnMode, AgentUpdateGeneration](
						const auto& Result) mutable -> void
					{
						auto This = WeakThis.Pin();
						UE_LOGF(
							LogAIAssistant, Log, "Update agent environment %s",
							This ? "complete" : "aborted");
						if (!This) return;

						if (This->HandleErrorResult(TEXT("UpdateAgentEnvironment"), Result))
						{
							return;
						}
						const auto& EnvironmentId = Result.GetValue().Id;
						if (!This->WithWebApi(
							[&This, AgentUpdateGeneration, bUseUefnMode, &EnvironmentId](FWebApi& WebApi)
							{
								UE_LOGF(
									LogAIAssistant, Log,
									"Set agent environment to %ls", *EnvironmentId.Id);
								WebApi.SetAgentEnvironment(EnvironmentId);
								This->bAgentEnvironmentIsUefn.Emplace(bUseUefnMode);

								// notify agent configured when all in-flight updates are completed
								if (AgentUpdateGeneration == This->AgentEnvironmentUpdateGeneration)
								{
									This->ConversationReadyExecutor.NotifyAgentEnvironmentConfigured();
									This->LoadState = ELoadState::Complete;
								}
							}))
						{
							This->ResetState(ELoadState::Error);
						}
					});
			}))
		{
			// revert the executor block if the web api is not available
			ConversationReadyExecutor.NotifyAgentEnvironmentConfigured();
		}
	}

	UE::ToolsetRegistry::FToolsetRegistry& FWebApplication::GetToolsetRegistry()
	{
		check(GEditor);
		UToolsetRegistrySubsystem* ToolsetRegistrySubsystem =
			GEditor->GetEditorSubsystem<UToolsetRegistrySubsystem>();
		check(ToolsetRegistrySubsystem);
		return ToolsetRegistrySubsystem->ToolsetRegistry;
	}

	TFuture<TValueOrError<void, FString>> FWebApplication::OnConversationUpdate(
		const FConversationId& ConversationId)
	{
		auto Promise = MakeShared<TPromise<TValueOrError<void, FString>>>();
		if (!WithWebApi(
			[this, ConversationId, Promise](FWebApi& WebApi) -> void
			{
				WebApi.GetConversation(ConversationId).Next(
					[WeakThis = SharedThis(this).ToWeakPtr(), ConversationId, Promise](
						const auto& Result) -> void
					{
						auto This = WeakThis.Pin();
						if (!This)
						{
							Promise->SetValue(MakeError(WebApplicationShutDownMessage));
							return;
						}
						if (Result.HasError())
						{
							Promise->SetValue(MakeError(Result.GetError()));
							return;
						}
						const auto& Conversation = Result.GetValue();
						// Track tool calls that have already been handled.
						This->TrackToolResponses(Conversation.Messages);
						// A new conversation update means a new generation round;
						// clear any previous cancellation for this conversation.
						{
							UE::TUniqueLock StateLock(This->StateMutex);
							This->CancelledConversationIds.Remove(ConversationId.Id);
						}
						// Process each message in the conversation.
						for (const FMessage& Message : Conversation.Messages)
						{
							This->ProcessMessage(Message, ConversationId.Id);
						}
						Promise->SetValue(MakeValue());
					});
			}))
		{
			// If the web API is not available, return a failed future.
			Promise->SetValue(MakeError(WebApplicationShutDownMessage));
		}
		return Promise->GetFuture();
	}

	// Processes tool calls sequentially, aborting remaining calls if one fails
	// or if the user stops generation. Allocated via MakeShared and shared across
	// RunSequential iterations so that abort state is visible to all iterations
	// without boxing.
	class FSequentialToolCallProcessor
	{
	public:
		FSequentialToolCallProcessor(
			TWeakPtr<FWebApplication> InWebApplication,
			FString InConversationId,
			FMessage InMessage) :
			WeakWebApplication(MoveTemp(InWebApplication)),
			ConversationId(MoveTemp(InConversationId)),
			Message(MoveTemp(InMessage))
		{}

		TFuture<void> operator()(const FToolCallContent& ToolCall)
		{
			TSharedPtr<FWebApplication> WebApplication = WeakWebApplication.Pin();
			if (!WebApplication)
			{
				return MakeFulfilledPromise<void>().GetFuture();
			}

			// Check if the user has stopped generation for this conversation.
			if (CancellationReason == ECancellationReason::None &&
				WebApplication->IsConversationCancelled(ConversationId))
			{
				CancellationReason = ECancellationReason::User;
			}

			if (CancellationReason != ECancellationReason::None)
			{
				// Abort: send an error response without executing.
				FString AbortReason = CancellationReason == ECancellationReason::User
					? TEXT("Tool call aborted: user stopped generation")
					: TEXT("Tool call aborted due to previous failure");
				TValueOrError<FString, FString> AbortResult = MakeError(AbortReason);
				if (ToolCall.ToolCallId.IsSet())
				{
					WebApplication->AddUserMessageToConversation(CreateToolResponseMessage(
						ToolCall.Name, *ToolCall.ToolCallId, AbortResult));
				}
				return MakeFulfilledPromise<void>().GetFuture();
			}
			return WebApplication->ProcessToolCallContent(ToolCall, ConversationId).Next(
				[this](TValueOrError<void, FString> Result)
				{
					if (Result.HasError())
					{
						CancellationReason = ECancellationReason::ToolCallFailure;
						UE_LOGF(
							LogAIAssistant, Verbose,
							"Error processing tool call for message %ls: %ls",
							*Message.MessageId.Get(FMessageId()).Id,
							*Result.GetError());
					}

					// Check for user cancellation after tool execution so that
					// the next tool call in the sequence will be aborted.
					TSharedPtr<FWebApplication> WebApp = WeakWebApplication.Pin();
					if (CancellationReason == ECancellationReason::None &&
						WebApp && WebApp->IsConversationCancelled(ConversationId))
					{
						CancellationReason = ECancellationReason::User;
					}
				});
		}

	private:
		// Reason tool call execution was aborted.
		// Written in a .Next() continuation and read in the subsequent operator() call.
		// No synchronization is needed because RunSequential guarantees strict sequential
		// execution operator() is never called concurrently with the previous continuation.
		enum class ECancellationReason
		{
			None,            // No cancellation; continue executing tool calls.
			ToolCallFailure, // A previous tool call failed.
			User,            // The user requested stop generation.
		};

		TWeakPtr<FWebApplication> WeakWebApplication;
		FString ConversationId;
		FMessage Message;
		ECancellationReason CancellationReason = ECancellationReason::None;
	};

	void FWebApplication::TrackToolResponses(const TArray<FMessage>& Messages)
	{
		for (const FMessage& Message : Messages)
		{
			for (const FMessageContent& Content : Message.MessageContent)
			{
				const FToolResponseContent* ToolResponse = Content.Content.TryGet<FToolResponseContent>();
				if (ToolResponse) ProcessedToolCallIds.Add(ToolResponse->ToolCallId);
			}
		}
	}

	bool FWebApplication::ProcessMessage(const FMessage& Message, const FString& ConversationId)
	{
		// In order to process a message, it must have an ID and not have been processed before.
		if (!Message.MessageId.IsSet() || ProcessedMessageIds.Contains(*Message.MessageId))
		{
			return false;
		}
		ProcessedMessageIds.Add(*Message.MessageId);

		// Extract tool calls from the message.
		TArray<FToolCallContent> ToolCalls;
		Algo::TransformIf(
			Message.MessageContent,
			ToolCalls,
			[](const auto& Item) {
				const FToolCallContent* ToolCall = Item.Content.template TryGet<FToolCallContent>();
				return ToolCall && ToolCall->ResponseRequired.Get(false);
			},
			[](const auto& Item) { return Item.Content.template Get<FToolCallContent>(); });

		// Process each tool call sequentially. If a tool call fails, subsequent tool calls
		// respond with an abort message rather than executing.
		auto Processor = MakeShared<FSequentialToolCallProcessor>(
			SharedThis(this).ToWeakPtr(), ConversationId, Message);
		RunSequential(
			MoveTemp(ToolCalls),
			[Processor](const FToolCallContent& ToolCall) -> TFuture<void>
			{
				return (*Processor)(ToolCall);
			});
		return true;
	}

	TFuture<TValueOrError<void, FString>> FWebApplication::ProcessToolCallContent(
		const FToolCallContent& ToolCall, const FString& ConversationId)
	{
		UE::ToolsetRegistry::FToolsetRegistry& ToolsetRegistry = GetToolsetRegistry();
		auto ToolCallId = ToolCall.ToolCallId;
		auto Name = ToolCall.Name;
		// Tool call must have an ID if a response is required.
		if (!ToolCallId.IsSet())
		{
			FString ErrorMessage = FString::Printf(
				TEXT("Tool call '%s' with arguments '%s' requires a response but has no ID"),
				*Name, *ToolCall.ArgumentsRawJson);
			return MakeFulfilledPromise<TValueOrError<void, FString>>(
				MakeError(ErrorMessage)).GetFuture();
		}
		// A response has already been provided.
		if (ProcessedToolCallIds.Contains(ToolCallId.GetValue()))
		{
			return MakeFulfilledPromise<TValueOrError<void, FString>>(MakeValue()).GetFuture();
		}

		// Only use the undo buffer if explicitly enabled via dev_options.
		TObjectPtr<UTransBuffer> TransactionBuffer;
		if (bEnableUndoBuffer)
		{
			TransactionBuffer =
				FTransactionBufferManager::GetOrCreateTransactionBuffer(ConversationId);
			FTransactionBufferManager::SetOverrideBuffer(TransactionBuffer.Get());
			GEditor->BeginTransaction(FText::FromString(TEXT("Tool Transaction")));
		}

		// Execute the tool and add the response to the conversation when the tool call completes.
		return ToolsetRegistry.ExecuteTool(Name, ToolCall.ArgumentsRawJson).Next(
			[WeakThis = SharedThis(this).ToWeakPtr(), ConversationId, Name, ToolCallId, 
			 TransactionBuffer](
				const TValueOrError<FString, FString>& ToolResult) -> TValueOrError<void, FString>
			{
				if (TransactionBuffer)
				{
					// Always restore the global transaction buffer, even if the application is
					// shutting down.
					GEditor->EndTransaction();
					FTransactionBufferManager::RestoreGlobalBuffer();
				}

				auto This = WeakThis.Pin();
				if (!This) return MakeError(WebApplicationShutDownMessage);

				if (TransactionBuffer)
				{
					This->UpdatePendingFileList(
						This->BuildPendingFileListOptions(ConversationId, TransactionBuffer));
				}

				This->ProcessedToolCallIds.Add(*ToolCallId);

				This->AddUserMessageToConversation(
					CreateToolResponseMessage(Name, *ToolCallId, ToolResult));

				if (ToolResult.HasError()) return MakeError(ToolResult.GetError());
				return MakeValue();
			});
	}

	void FWebApplication::OnPendingFileDecision(const FOnPendingFileDecisionOptions& Options)
	{
		const FString& ConversationId = Options.ConversationId.Id;

		// Clear the pending file list by sending an empty list.
		FUpdatePendingFileListOptions ClearOptions;
		ClearOptions.ConversationId.Emplace();
		ClearOptions.ConversationId->Id = ConversationId;
		ClearOptions.ConversationId->Type = TEXT("ConversationId");
		// Files array is intentionally empty to clear the pending file list.
		UpdatePendingFileList(MoveTemp(ClearOptions));

		// Get the transaction buffer for this conversation.
		TObjectPtr<UTransBuffer> TransactionBuffer =
			FTransactionBufferManager::GetTransactionBuffer(ConversationId);

		if (TransactionBuffer)
		{
			// If the change was not accepted, undo all changes in the transaction buffer.
			if (!Options.bAccepted)
			{
				// Undo all active transactions.
				while (TransactionBuffer->CanUndo())
				{
					TransactionBuffer->Undo();
				}
			}

			// Clear/delete the transaction buffer.
			FTransactionBufferManager::DestroyTransactionBuffer(ConversationId);
		}
	}

	void FWebApplication::OnStopGenerating(const FConversationId& ConversationId)
	{
		UE::TUniqueLock StateLock(StateMutex);
		CancelledConversationIds.Add(ConversationId.Id);
		UE_LOGF(
			LogAIAssistant, Display,
			"Stop generating requested for conversation: %ls",
			*ConversationId.Id);
	}

	bool FWebApplication::IsConversationCancelled(const FString& ConversationId) const
	{
		UE::TUniqueLock StateLock(StateMutex);
		return CancelledConversationIds.Contains(ConversationId);
	}

	void FWebApplication::EnsureInitialized()
	{
		UE::TUniqueLock Lock(InitializationMutex);
		// It's not possible to use SharedThis() on construction as the shared pointer hasn't been
		// allocated at that point so we lazy initialize listeners here.

		// Subscribe to culture / language modifications.
		if (!CultureChangeDelegateHandle.IsValid())
		{
			auto& OnCultureChanged = FInternationalization::Get().OnCultureChanged();
			CultureChangeDelegateHandle = UE::ToolsetRegistry::FDelegateHandleRaii::Create(
				OnCultureChanged,
				OnCultureChanged.AddSP(SharedThis(this), &FWebApplication::OnCultureChanged));
		}

		// Subscribe to editor mode updates.
		if (!UefnModeSubscription.IsSet())
		{
			UefnModeSubscription.Emplace(
				[WeakThis = SharedThis(this).ToWeakPtr()](bool bIsUefnMode) -> void
				{
					auto This = WeakThis.Pin();
					if (This) This->TryUpdateAgentEnvironment(bIsUefnMode);
				});
		}

		// Register for toolset changes.
		auto& Delegate = GetToolsetRegistry().OnToolsetRegistered();
		OnToolsetRegisteredCallbackHandle = UE::ToolsetRegistry::FDelegateHandleRaii::Create(
				Delegate,
				Delegate.AddLambda([WeakThis = SharedThis(this).ToWeakPtr()]()
				{
					auto This = WeakThis.Pin();
					if (!This) return;

					UE_LOGF(
						LogAIAssistant, Display,
						"New toolset detected, reloading environment");
					This->TryUpdateAgentEnvironment(
						This->bRequestedAgentEnvironmentIsUefn, true);
				}));
	}

	void FWebApplication::RegisterForConversationUpdates()
	{
		if (OnConversationUpdateHandle.IsSet() && OnPendingFileDecisionHandle.IsSet())
		{
			return;
		}

		(void)WithWebApi(
			[WeakThis = SharedThis(this).ToWeakPtr()](FWebApi& WebApi)
			{
				auto This = WeakThis.Pin();
				if (!This) return;

				if (!This->OnConversationUpdateHandle.IsSet())
				{
					WebApi.RegisterOnConversationUpdate(
						[WeakThis](FConversationUpdateEvent Event) ->
							UAIAssistantWebJavaScriptResultDelegate::EHandlerStatus
						{
							auto This = WeakThis.Pin();
							if (!This)
							{
								return
									UAIAssistantWebJavaScriptResultDelegate::EHandlerStatus::Unregister;
							}

							switch (Event.UpdateType)
							{
							case EConversationUpdateType::Stopped:
							{
								This->OnStopGenerating(Event.ConversationId);
								break;
							}
							case EConversationUpdateType::MessagesUpdated:
							{
								This->OnConversationUpdate(Event.ConversationId).Next(
									[WeakThis](const auto& Result) {
										auto This = WeakThis.Pin();
										if (!This) return;
										// A "canceled" result means the frontend aborted the
										// request (e.g., AbortController to discard a stale
										// fetch). This is benign -- a subsequent
										// "messagesUpdated" event will trigger a fresh fetch.
										if (Result.HasError() && Result.GetError() ==
											UAIAssistantWebJavaScriptResultDelegate::CanceledError)
										{
											return;
										}
										(void)This->HandleErrorResult(
											TEXT("OnConversationUpdate"), Result);
									});
								break;
							}
							case EConversationUpdateType::Complete:
							{
								// "complete" signals end-of-generation on the frontend.
								// No C++ action is required.
								break;
							}
							case EConversationUpdateType::Unknown:
							{
								// Unknown types are expected when a newer frontend sends event
								// types this build doesn't know about. This is not an error.
								UE_LOGF(
									LogAIAssistant, Verbose,
									"Unknown conversation update type received");
								break;
							}
							}
							return UAIAssistantWebJavaScriptResultDelegate::EHandlerStatus::Continue;
						}).Next([WeakThis](auto HandleOrError) {
							auto This = WeakThis.Pin();
							if (!This) return;

							if (This->HandleErrorResult(
								TEXT("Error registering OnConversationUpdate"), HandleOrError))
							{
								return;
							}
							This->OnConversationUpdateHandle.Emplace(HandleOrError.GetValue());
						});
				}

				if (!This->OnPendingFileDecisionHandle.IsSet())
				{
					WebApi.RegisterOnPendingFileDecision(
						[WeakThis](FOnPendingFileDecisionOptions Options)
						{
							auto This = WeakThis.Pin();
							if (!This) return;

							This->OnPendingFileDecision(Options);
						}).Next([WeakThis](auto HandleOrError) {
							auto This = WeakThis.Pin();
							if (!This) return;

							if (This->HandleErrorResult(
								TEXT("Error registering OnPendingFileDecision"), HandleOrError))
							{
								return;
							}
							This->OnPendingFileDecisionHandle.Emplace(HandleOrError.GetValue());
						});
				}
			});
	}

	TSharedPtr<FWebApi> FWebApplication::EnsureWebApi()
	{
		UE::TUniqueLock StateLock(StateMutex);
		if (!MaybeWebApi) MaybeWebApi = CreateWebApi();
		return MaybeWebApi;
	}

	bool FWebApplication::WithWebApi(TFunction<void(FWebApi&)>&& UsingWebApi)
	{
		// Ensure the assistant application is loaded.
		if (!IsLoaded()) return false;

		UsingWebApi(*EnsureWebApi());
		return true;
	}

	void FWebApplication::WithWebApiWhenConversationReady(
		TFunction<void(FWebApi&)>&& UsingWebApi)
	{
		ConversationReadyExecutor.ExecuteWhenReady(
			[WeakThis = SharedThis(this).ToWeakPtr(),
			UsingWebApi = MoveTemp(UsingWebApi)]() mutable -> void
			{
				auto This = WeakThis.Pin();
				if (This) (void)This->WithWebApi(MoveTemp(UsingWebApi));
			});
	}

	void FWebApplication::ResetConversationState()
	{
		if (MaybeWebApi.IsValid())
		{
			if (OnConversationUpdateHandle.IsSet())
			{
				MaybeWebApi->UnregisterOnConversationUpdate(*OnConversationUpdateHandle);
			}
			if (OnPendingFileDecisionHandle.IsSet())
			{
				MaybeWebApi->UnregisterOnPendingFileDecision(*OnPendingFileDecisionHandle);
			}
		}
		OnConversationUpdateHandle.Reset();
		OnPendingFileDecisionHandle.Reset();
	}

	void FWebApplication::ResetPageState()
	{
		UE::TUniqueLock StateLock(StateMutex);
		ResetConversationState();
		AgentEnvironmentUpdateGeneration = 0;
		ProcessedMessageIds.Empty();
		ProcessedToolCallIds.Empty();
		CancelledConversationIds.Empty();
		MaybeWebApi.Reset();
		bAgentEnvironmentIsUefn.Reset();
		ConversationReadyExecutor.Reset();
		bPageLoaded = false;
		bApplicationAvailable = false;
	}

	void FWebApplication::ResetState(const TOptional<ELoadState>&& NewLoadState)
	{
		UE::TUniqueLock StateLock(StateMutex);
		ResetPageState();
		OnToolsetRegisteredCallbackHandle.Reset();
		if (NewLoadState.IsSet()) LoadState = NewLoadState.GetValue();
	}

	bool FWebApplication::IsLoaded() const
	{
		// Ensure the assistant application is loaded.
		return bApplicationAvailable && bPageLoaded &&
			GetLoadState() != ELoadState::Error;
	}

	TFunction<TSharedPtr<FWebApi>()> FWebApplication::CreateWebApiFactory(
		IWebJavaScriptExecutor& JavaScriptExecutor,
		IWebJavaScriptDelegateBinder& JavaScriptDelegateBinder,
		FSimpleMulticastDelegate& UnbindDelegate)
	{
		return [&JavaScriptExecutor, &JavaScriptDelegateBinder, 
				&UnbindDelegate]() -> TSharedPtr<FWebApi>
			{
				return MakeShared<FWebApi>(
					JavaScriptExecutor, JavaScriptDelegateBinder, UnbindDelegate);
			};
	}

	TUniquePtr<FAgentEnvironment> FWebApplication::GetAgentEnvironment(bool bUseUefnMode)
	{
		auto AgentEnvironment = MakeUnique<FAgentEnvironment>();
		auto& Descriptor = AgentEnvironment->Descriptor;
		Descriptor.EnvironmentName = bUseUefnMode ? TEXT("UEFN") : TEXT("UE");
		Descriptor.EnvironmentVersion = FEngineVersion::Current().ToString();
		Descriptor.DevOptionsRawJson = DevOptionsRawJson.IsEmpty() ? TEXT("{}") : DevOptionsRawJson;
		auto& Schema = AgentEnvironment->Schema.Emplace();
		Schema.ToolsetsRawJson = GetToolsetRegistry().GetToolsetJsonSchemas();
		return AgentEnvironment;
	}

	FUpdatePendingFileListOptions FWebApplication::BuildPendingFileListOptions(
		const FString& ConversationId, const UTransBuffer* TransactionBuffer)
	{
		FUpdatePendingFileListOptions Options;
		Options.ConversationId.Emplace();
		Options.ConversationId->Id = ConversationId;
		Options.ConversationId->Type = TEXT("ConversationId");

		TSet<FString> ModifiedFiles =
			FTransactionBufferManager::GetFilenamesFromUndoStack(TransactionBuffer);
		for (const FString& Filename : ModifiedFiles)
		{
			FPendingFileMetadata& FileMetadata = Options.Files.AddDefaulted_GetRef();
			FileMetadata.FullPath = Filename;
			// Extract just the filename portion for display.
			FileMetadata.DisplayName = FPaths::GetCleanFilename(Filename);
			// Files in the transaction buffer were modified by tool calls.
			// TODO: Differentiate between added, modified, and deleted files.
			FileMetadata.Status = EPendingFileStatus::Modified;
		}

		return Options;
	}
}
