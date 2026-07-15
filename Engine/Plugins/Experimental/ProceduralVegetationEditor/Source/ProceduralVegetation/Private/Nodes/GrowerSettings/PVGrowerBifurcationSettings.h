// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PVGrowerBaseSettings.h"
#include "DataTypes/PVGrowerParams.h"
#include "PVGrowerBifurcationSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVGrowerBifurcationSettings : public UPVGrowerBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GrowerBifurcationSettings")); }
	virtual FText GetDefaultNodeTitle() const override;
#endif

	virtual FString GetAdditionalTitleInformation() const override;
 
protected:
	virtual FPCGDataTypeIdentifier GetOutputPinTypeIdentifier() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	UPROPERTY(EditAnywhere, Category="BifurcationSettings", meta=(ShowOnlyInnerProperties, PCG_Overridable, InnerCategoryToggle))
	FPVGrowerBifurcationWithTargets ParamsWithTargets;
};

class FPVGrowerBifurcationElement : public FPVBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
