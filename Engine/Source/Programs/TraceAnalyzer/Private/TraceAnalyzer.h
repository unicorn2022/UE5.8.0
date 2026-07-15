// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Logging/LogCategory.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTraceAnalyzer, Log, All);

#define LogTraceAnalyzerError(Format, ...) \
	{ \
		wprintf(L"Error: " Format "\n", ##__VA_ARGS__); \
		UE_LOG(LogTraceAnalyzer, Error, TEXT(Format), ##__VA_ARGS__); \
	}
#define LogTraceAnalyzerWarning(Format, ...) \
	{ \
		wprintf(L"Warning: " Format "\n", ##__VA_ARGS__); \
		UE_LOG(LogTraceAnalyzer, Warning, TEXT(Format), ##__VA_ARGS__); \
	}
#define LogTraceAnalyzerMessage(Format, ...) \
	{ \
		wprintf(L"" Format "\n", ##__VA_ARGS__); \
		UE_LOG(LogTraceAnalyzer, Log, TEXT(Format), ##__VA_ARGS__); \
	}
