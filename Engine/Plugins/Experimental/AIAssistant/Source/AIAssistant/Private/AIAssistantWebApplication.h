// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>

#include "Async/Mutex.h"
#include "Async/RecursiveMutex.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "IWebBrowserWindow.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/ValueOrError.h"

#include "AIAssistantConsole.h"
#include "AIAssistantConversationReadyExecutor.h"
#include "AIAssistantLog.h"
#include "AIAssistantWebApi.h"
#include "AIAssistantWebJavaScriptDelegateBinder.h"
#include "AIAssistantWebJavaScriptExecutor.h"
#include "ToolsetRegistry/ToolsetRegistry.h"

class UTransBuffer;

namespace UE::AIAssistant
{
	// Assistant web application.
	class FWebApplication : public TSharedFromThis<FWebApplication, ESPMode::ThreadSafe>
	{
	public:
		// Load state of the AI Assistant URL.
		enum class ELoadState
		{
			NotLoaded,
			Error,
			Complete,
		};

	public:
		// This *must* be constructed using MakeShared.
		explicit FWebApplication(
			TFunction<TSharedPtr<FWebApi>()>&& WebApiFactory,
			const FString& DevOptionsRawJson = FString());

		virtual ~FWebApplication() = default;

		// Disable move and delete.
		FWebApplication(const FWebApplication&) = delete;
		FWebApplication& operator=(const FWebApplication&) = delete;
		FWebApplication(FWebApplication&&) = delete;
		FWebApplication& operator=(FWebApplication&&) = delete;

		// Create a new conversation.
		void CreateConversation();

		// Add a user message to the existing conversation.
		virtual void AddUserMessageToConversation(
			FAddMessageToConversationOptions&& Options);

		// Add pending files modified by tool calls to the conversation.
		void UpdatePendingFileList(FUpdatePendingFileListOptions&& Options);

		// Should be called before navigation to a new webpage occurs.
		void OnBeforeNavigation(const FString& Url, const FWebNavigationRequest& Request);

		// Should be called to notify that a page load is complete.
		void OnPageLoadComplete();

		// Notify the application that a page load error occurred.
		void OnPageLoadError();

		// Get the load state of the application.
		// If the application enters ELoadState::Error a new instance of this object must be
		// created to use the application.
		ELoadState GetLoadState() const;

		// Get the current agent environment.
		TUniquePtr<FAgentEnvironment> GetAgentEnvironment(bool bUseUefnMode);

		friend class FSequentialToolCallProcessor;

	private:
		// Handle language / culture changed notification.
		void OnCultureChanged();

		// Update the agent environment exposed to the assistant.
		void TryUpdateAgentEnvironment(bool bUseUefnMode, bool bForce = false);

		// Check a result for an error, logging the error if one is present, setting the load state
		// to error and returning true. Returns false if the result does not have an error.
		template<typename ValueType>
		bool HandleErrorResult(
			const FString& HandlerContext,
			const TValueOrError<ValueType, FString>& Result)
		{
			if (Result.HasError())
			{
				UE_LOGF(
					LogAIAssistant, Error,
					"JavaScript Execution Failed %ls: '%ls'",
					*HandlerContext, *Result.GetError());
				ResetState(ELoadState::Error);
				return true;
			}
			return false;
		}

		// Ensure state that needs to lazily initialized is initialized.
		void EnsureInitialized();

		// Register for conversation updates. Called when IsLoaded() becomes true.
		void RegisterForConversationUpdates();

		// Get / create the web API.
		TSharedPtr<FWebApi> EnsureWebApi();

		// Ensure the web API is initialized and get a reference to it if possible.
		// Returns true if the function was executed, false if the web API was not available.
		// 
		// NOTE: The provided reference to the web API is only valid within the current stack
		// frame. If the web API needs to be accessed later (e.g in a future continuation) it
		// should be requested again using this method.
		bool WithWebApi(TFunction<void(FWebApi&)>&& UsingWebApi);

		// Use the web API when the conversation is ready.
		// IMPORTANT: See caveats in WithWebApi() documentation.
		// NOTE: It's possible that UsingWebApi is never called if ConversationReadyExecutor's
		// queue is flushed.
		void WithWebApiWhenConversationReady(
			TFunction<void(FWebApi&)>&& UsingWebApi);

		// Reset the conversation state.
		void ResetConversationState();

		// Resets load/conversation state without tearing down the WebApi.
		void ResetPageState();

		// Whether the assistant application has been loaded.
		bool IsLoaded() const;

	protected:
		// Reset the application state.
		void ResetState(const TOptional<ELoadState>&& NewLoadState = TOptional<ELoadState>());

		// Get the toolset registry. This is virtual to allow overriding in tests.
		virtual UE::ToolsetRegistry::FToolsetRegistry& GetToolsetRegistry();

		// Handle updates to the conversation.
		TFuture<TValueOrError<void, FString>> OnConversationUpdate(const FConversationId& ConversationId);

		// Handle pending file decision (accept/reject) from the user.
		void OnPendingFileDecision(const FOnPendingFileDecisionOptions& Options);

		// Handle stop generating notification from the frontend.
		void OnStopGenerating(const FConversationId& ConversationId);

		// Check whether a conversation has been cancelled by the user.
		bool IsConversationCancelled(const FString& ConversationId) const;

		// Process a message from the conversation.
		virtual bool ProcessMessage(const FMessage& Message, const FString& ConversationId);

		// Extract all tool call IDs from their responses in a set of messages and store
		// them in ProcessedToolCallIds.
		void TrackToolResponses(const TArray<FMessage>& Messages);

		// Process a tool call message.
		virtual TFuture<TValueOrError<void, FString>> ProcessToolCallContent(
			const FToolCallContent& ToolCall, const FString& ConversationId);

		// Build pending file list options from a set of filenames.
		FUpdatePendingFileListOptions BuildPendingFileListOptions(
			const FString& ConversationId, const UTransBuffer* TransactionBuffer);

	private:
		// Handles deferring adding messages until a conversation is ready.
		FConversationReadyExecutor ConversationReadyExecutor;
		// Used to construct the web API.
		const TFunction<TSharedPtr<FWebApi>()> CreateWebApi;
		// Raw JSON string for dev options passed to the agent environment.
		FString DevOptionsRawJson;
		// Whether tool calls should be wrapped in undo transactions (from dev_options).
		bool bEnableUndoBuffer = false;
		UE::FMutex InitializationMutex;
		// Subscription to a cvar that controls the mode of the assistant.
		TOptional<FUefnModeSubscription> UefnModeSubscription;
		// Reference to a culture change notification delegate.
		UE::ToolsetRegistry::FDelegateHandleRaii CultureChangeDelegateHandle;

		// Handle for conversation update registration.
		TOptional<FWebApi::FEventCallbackHandle> OnConversationUpdateHandle;
		// Handle for pending file decision registration.
		TOptional<FWebApi::FEventCallbackHandle> OnPendingFileDecisionHandle;
		// The set of message IDs that have been processed previously.
		TSet<FMessageId> ProcessedMessageIds;
		// Set of tool call IDs that have been processed.
		TSet<FString> ProcessedToolCallIds;
		// Set of conversation IDs that the user has cancelled via "Stop Generating".
		// Guarded by StateMutex.
		TSet<FString> CancelledConversationIds;

		// Guards:
		// * MaybeWebApi
		// * bAgentEnvironmentIsUefn
		// * bRequestedAgentEnvironmentIsUefn
		// * bPageLoaded
		// * bApplicationAvailable
		// * LoadState
		// * CancelledConversationIds
		mutable UE::FRecursiveMutex StateMutex;

		// Interface for the assistant web application.
		TSharedPtr<FWebApi> MaybeWebApi;

		// Current agent environment mode if set.
		TOptional<bool> bAgentEnvironmentIsUefn;
		// Requested agent environment mode.
		bool bRequestedAgentEnvironmentIsUefn = false;

		// Whether a page load is complete.
		bool bPageLoaded = false;
		// Whether the application is available on the loaded page.
		bool bApplicationAvailable = false;
		// State of the application.
		ELoadState LoadState = ELoadState::NotLoaded;

		UE::ToolsetRegistry::FDelegateHandleRaii OnToolsetRegisteredCallbackHandle;

		// Generation counter for environment updates. Incremented each time an update starts;
		// only the latest generation's completion unblocks EDA responses.
		std::atomic<uint32> AgentEnvironmentUpdateGeneration = 0;

	public:
		// Create a factory for web API instances.
		static TFunction<TSharedPtr<FWebApi>()> CreateWebApiFactory(
			IWebJavaScriptExecutor& JavaScriptExecutor,
			IWebJavaScriptDelegateBinder& JavaScriptDelegateBinder,
			FSimpleMulticastDelegate& UnbindDelegate);
	};
}