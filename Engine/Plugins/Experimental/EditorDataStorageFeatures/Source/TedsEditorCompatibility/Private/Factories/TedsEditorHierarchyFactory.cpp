// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/TedsEditorHierarchyFactory.h"

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

FName UTedsEditorHierarchyFactory::EditorObjectHierarchyName("EditorObjectHierarchy");

void UTedsEditorHierarchyFactory::RegisterHierarchies(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage;
	
	UE::Editor::DataStorage::FHierarchyRegistrationParams WorldHierarchyParams
	{
		.Name = EditorObjectHierarchyName, 
		.bEnableParentChangedColumn = true
	};
	
	DataStorage.RegisterHierarchy(WorldHierarchyParams);
}