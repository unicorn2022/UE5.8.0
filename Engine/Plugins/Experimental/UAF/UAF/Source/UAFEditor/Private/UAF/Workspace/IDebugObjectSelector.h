// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::UAF::Editor
{
	class IDebugObjectSelector
	{
	public:
		virtual ~IDebugObjectSelector() = default;
		
		using FOnDebugObjectChanged = TMulticastDelegate<void(TWeakObjectPtr<UObject>)>;

		virtual TWeakObjectPtr<UObject> GetDebugObject() const = 0;

		virtual FDelegateHandle RegisterOnDebugObjectChanged(FOnDebugObjectChanged::FDelegate InDelegate) = 0;

		virtual bool UnregisterOnDebugObjectChanged(const FDelegateHandle InHandle) = 0;
	};
}