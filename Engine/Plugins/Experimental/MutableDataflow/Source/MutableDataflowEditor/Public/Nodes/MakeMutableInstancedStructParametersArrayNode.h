// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/Guid.h"
#include "CoreMinimal.h"
#include "MakeMutableParametersArrayBaseNode.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowInputOutput.h"
#include "MutableDataflowParameters.h"

#include "MakeMutableInstancedStructParametersArrayNode.generated.h"


USTRUCT(meta = (Experimental))
struct FMakeMutableInstancedStructParametersArrayNode : public FMakeMutableParametersArrayBaseNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMakeMutableInstancedStructParametersArrayNode, "MakeMutableInstancedStructParametersArray", "Mutable|Parameters", "")

private:
	UPROPERTY()
	TArray<FMutableInstancedStructParameter> InputStructParameters;
	
	UPROPERTY(meta = (DataflowOutput, DisplayName = "Instanced Struct Parameters"))
	TArray<FMutableInstancedStructParameter> GroupedParameters;

public:
	
	FMakeMutableInstancedStructParametersArrayNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());
	
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

	// Overrides to allow the addition and removal of optional inputs
	virtual TArray<UE::Dataflow::FPin> AddPins() override;
	virtual bool CanRemovePin() const override;
	virtual TArray<UE::Dataflow::FPin> GetPinsToRemove() const override;
	virtual void OnPinRemoved(const UE::Dataflow::FPin& Pin) override;
	virtual void PostSerialize(const FArchive& Ar) override; 
};