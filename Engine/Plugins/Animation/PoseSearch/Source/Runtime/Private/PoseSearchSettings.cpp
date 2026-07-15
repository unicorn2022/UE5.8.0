// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchSettings.h"

UPoseSearchSettings::UPoseSearchSettings()
{
	CategoryName = TEXT("Plugins");
}

const UPoseSearchSettings& UPoseSearchSettings::Get()
{
	UPoseSearchSettings* MutableCDO = GetMutableDefault<UPoseSearchSettings>();
	check(MutableCDO != nullptr)

	return *MutableCDO;
}
