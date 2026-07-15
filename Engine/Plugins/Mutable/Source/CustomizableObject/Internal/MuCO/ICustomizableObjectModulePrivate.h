// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/ICustomizableObjectModule.h"

#include "Modules/ModuleManager.h"

namespace UE::Mutable
{
	struct FExternalOperation;
}


class ICustomizableObjectModulePrivate : public ICustomizableObjectModule
{
public:
	static ICustomizableObjectModulePrivate& Get()
	{
		return FModuleManager::LoadModuleChecked<ICustomizableObjectModulePrivate>("CustomizableObject");
	}
	
	virtual TSet<TStrongObjectPtr<const UScriptStruct>> GetRegisteredExternalOperations() const = 0;
};

