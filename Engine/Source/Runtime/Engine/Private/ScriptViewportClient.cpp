// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/ScriptViewportClient.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScriptViewportClient)

UScriptViewportClient::UScriptViewportClient(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UWorld* UScriptViewportClient::GetWorld() const
{
	return Super::GetWorld();
}
