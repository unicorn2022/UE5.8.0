// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFGraphFactoryAsset_Chooser.h"

#include "UAFAnimChooser.h"

void FUAFGraphFactoryAsset_Chooser::GetObjectReferences(TArray<const UObject*>& OutReferencedObjects) const
{ 
	OutReferencedObjects.Add(ChooserTable); 
}
