// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Templates/ValueOrError.h"

#include "AIAssistantWebApi.h"

namespace UE::AIAssistant
{
	// Create a tool response message content.
	FAddMessageToConversationOptions CreateToolResponseMessage(
		const FString& ToolName, const FString& ToolCallId,
		const TValueOrError<FString, FString>& ToolResult);
	
}