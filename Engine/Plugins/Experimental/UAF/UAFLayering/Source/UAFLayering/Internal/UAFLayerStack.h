// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/AnimNextAnimationGraph.h"
#include "UAFLayerStack.generated.h"

UCLASS(BlueprintType, MinimalAPI)
class UUAFLayerStack : public UUAFAnimGraph
{
	GENERATED_BODY()

public:
	friend class UUAFLayerStack_EditorData;
	friend class UUAFLayerStackFactory;
};
