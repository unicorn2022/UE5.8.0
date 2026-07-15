// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChooserInitializer.h"
#include "ChooserPlayerTraitData.h"

#include "UAFAnimationChooserInitializer.generated.h"

USTRUCT(DisplayName="UAF Animation Chooser", meta = (ToolTip="A ChooserTable for use with the ChooserPlayer UAF Trait.\nReturns any type of Asset which has a registered UAF Graph Factory."))
struct FUAFAnimationChooserInitializer : public FChooserInitializer
{
	GENERATED_BODY()
	virtual void InitializeSignature(UChooserSignature* Chooser) const override;
	virtual UClass* OverrideClass(UClass* Class) const override;

	UPROPERTY(EditAnywhere, Category = "Shared Variables")
	TArray<TObjectPtr<UUAFSharedVariables>> SharedVariablesAssets;
};
