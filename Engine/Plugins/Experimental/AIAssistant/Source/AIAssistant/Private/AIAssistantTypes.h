// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Misc/DateTime.h"
#include "Misc/Optional.h"
#include "Misc/Variant.h"
#include "Serialization/JsonSerializable.h"
#include "Serialization/JsonSerializerMacros.h"

#include "AIAssistantConversationId.h"
#include "AIAssistantEnum.h"
#include "AIAssistantJsonVariantSerializer.h"

namespace UE::AIAssistant
{
	// Role of the message author / source.
	enum class EMessageRole
	{
		Unknown, // Default; used for roles not recognized by this build.
		Agent,
		User,
	};

	#define UE_AI_ASSISTANT_MESSAGE_ROLE_ENUM(X) \
		X(EMessageRole::Unknown, "unknown"), \
		X(EMessageRole::Agent, "agent"), \
		X(EMessageRole::User, "user")

	UE_ENUM_METADATA_DECLARE(EMessageRole, UE_AI_ASSISTANT_MESSAGE_ROLE_ENUM);

	// Type of content added to a message (text and tool calls now, images etc. in future)
	enum class EMessageContentType
	{
		Unknown,      // Default; used for content types not recognized by this build.
		Text,
		ToolCall,
		ToolResponse,
	};

	#define UE_AI_ASSISTANT_MESSAGE_CONTENT_TYPE_ENUM(X) \
		X(EMessageContentType::Unknown, "unknown"), \
		X(EMessageContentType::Text, "text"), \
		X(EMessageContentType::ToolCall, "tool_call"), \
		X(EMessageContentType::ToolResponse, "tool_response")

	UE_ENUM_METADATA_DECLARE(EMessageContentType, UE_AI_ASSISTANT_MESSAGE_CONTENT_TYPE_ENUM);

	// Type of update delivered by OnConversationUpdate callbacks.
	enum class EConversationUpdateType
	{
		Unknown,         // Default; used for update types not recognized by this build.
		MessagesUpdated, // New messages are available for the conversation.
		Stopped,         // The user requested that generation be stopped.
		Complete,        // Generation has finished on the frontend.
	};

	#define UE_AI_ASSISTANT_CONVERSATION_UPDATE_TYPE_ENUM(X) \
		X(EConversationUpdateType::Unknown, "unknown"), \
		X(EConversationUpdateType::MessagesUpdated, "messagesUpdated"), \
		X(EConversationUpdateType::Stopped, "stopped"), \
		X(EConversationUpdateType::Complete, "complete")

	UE_ENUM_METADATA_DECLARE(EConversationUpdateType, UE_AI_ASSISTANT_CONVERSATION_UPDATE_TYPE_ENUM);

	// Text message content.
	struct FTextMessageContent : public FJsonSerializable
	{
		// Text message in a conversation.
		FString Text;
		// (Optional) Text format, default is set as markdown.
		TOptional<FString> Format;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("text", Text);
			JSON_SERIALIZE_OPTIONAL("format", Format);
		END_JSON_SERIALIZER
	};

	// Tool call message content.
	struct FToolCallContent : public FJsonSerializable
	{
		// Tool name (e.g., 'agent_status', 'reference').
		FString Name;
		// Tool-specific arguments.
		FString ArgumentsRawJson;
		// UUID for tool calls that expect response.
		TOptional<FString> ToolCallId;
		// Whether agent expects response, default is set to false.
		TOptional<bool> ResponseRequired;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("name", Name);
			JSON_SERIALIZE_RAW_JSON_STRING("arguments", ArgumentsRawJson);
			JSON_SERIALIZE_OPTIONAL("toolCallId", ToolCallId);
			JSON_SERIALIZE_OPTIONAL("responseRequired", ResponseRequired);
		END_JSON_SERIALIZER
	};

	// Tool response content.
	struct FToolResponseContent : public FJsonSerializable
	{
		// ID of the tool call this responds to.
		FString ToolCallId;
		// Name of the tool that was called.
		FString Name;
		// Response data from client execution.
		FString ResponseRawJson;
		// Whether execution succeeded. An empty value represents true.
		TOptional<bool> Success;
		// Error details if success=false.
		TOptional<FString> ErrorMessage;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("toolCallId", ToolCallId);
			JSON_SERIALIZE("name", Name);
			JSON_SERIALIZE_RAW_JSON_STRING("response", ResponseRawJson);
			JSON_SERIALIZE_OPTIONAL("success", Success);
			JSON_SERIALIZE_OPTIONAL("errorMessage", ErrorMessage);
		END_JSON_SERIALIZER
	};

	// Placeholder for message content whose type is not recognized by this build.
	struct FUnknownMessageContent : public FJsonSerializable
	{
		BEGIN_JSON_SERIALIZER
		END_JSON_SERIALIZER
	};

	using FMessageContentVariant = TVariant<
		FUnknownMessageContent,
		FTextMessageContent,
		FToolCallContent,
		FToolResponseContent
	>;

	// Content of a message.
	struct FMessageContent : public FJsonSerializable
	{
		// Type of the messageContent field.
		EMessageContentType ContentType = EMessageContentType::Unknown;
		// Content of the message, this can be extended in future to support additional types of content.
		FMessageContentVariant Content;
		// Whether the message is visible to the user.
		bool bVisibleToUser = true;

		BEGIN_JSON_SERIALIZER
			UE_JSON_SERIALIZE_ENUM_VARIANT_BEGIN("contentType", ContentType, "content", Content);
				UE_JSON_SERIALIZE_ENUM_VARIANT(EMessageContentType::Unknown, FUnknownMessageContent);
				UE_JSON_SERIALIZE_ENUM_VARIANT(EMessageContentType::Text, FTextMessageContent);
				UE_JSON_SERIALIZE_ENUM_VARIANT(EMessageContentType::ToolCall, FToolCallContent);
				UE_JSON_SERIALIZE_ENUM_VARIANT(EMessageContentType::ToolResponse, FToolResponseContent);
			UE_JSON_SERIALIZE_ENUM_VARIANT_END();
			JSON_SERIALIZE_WITHDEFAULT("visibleToUser", bVisibleToUser, true);
		END_JSON_SERIALIZER

	};

	// ID of a message, generated by the assistant backend.
	struct FMessageId : public FJsonSerializable
	{
		// Unique ID of an object.
		FString Id;
		// Discriminator of the type.
		FString Type;

		bool operator==(const FMessageId& Other) const
		{
			return Id == Other.Id && Type == Other.Type;
		}

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("id", Id);
			JSON_SERIALIZE("type", Type);
		END_JSON_SERIALIZER
	};

	// Message within a conversation.
	struct FMessage : public FJsonSerializable
	{
		// Populated on the server side and ignore when specified on the client.
		TOptional<FMessageId> MessageId;
		// When the message was created (UTC). This is populated on the server side and ignored when
		// specified on the client side.
		TOptional<FDateTime> Date;
		// Role or source of the message.
		EMessageRole MessageRole = EMessageRole::Unknown;
		// Content of the message.
		TArray<FMessageContent> MessageContent;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OPTIONAL_OBJECT_SERIALIZABLE("id", MessageId);
			JSON_SERIALIZE_OPTIONAL("date", Date);
			JSON_SERIALIZE_ENUM("messageRole", MessageRole);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("messageContent", MessageContent, FMessageContent);
		END_JSON_SERIALIZER
	};

	// Metadata used to index and display a conversation.
	struct FConversationDescriptor : public FJsonSerializable
	{
		// ID of the conversation, this is generated on the server side
		// and ignored if set by the client.
		TOptional<FConversationId> ConversationId;
		// Display name / one-line description of the conversation that
		// is simply used to help a user find it within a flat list of
		// all conversations.
		FString DisplayName;
		// Labels applied to the conversation to make it possible for a user
		// to organize and search for conversations. For example:
		// "epic.favorite" could be reserved for favorites (starred)
		// conversations.
		TArray<FString> Labels;
		// When the conversation was created (UTC milliseconds since
		// the epoch). This is derived from the first message in the
		// conversation.
		FDateTime CreatedAt;
		// When the conversation was last updated (UTC milliseconds since
		// the epoch). This is derived  from the most recent message in the
		// conversation.
		FDateTime UpdatedAt;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OPTIONAL_OBJECT_SERIALIZABLE("conversationId", ConversationId);
			JSON_SERIALIZE("displayName", DisplayName);
			JSON_SERIALIZE_ARRAY("labels", Labels);
			JSON_SERIALIZE("createdAt", CreatedAt);
			JSON_SERIALIZE("updatedAt", UpdatedAt);
		END_JSON_SERIALIZER
	};

	// Conversation.
	struct FConversation : public FJsonSerializable
	{
		// Metadata used to index and display a conversation.
		FConversationDescriptor Descriptor;
		// Messages in the conversation, ordered chronologically.
		TArray<FMessage> Messages;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("descriptor", Descriptor);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("messages", Messages, FMessage);
		END_JSON_SERIALIZER
	};

	// Argument for AddMessageToConversation.
	struct FAddMessageToConversationOptions : public FJsonSerializable
	{
		// ID of the conversation to add the message to.
		// If this is not specified, the message is added to the current conversation.
		TOptional<FConversationId> ConversationId;
		// Message to add to the conversation.
		FMessage Message;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OPTIONAL_OBJECT_SERIALIZABLE("conversationId", ConversationId);
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("message", Message);
		END_JSON_SERIALIZER
	};

	// Status indicating how a file was changed.
	enum class EPendingFileStatus
	{
		Added,
		Removed,
		Modified,
		Moved,
	};

	#define UE_AI_ASSISTANT_PENDING_FILE_STATUS_ENUM(X) \
		X(EPendingFileStatus::Added, "added"), \
		X(EPendingFileStatus::Removed, "removed"), \
		X(EPendingFileStatus::Modified, "modified"), \
		X(EPendingFileStatus::Moved, "moved")

	UE_ENUM_METADATA_DECLARE(EPendingFileStatus, UE_AI_ASSISTANT_PENDING_FILE_STATUS_ENUM);

	// Metadata describing a file that has been modified by tool calls.
	struct FPendingFileMetadata : public FJsonSerializable
	{
		// Display name shown to the user (e.g., filename without path).
		FString DisplayName;
		// Full path to the source file.
		FString FullPath;
		// Status indicating how the file was changed.
		EPendingFileStatus Status = EPendingFileStatus::Modified;
		// For moved files, the original path before the move.
		TOptional<FString> OriginalPath;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("displayName", DisplayName);
			JSON_SERIALIZE("fullPath", FullPath);
			JSON_SERIALIZE_ENUM("status", Status);
			JSON_SERIALIZE_OPTIONAL("originalPath", OriginalPath);
		END_JSON_SERIALIZER
	};

	// Argument for UpdatePendingFileList.
	struct FUpdatePendingFileListOptions : public FJsonSerializable
	{
		// ID of the conversation this file list belongs to.
		// If unspecified, the currently active conversation is used.
		TOptional<FConversationId> ConversationId;
		// List of file metadata describing the pending file changes.
		TArray<FPendingFileMetadata> Files;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OPTIONAL_OBJECT_SERIALIZABLE("conversationId", ConversationId);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("files", Files, FPendingFileMetadata);
		END_JSON_SERIALIZER
	};

	// Argument for OnPendingFileDecision callback.
	struct FOnPendingFileDecisionOptions : public FJsonSerializable
	{
		// Unique ID of the conversation.
		FConversationId ConversationId;
		bool bAccepted = false;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("conversationId", ConversationId);
			JSON_SERIALIZE("accepted", bAccepted);
		END_JSON_SERIALIZER
	};

	// Event delivered to OnConversationUpdate callbacks describing what changed.
	struct FConversationUpdateEvent : public FJsonSerializable
	{
		// Unique ID of the conversation that was updated.
		FConversationId ConversationId;
		// Type of update. Unknown is used for types not recognized by this build.
		EConversationUpdateType UpdateType = EConversationUpdateType::Unknown;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("conversationId", ConversationId);
			JSON_SERIALIZE_ENUM("updateType", UpdateType);
		END_JSON_SERIALIZER
	};

	// High level descriptor of the environment is interacting with.
	struct FAgentEnvironmentDescriptor : public FJsonSerializable {
		// Name of the current environment.
		// NOTE: This is a string so that this can be extended without
		// modifying anything in the data path from the assistant client to
		// the backend.
		// Valid values are initially "Web", "UE", "UEFN".
		FString EnvironmentName;
		// If applicable, the version of the environment. For example, given
		// a particular release version of the Unreal Engine this may allow
		// the assistant backend to reference a particular set of
		// documentation.
		FString EnvironmentVersion;
		// JSON dict of arbitrary key-value pairs for development options.
		FString DevOptionsRawJson;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("environmentName", EnvironmentName);
			JSON_SERIALIZE("environmentVersion", EnvironmentVersion);
			JSON_SERIALIZE_RAW_JSON_STRING("devOptions", DevOptionsRawJson);
		END_JSON_SERIALIZER
	};

	struct FAgentEnvironmentSchema : public FJsonSerializable
	{
		// List of JSON schema objects that describe the interfaces
		// available to the web frontend in the current environment.
		// These are interactive modal tools we would expect the agent to
		// not make use of when responding. For example,
		// create a Verse file from a prior Verse code block response,
		// add to the current UEFN project and open it in VS code.
		//
		// Note: In JSON this is represented as an array of JSON schema objects,
		// but here we just store the raw JSON string.
		FString FrontendJsonSchemaRawJson;

		// List of JSON schema objects that describe the JSON schema
		// of tools available to the assistant in the current environment.
		// This is described by EDA Tool Calling Architecture
		//
		// Note: In JSON this is represented as an array of JSON schema objects,
		// but here we just store the raw JSON string.
		FString ToolsetsRawJson;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_RAW_JSON_STRING("frontendJsonSchema", FrontendJsonSchemaRawJson);
			JSON_SERIALIZE_RAW_JSON_STRING("toolsets", ToolsetsRawJson);
		END_JSON_SERIALIZER
	};

	// Description of the agent's environment.
	struct FAgentEnvironment : public FJsonSerializable
	{
		// Very high level description of the environment.
		FAgentEnvironmentDescriptor Descriptor;
		// Schema of the environment's API.
		TOptional<FAgentEnvironmentSchema> Schema;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("descriptor", Descriptor);
			JSON_SERIALIZE_OPTIONAL_OBJECT_SERIALIZABLE("schema", Schema);
		END_JSON_SERIALIZER
	};

	// Permanent storage ID of an agent environment
	// (e.g database generated ID)
	struct FAgentEnvironmentId : public FJsonSerializable
	{
		FString Id;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("id", Id);
		END_JSON_SERIALIZER
	};

	// Hash of an agent environment.
	struct FAgentEnvironmentHash : public FJsonSerializable
	{
		// Name of the hash algorithm. If this is not specified, use SHA256.
		FString Algorithm;
		// Hash of the AgentEnvironment.
		FString Hash;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("algorithm", Algorithm);
			JSON_SERIALIZE("hash", Hash);
		END_JSON_SERIALIZER
	};

	// Handle to an agent environment.
	struct FAgentEnvironmentHandle : public FJsonSerializable
	{
		// ID of the agent environment.
		FAgentEnvironmentId Id;
		// Hash of the environment.
		FAgentEnvironmentHash Hash;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("id", Id);
			JSON_SERIALIZE_OBJECT_SERIALIZABLE("hash", Hash);
		END_JSON_SERIALIZER
	};

	// Boolean result.
	struct FWebApiBoolResult : public FJsonSerializable
	{
		bool bValue = false;

		operator bool() const { return bValue; }

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("value", bValue);
		END_JSON_SERIALIZER
	};
}

FORCEINLINE uint32 GetTypeHash(const UE::AIAssistant::FMessageId& Id)
{
	return HashCombine(GetTypeHash(Id.Id), GetTypeHash(Id.Type));
}
