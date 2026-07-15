// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PVDistributionBaseSettings.h"
#include "DataTypes/PVDistributionParams.h"
#include "PVDistributionConditionSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVDistributionConditionSettings : public UPVDistributionBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DistributionConditionSettings")); }
	virtual FText GetDefaultNodeTitle() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return TArray<FPCGPinProperties>(); }
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(EditAnywhere, Category = "Condition Settings", meta = (ShowOnlyInnerProperties, PCG_Overridable))
	FPVDistributionConditionParams Params;
};

class FPVDistributionConditionElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
