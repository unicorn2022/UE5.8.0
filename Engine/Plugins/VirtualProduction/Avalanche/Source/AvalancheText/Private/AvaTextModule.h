// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"

class FAvaTextModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	void RegisterTextRendererDelegates();
	void UnregisterTextRendererDelegates();

	FDelegateHandle TextPreRendererUpdateHandle;
	FDelegateHandle TextPostRendererUpdateHandle;
};
