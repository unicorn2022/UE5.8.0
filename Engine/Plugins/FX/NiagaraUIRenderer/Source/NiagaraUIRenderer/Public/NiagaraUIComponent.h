// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraComponent.h"
#include "NiagaraUITypes.h"
#include "NiagaraUIComponent.generated.h"

UCLASS(ClassGroup=FX, meta=(BlueprintSpawnableComponent, DisplayName="Niagara UI Component"))
class UNiagaraUIComponent : public UNiagaraComponent
{
	GENERATED_BODY()

public:
	UNiagaraUIComponent();

	// BEGIN: UNiagaraComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// END: UNiagaraComponent interface

	// Get the last generated UI render data
	const FNiagaraUIRenderData* GetRenderData() const;

private:
	void CreateRenderData();

private:
	TUniquePtr<FNiagaraUIRenderData> RenderData;
};
