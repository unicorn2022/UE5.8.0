// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "MetaHumanCharacterPipeline.h"

#include "MetaHumanCharacterActorInterface.generated.h"

class UMetaHumanInstance;

/**
 * Interface for actors that can be initialized from a UMetaHumanInstance.
 *
 * An actor implementing this interface can be used as a preview actor in the Collection editor.
 */
UINTERFACE(BlueprintType)
class METAHUMANCHARACTERPALETTE_API UMetaHumanCharacterActorInterface : public UInterface
{
	GENERATED_BODY()
};

class METAHUMANCHARACTERPALETTE_API IMetaHumanCharacterActorInterface
{
	GENERATED_BODY()

public:
	/** 
	 * Initializes the actor from the given MetaHuman Instance.
	 * 
	 * This function is called when the Instance has changed, so implementers *must* re-assemble 
	 * from the given Instance when this is called, even if this Instance was already set on the 
	 * actor.
	 * 
	 * The easiest way to do this is to call GetAssemblyOutput, which will trigger re-assembly only
	 * if needed.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, CallInEditor, Category = "MetaHuman|Character")
	void SetMetaHumanInstance(UMetaHumanInstance* MetaHumanInstance);

	UE_DEPRECATED(5.8, "This function has been renamed. Use SetMetaHumanInstance instead")
	UFUNCTION(BlueprintImplementableEvent, CallInEditor, meta=(DeprecatedFunction, DeprecationMessage="Use SetMetaHumanInstance instead"))
	void SetCharacterInstance(UMetaHumanInstance* CharacterInstance);

	/** Returns the MetaHuman Instance that this actor is currently using, even if it wasn't set by SetMetaHumanInstance */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, CallInEditor, Category = "MetaHuman|Character")
	UMetaHumanInstance* GetMetaHumanInstance() const;
};
