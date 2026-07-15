// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeEditorSchema.h"

#include "UAFStateTreeEditorSchema.generated.h"

UCLASS(MinimalAPI)
class UUAFStateTreeEditorSchema : public UStateTreeEditorSchema
{
	GENERATED_BODY()

	virtual EStateTreeTransitionEditingRules GetTransitionEditingRules(TNotNull<UStateTreeEditorData*> StateTree) const override
	{
		return EStateTreeTransitionEditingRules::AllowReactivation;
	}
};
