// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "COMultiPluginBulkValidationCommandlet.generated.h"


UCLASS()
class UCOMultiPluginBulkValidationCommandlet : public UCommandlet
{
	GENERATED_BODY()
	
public:
	virtual int32 Main(const FString& Params) override;
};
