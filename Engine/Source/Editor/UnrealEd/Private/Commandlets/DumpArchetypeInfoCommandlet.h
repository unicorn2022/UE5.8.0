// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#include "DumpArchetypeInfoCommandlet.generated.h"

UCLASS()
class UDumpArchetypeInfoCommandlet : public UCommandlet
{
	GENERATED_BODY()

private:
	virtual int32 Main(const FString& Params) override;
};
