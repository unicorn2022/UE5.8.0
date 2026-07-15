// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/PVBaseSettings.h"
#include "PVMeshBuilderBaseSettings.generated.h"

UCLASS(BlueprintType, Abstract, ClassGroup = (Procedural))
class UPVMeshBuilderBaseSettings : public UPVBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("UPVMeshBuilderBaseSettings")); }
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual TArray<EPVRenderType> GetDefaultRenderType() const override { return TArray{ EPVRenderType::None }; }
	virtual FText GetCategoryOverride() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
};
