// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/UAssetEditor.h"
#include "AssetEditor_TaggedAssetBrowserConfiguration.generated.h"

#define UE_API USERASSETTAGSEDITOR_API

/**
 * 
 */
UCLASS(MinimalAPI, Transient)
class UAssetEditor_TaggedAssetBrowserConfiguration : public UAssetEditor
{
	GENERATED_BODY()

public:
	UE_API void SetObjectToEdit(UObject* InObject);

protected:
	UE_API virtual void GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit) override;
	UE_API virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UObject> ObjectToEdit;
};

#undef UE_API
