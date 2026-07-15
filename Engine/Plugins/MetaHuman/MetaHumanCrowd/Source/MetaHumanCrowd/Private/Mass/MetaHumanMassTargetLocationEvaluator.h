// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "MassNavigationTypes.h"
#include "MetaHumanMassTargetLocationEvaluator.generated.h"

USTRUCT()
struct FMetaHumanMassTargetReaderInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Output)
	FMassTargetLocation TargetLocation;
};

USTRUCT(meta = (DisplayName = "MetaHuman Mass Target Location Evaluator"))
struct FMetaHumanMassTargetLocationEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FMetaHumanMassTargetReaderInstanceData::StaticStruct();
	}
};