// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/DragAndDrop/AssetUtilities.h"

#include "DragAndDrop/DropOperationInput.h"
#include "Editor.h"
#include "Editor/AssetReferenceFilter.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "LevelEditor.h"
#include "LevelEditorDragDropHandler.h"
#include "Modules/ModuleManager.h"
#include "TedsAssetDataColumns.h"

#define LOCTEXT_NAMESPACE "DragAndDrop_AssetUtilities"

namespace UE::Editor::DataStorage::Operations::Utilities
{

namespace AssetUtilities_Private
{
static bool IsLevelEditorWorld(TNotNull<const UWorld*> World)
{
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		if (TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule->GetFirstLevelEditor())
		{
			return LevelEditor->GetWorld() == World;
		}
	}
	return false;
}
}
	
UClass* GetSourceAssetClass(const ICoreProvider& Storage, RowHandle InputRow)
{
	RowHandle SourceRow = GetSourceRow(Storage, InputRow);
	const FAssetClassColumn* Column = Storage.GetColumn<FAssetClassColumn>(SourceRow);
	return Column ? LoadObject<UClass>(nullptr, *Column->ClassPath.ToString()) : nullptr;
}

const FAssetData* GetSourceAssetData(const ICoreProvider& Storage, RowHandle InputRow)
{
	RowHandle SourceRow = GetSourceRow(Storage, InputRow);
	const FAssetDataColumn_Experimental* Column = Storage.GetColumn<FAssetDataColumn_Experimental>(SourceRow);
	return Column ? &Column->AssetData : nullptr;
}

bool AssetDataPassesFilter(TNotNull<const UObject*> TargetObject, const FAssetData& AssetData, FText* OutError)
{
	if (!GEditor)
	{
		return true;
	}
	
	if (UWorld* World = TargetObject->GetWorld(); World && AssetUtilities_Private::IsLevelEditorWorld(World))
	{
		// The LevelEditorDragDropHandler was created for viewport placement and holds a lot of custom verification code (especially for FN) that
		// we cannot ignore. @todo: This should be properly refactored, but for now we simply call it without a viewport.
		if (ULevelEditorDragDropHandler* ViewportHandler = GEditor->GetLevelEditorDragDropHandler())
		{
			bool bResult = ViewportHandler->PreviewDropObjectsAtCoordinates(0, 0, World, nullptr, AssetData);
			if (OutError)
			{
				*OutError = ViewportHandler->GetPreviewDropHintText();
			}
			return bResult;
		}
	}
	
	FAssetReferenceFilterContext AssetReferenceFilterContext;
	AssetReferenceFilterContext.AddReferencingAsset(TargetObject);
	TSharedPtr<IAssetReferenceFilter> AssetReferenceFilter = GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext);
	return !AssetReferenceFilter || AssetReferenceFilter->PassesFilter(AssetData, OutError);
}

UObject* GetTargetObject(const ICoreProvider& Storage, RowHandle InputRow, FText* OutError)
{
	if (RowHandle TargetRow = GetDropTargetRow(Storage, InputRow); TargetRow != InvalidRowHandle)
	{
		if (const FTypedElementUObjectColumn* Column = Storage.GetColumn<FTypedElementUObjectColumn>(TargetRow))
		{
			if (UObject* Object = Column->Object.Get(); IsValid(Object))
			{
				return Object;
			}
		}
	}

	if (OutError)
	{
		*OutError = LOCTEXT("InvalidTarget", "Invalid target object.");
	}	
	return nullptr;
}

UObject* GetTargetObjectWithAssetValidation(const ICoreProvider& Storage, RowHandle InputRow, const FAssetData& AssetData, FText* OutError)
{
	UObject* TargetObject = GetTargetObject(Storage, InputRow, OutError);
	return (TargetObject && AssetDataPassesFilter(TargetObject, AssetData, OutError)) ? TargetObject : nullptr;
}

bool PrePlaceActorValidation(TNotNull<const UObject*> TargetObject, const FAssetData& AssetData)
{
	if (GEditor)
	{
		if (UWorld* World = TargetObject->GetWorld(); World && AssetUtilities_Private::IsLevelEditorWorld(World))
		{
			// Call the LevelEditorDragDropHandler to be on par with the old viewport placement flow. @todo: This should be properly refactored.
			if (ULevelEditorDragDropHandler* ViewportHandler = GEditor->GetLevelEditorDragDropHandler())
			{
				if (UObject* DroppedObject = AssetData.GetAsset())
				{
					TArray<AActor*> TempActors;
					return ViewportHandler->PreDropObjectsAtCoordinates(0, 0, World, nullptr, { DroppedObject }, TempActors);
				}
			}
		}
	}
	return true;
}

void PostPlaceActorValidation(TNotNull<const UObject*> TargetObject, const FAssetData& AssetData)
{
	if (GEditor)
	{
		if (UWorld* World = TargetObject->GetWorld(); World && AssetUtilities_Private::IsLevelEditorWorld(World))
		{
			// Call the LevelEditorDragDropHandler to be on par with the old viewport placement flow. @todo: This should be properly refactored.
			if (ULevelEditorDragDropHandler* ViewportHandler = GEditor->GetLevelEditorDragDropHandler())
			{
				if (UObject* DroppedObject = AssetData.GetAsset())
				{
					ViewportHandler->PostDropObjectsAtCoordinates(0, 0, World, nullptr, { DroppedObject });
				}
			}
		}
	}
}
	
}

#undef LOCTEXT_NAMESPACE
