// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "FrameRange.h"

class UMetaHumanPerformance;

class FMetaHumanPerformanceNotifier : public TSharedFromThis<FMetaHumanPerformanceNotifier>
{
private:
	
	struct FPrivateToken { explicit FPrivateToken() = default; };

public:

	static TSharedRef<FMetaHumanPerformanceNotifier> Attach(UMetaHumanPerformance* InPerformance);

	explicit FMetaHumanPerformanceNotifier(UMetaHumanPerformance* InPerformance, FPrivateToken);
	~FMetaHumanPerformanceNotifier();

	FMetaHumanPerformanceNotifier(const FMetaHumanPerformanceNotifier&) = delete;
	FMetaHumanPerformanceNotifier& operator=(const FMetaHumanPerformanceNotifier&) = delete;

	void StartShowing();

private:

	class FImpl;
	TPimplPtr<FImpl> Impl;
};