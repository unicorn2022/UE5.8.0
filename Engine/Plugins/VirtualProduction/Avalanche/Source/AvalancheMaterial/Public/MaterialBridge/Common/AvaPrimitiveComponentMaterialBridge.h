// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MaterialBridge/AvaMaterialBridge.h"
#include "MaterialBridge/AvaMaterialContainerState.h"
#include "AvaPrimitiveComponentMaterialBridge.generated.h"

#define UE_API AVALANCHEMATERIAL_API

class UMaterialInterface;

/** Material container state for primitive components */
USTRUCT()
struct FAvaPrimitiveComponentMaterialContainerState : public FAvaMaterialContainerState
{
	GENERATED_BODY()

	/** Materials found via the UPrimitiveComponent material api */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
};

namespace UE::Ava
{
/** Material Bridge for Primitive Components */
class FPrimitiveComponentMaterialBridge : public FMaterialBridge
{
public:
	using FContainerState = FAvaPrimitiveComponentMaterialContainerState;

protected:
	//~ Begin FMaterialBridge
	UE_API virtual const UStruct* OnGetBridgedType() const override;
	UE_API virtual EControlFlow OnAccessSlots(const FReadSlotContext& InContext, TFunctionRef<EControlFlow(const FReadSlotContext&, const FReadSlot&)> InFunc, const FReadSlotOptions& InOptions) const override;
	UE_API virtual EControlFlow OnAccessSlots(const FWriteSlotContext& InContext, TFunctionRef<EControlFlow(const FWriteSlotContext&, FWriteSlot&)> InFunc, const FWriteSlotOptions& InOptions) const override;
	UE_API virtual TSubScriptStructOf<FAvaMaterialContainerState> OnGetContainerStateType() const override;
	UE_API virtual void OnApplyState(const FApplyStateContext& InContext, TConstStructView<FAvaMaterialContainerState> InContainerState, const FApplyStateOptions& InOptions) const override;
	UE_API virtual void OnStoreState(const FStoreStateContext& InContext, TStructView<FAvaMaterialContainerState> InContainerState, const FStoreStateOptions& InOptions) const override;
	//~ End FMaterialBridge
};

} // UE::Ava

#undef UE_API
