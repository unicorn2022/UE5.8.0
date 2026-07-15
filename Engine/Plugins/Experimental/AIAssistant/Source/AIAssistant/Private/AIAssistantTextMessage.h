// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

#include "AIAssistantWebApi.h"

namespace UE::AIAssistant
{
	// Create a user message from visible and hidden prompt.
	FAddMessageToConversationOptions CreateUserMessage(
		const FString& VisiblePrompt, const FString& HiddenContext);
	
}