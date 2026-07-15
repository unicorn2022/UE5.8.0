// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Bindings/ImplicitConverters/MVVMImplicitConverter.h"

/**
 * For converting between numeric types
 */
class FMVVMNumericImplicitConverter : public FMVVMImplicitConverter
{
public:
	virtual bool CanConvertImplicitly(const FProperty* Source, const FProperty* Destination) override;
	virtual void ConvertImplicitly(const FProperty* Source, const FProperty* Destination, void* Data) override;
};
