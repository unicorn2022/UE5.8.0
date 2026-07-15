// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class MSGAMINGRUNTIME_API IMSGamingRuntimeModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to the MS Gaming Runtime module instance
	 * @return Returns IMSGamingRuntimeModule singleton instance, loading the module on demand if needed
	 */
	static inline IMSGamingRuntimeModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IMSGamingRuntimeModule>("MSGamingRuntime");
	}

	/**
	 * Returns whether the GRDK is available
	 * It will be safe to access IGDKRuntimeModule if this returns true
	 */
	virtual bool IsAvailable() const = 0;
};

