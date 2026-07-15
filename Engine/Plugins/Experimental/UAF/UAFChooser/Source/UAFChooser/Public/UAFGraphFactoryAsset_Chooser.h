// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChooserTypes.h"
#include "CoreTypes.h"
#include "UAF/UAFAssetData.h"

#include "UAFGraphFactoryAsset_Chooser.generated.h"

class UUAFAnimChooserTable;

USTRUCT(DisplayName="Chooser Asset")
struct FUAFGraphFactoryAsset_Chooser : public FUAFGraphFactoryAsset
{
	GENERATED_BODY()

	FUAFGraphFactoryAsset_Chooser() = default;
	FUAFGraphFactoryAsset_Chooser(const UUAFAnimChooserTable* InChooserTable) : ChooserTable(InChooserTable) {}

	virtual bool Validate() const override { return ChooserTable != nullptr; }

	virtual void GetObjectReferences(TArray<const UObject*>& OutReferencedObjects) const override;

	UPROPERTY(EditAnywhere, Category = Chooser)
	TObjectPtr<const UUAFAnimChooserTable> ChooserTable;

	// How often the chooser should be evaluated
	UPROPERTY(EditAnywhere, Category = Settings)
    EChooserEvaluationFrequency EvaluationFrequency = EChooserEvaluationFrequency::OnBecomeRelevant;
};
