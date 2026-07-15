// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureManagerTakeMetadata.h"

#define UE_API CAPTUREMETADATAEXTRACTION_API

namespace UE::CaptureManager::StereoCameraMetadata
{

UE_API TOptional<FTakeMetadata> ParseOldStereoCameraMetadata(const FString& InTakeFolder, TArray<FText>& OutValidationError);

}

#undef UE_API
