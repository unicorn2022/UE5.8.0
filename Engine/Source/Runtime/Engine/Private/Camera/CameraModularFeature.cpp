// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraModularFeature.h"
#include "UObject/NameTypes.h"

FName ICameraModularFeature::GetModularFeatureName()
{
	static FName FeatureName = FName("Camera");
	return FeatureName;
}
