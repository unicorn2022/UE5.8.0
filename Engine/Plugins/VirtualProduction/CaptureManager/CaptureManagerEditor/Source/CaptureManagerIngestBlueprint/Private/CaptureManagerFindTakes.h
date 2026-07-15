// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureManagerIngestBlueprintLibrary.h"

namespace UE::CaptureManager
{

TArray<FCaptureManagerTakeDirectoryInfo> FindTakesInDirectory(const FString& InSearchDirectory, bool bRecursive);

} // namespace UE::CaptureManager
