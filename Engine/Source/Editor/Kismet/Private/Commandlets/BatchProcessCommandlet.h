// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "BatchProcessCommandlet.generated.h"

UCLASS()
class UBatchProcessCommandlet : public UCommandlet
{
	GENERATED_BODY()

	UBatchProcessCommandlet(const FObjectInitializer& ObjectInitializer);
private:
	// Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	// End UCommandlet Interface
};
