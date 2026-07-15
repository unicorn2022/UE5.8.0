// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanInstanceActorFactory.h"

#include "AssetRegistry/AssetData.h"
#include "MetaHumanCharacterActorInterface.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCollectionPipeline.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MetaHumanInstanceActorFactory"

UMetaHumanInstanceActorFactory::UMetaHumanInstanceActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
    DisplayName = LOCTEXT("MetaHumanInstanceDisplayName", "MetaHuman Instance");
}

UClass* UMetaHumanInstanceActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
	UMetaHumanCollection* Collection = nullptr;
	if (UMetaHumanInstance* CharacterInstance = Cast<UMetaHumanInstance>(AssetData.GetAsset()))
	{
		Collection = CharacterInstance->GetMetaHumanCollection();
	}

	if (Collection
		&& Collection->GetPipeline())
	{
		return Collection->GetPipeline()->GetActorClass();
	}
	else
	{
		return nullptr;
	}
}

void UMetaHumanInstanceActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	check(NewActor->Implements<UMetaHumanCharacterActorInterface>());

	// If CanCreateActorFrom returned true, this asset must be a valid Instance
	UMetaHumanInstance* CharacterInstance = CastChecked<UMetaHumanInstance>(Asset);

	IMetaHumanCharacterActorInterface::Execute_SetMetaHumanInstance(NewActor, CharacterInstance);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Old name for SetMetaHumanInstance
	IMetaHumanCharacterActorInterface::Execute_SetCharacterInstance(NewActor, CharacterInstance);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

}

UObject* UMetaHumanInstanceActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	// TODO: Find out if this function is worth implementing
	return nullptr;
}

bool UMetaHumanInstanceActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (!AssetData.IsValid())
	{
		return false;
	}

	UMetaHumanCollection* Collection = nullptr;
	if (UMetaHumanInstance* CharacterInstance = Cast<UMetaHumanInstance>(AssetData.GetAsset()))
	{
		Collection = CharacterInstance->GetMetaHumanCollection();
	}

	if (!Collection)
	{
        OutErrorMsg = LOCTEXT("NoValidAsset", "A valid MetaHuman Instance must be specified");
		return false;
	}

	if (!Collection->GetPipeline())
	{
        OutErrorMsg = LOCTEXT("NoValidPipeline", "The MetaHuman Collection doesn't have an associated Character Pipeline");
		return false;
	}

	if (!Collection->GetPipeline()->GetActorClass())
	{
        OutErrorMsg = LOCTEXT("NoActor", "The Character Pipeline doesn't specify a type of actor to spawn");
		return false;
	}

	if (!Collection->GetPipeline()->GetActorClass()->ImplementsInterface(UMetaHumanCharacterActorInterface::StaticClass()))
	{
        OutErrorMsg = LOCTEXT("NoActorInterface", "The Character Pipeline's actor doesn't implement IMetaHumanCharacterActorInterface");
		return false;
	}

    return true;
}

#undef LOCTEXT_NAMESPACE
