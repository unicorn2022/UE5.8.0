// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCollectionFactory.h"

#include "MetaHumanCollection.h"
#include "MetaHumanCharacterPaletteEditorAnalytics.h"

#define LOCTEXT_NAMESPACE "MetaHumanCollectionFactory"

UMetaHumanCollectionFactory::UMetaHumanCollectionFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanCollection::StaticClass();
}

UObject* UMetaHumanCollectionFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn)
{
	UMetaHumanCollection* NewCollection = NewObject<UMetaHumanCollection>(InParent, InClass, InName, InFlags | RF_Transactional);

	UE::MetaHuman::Analytics::RecordCreateCollectionEvent(NewCollection);

	return NewCollection;
}

FText UMetaHumanCollectionFactory::GetToolTip() const
{
	return LOCTEXT("MetaHumanCollectionFactory_ToolTip", "MetaHuman Collection");
}

#undef LOCTEXT_NAMESPACE
