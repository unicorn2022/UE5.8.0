// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"

namespace UE::SandboxedEditing
{
/** Gives an indication of the workflow's result. */
enum class EImportWorkflowResult : uint8
{
	Success,
	SomeErrors,
	Cancelled,
	
	Count
};

/** Describes the result of the entire import operation (multiple sandboxes can be imported at the same time). */
struct FImportWorkflowResult
{
	/** Gives an indication of the workflow's result. */
	EImportWorkflowResult ResultCode;
	
	/** The errors that occured. If empty, the operation was successful. */
	TArray<FString> Errors;
};
}