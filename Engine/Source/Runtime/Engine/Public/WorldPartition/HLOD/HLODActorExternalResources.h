// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"

#if WITH_EDITOR
#include "UObject/ObjectSaveContext.h"
#endif

#include "HLODActorExternalResources.generated.h"

UCLASS()
class UHLODActorExternalResources : public UDataAsset
{
	GENERATED_BODY()

protected:
#if WITH_EDITOR
	ENGINE_API virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
#endif // WITH_EDITOR

public:
	UPROPERTY(VisibleAnywhere, Category=Resources)
	TArray<TObjectPtr<UObject> > Resources;
};