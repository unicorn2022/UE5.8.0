// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargetProfile.h"

class UIKRetargeter;
class USkeletalMesh;

class FMetaHumanBodyAnimRetargeter
{
public:
	bool Initialize(const USkeletalMesh* InSourceMesh, const USkeletalMesh* InTargetMesh, const UIKRetargeter* InRetargeter);

	bool IsInitialized() const { return Processor.IsInitialized(); }

	void RetargetFrame(const TMap<FString, FTransform>& InBodyAnimationData, TMap<FString, FTransform>& OutBodyAnimationData, float InDeltaTime);

private:

	FIKRetargetProcessor Processor;

	FRetargetProfile Profile;

	TStrongObjectPtr<const USkeletalMesh> SourceMesh = nullptr;
	TStrongObjectPtr<const USkeletalMesh> TargetMesh = nullptr;
};
