// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::UAF::Editor
{
	class IUAFWorkspaceRewindDebugger
	{
	public:
		virtual ~IUAFWorkspaceRewindDebugger() = default;
		
		using FOnRewindDebuggerUpdate = TMulticastDelegate<void(float, class IRewindDebugger*)>;

		virtual FDelegateHandle RegisterOnRewindDebuggerUpdate(FOnRewindDebuggerUpdate::FDelegate InDelegate) = 0;
		virtual bool UnregisterOnRewindDebuggerUpdate(const FDelegateHandle InHandle) = 0;
	};
}