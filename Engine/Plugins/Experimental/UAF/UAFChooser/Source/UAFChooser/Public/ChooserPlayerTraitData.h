// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChooserPropertyAccess.h"
#include "TraitCore/TraitBinding.h"
#include "TraitCore/TraitSharedData.h"
#include "Variables/AnimNextSharedVariables.h"
#include "UAFAnimChooser.h"
#include "ChooserTypes.h"
#include "UAF/UAFAssetData.h"
#include "ChooserPlayerTraitData.generated.h"

/** A trait that can evaluate a chooser and play a subggraph based on whatever entry from the chooser was selected . */
USTRUCT(meta = (DisplayName = "Chooser Player", ShowTooltip=true))
struct FUAFChooserPlayerTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

private:

	/** The chooser to play. */
	UPROPERTY(EditAnywhere, Category = "Chooser", meta=(ExportAsReference="true", FactorySource, OnBecomeRelevant))
	TObjectPtr<const UUAFAnimChooserTable> Chooser;
	
	// How often the chooser should be evaluated
	UPROPERTY(EditAnywhere, Category = "Chooser")
	EChooserEvaluationFrequency EvaluationFrequency = EChooserEvaluationFrequency::OnBecomeRelevant;

public:
	void SetChooser(const UUAFAnimChooserTable* InChooser)
	{
		Chooser = InChooser;
	}
	
	void SetEvaluationFrequency(EChooserEvaluationFrequency InEvaluationFrequency)
	{
		EvaluationFrequency = InEvaluationFrequency;
	}

	// Latent pin support boilerplate
	#define TRAIT_LATENT_PROPERTIES_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(Chooser) \
		GeneratorMacro(EvaluationFrequency) \

	GENERATE_TRAIT_LATENT_PROPERTIES(FUAFChooserPlayerTraitSharedData, TRAIT_LATENT_PROPERTIES_ENUMERATOR)
	#undef TRAIT_LATENT_PROPERTIES_ENUMERATOR
};

USTRUCT(DisplayName = "UAF Variables Parameter")
struct FUAFSharedVariablesContext : public FContextObjectTypeBase
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category="Type")
	TArray<TObjectPtr<UUAFSharedVariables>> SharedVariablesAssets; 
};

USTRUCT(BlueprintType)
struct FUAFChooserPlayerSettings : public FChooserPlayerSettings
{
	GENERATED_BODY()

	UE::UAF::FGraphAssetHandleConstView AssetData;
};


namespace UE::UAF
{
	// Namespaced alias
	using FChooserPlayerData = FUAFChooserPlayerTraitSharedData;
}