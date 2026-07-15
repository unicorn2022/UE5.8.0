// Copyright Epic Games, Inc. All Rights Reserved.

#include "PaletteEditor/MetaHumanCharacterPaletteAssetEditor.h"
#include "PaletteEditor/MetaHumanCharacterPaletteEditorToolkit.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCollection.h"

void UMetaHumanCharacterPaletteAssetEditor::GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit)
{
	if (bIsCollectionEditable)
	{
		OutObjectsToEdit.Add(Collection);
	}
	else
	{
		OutObjectsToEdit.Add(Instance);
	}
}

TSharedPtr<FBaseAssetToolkit> UMetaHumanCharacterPaletteAssetEditor::CreateToolkit()
{
	return MakeShared<FMetaHumanCharacterPaletteEditorToolkit>(this);
}

void UMetaHumanCharacterPaletteAssetEditor::SetObjectToEdit(UMetaHumanCollection* InObject)
{
	check(InObject);

	Collection = InObject;
	Instance = Collection->GetMutableDefaultInstance();

	bIsCollectionEditable = true;
}

void UMetaHumanCharacterPaletteAssetEditor::SetObjectToEdit(UMetaHumanInstance* InObject)
{
	check(InObject);

	Instance = InObject;
	Collection = Instance->GetMetaHumanCollection();
	// It's possible for an Instance to be created with a null Collection, but callers should not
	// try to open this asset editor on an Instance that's in that state.
	check(Collection);

	bIsCollectionEditable = false;
}
