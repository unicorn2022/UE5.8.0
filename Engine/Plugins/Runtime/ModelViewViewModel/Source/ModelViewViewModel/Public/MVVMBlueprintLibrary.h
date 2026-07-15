// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "MVVMBlueprintLibrary.generated.h"

UCLASS()
class UMVVMBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="MVVM | Utilities")
	static MODELVIEWVIEWMODEL_API void SetViewModelByClass(UUserWidget* Widget, TScriptInterface<INotifyFieldValueChanged> ViewModel);
};