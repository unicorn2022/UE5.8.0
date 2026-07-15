// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureManagerIngestPreparation.h"

struct FIngestProcessResult
{
	FString TakeIngestPackagePath;
	UE::CaptureManager::FCaptureDataTakeInfo CaptureDataTakeInfo;
	TArray<UE::CaptureManager::FCreateAssetsData> AssetsData;
};