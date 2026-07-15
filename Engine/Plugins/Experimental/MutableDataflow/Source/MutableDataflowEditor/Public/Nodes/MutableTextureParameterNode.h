// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Dataflow/DataflowNode.h"
#include "Misc/Guid.h"
#include "CoreMinimal.h"
#include "MutableDataflowParameters.h"
#include "MutableParameterNode.h"

#include "MutableTextureParameterNode.generated.h"

USTRUCT(meta = (Experimental))
struct FMutableTextureParameterNode : public FMutableParameterNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMutableTextureParameterNode, "MutableTextureParameter", "Mutable|Parameters", "")

	UPROPERTY(EditAnywhere, Category = "Mutable", meta = (DataflowInput))
	TObjectPtr<UTexture2D> Texture;
	
	UPROPERTY(meta = (DataflowOutput))
	FMutableTextureParameter TextureParameter;
	
public:
	
	FMutableTextureParameterNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};