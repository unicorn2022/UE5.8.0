// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class FMVVMImplicitConverter;

class IModelViewViewModelModule : public IModuleInterface
{
public:
	virtual void RegisterImplicitConverter(const TSharedRef<FMVVMImplicitConverter>& Converter) = 0;
	virtual void UnregisterImplicitConverter(const TSharedRef<FMVVMImplicitConverter>& Converter) = 0;
};
