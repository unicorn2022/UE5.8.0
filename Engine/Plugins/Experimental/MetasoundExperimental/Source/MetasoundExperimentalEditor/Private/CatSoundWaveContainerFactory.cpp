// Copyright Epic Games, Inc. All Rights Reserved.

#include "CatSoundWaveContainerFactory.h"

#include "CatSoundWaveContainer.h"

UCatSoundWaveContainerFactory::UCatSoundWaveContainerFactory()
{
	SupportedClass = UCatSoundWaveContainer::StaticClass();
	bCreateNew = true;
	bText = false;
	bEditorImport = false;
}

UObject* UCatSoundWaveContainerFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return NewObject<UCatSoundWaveContainer>(InParent, InName, Flags);
}
