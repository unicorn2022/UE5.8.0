// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFGraphChooserWidget.h"
#include "CoreMinimal.h"
#include "DetailCategoryBuilder.h"
#include "IObjectChooser.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "ObjectChooserWidgetFactories.h"
#include "ObjectChooser_UAFGraph.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Factory/AnimGraphFactory.h"
#include "UAF/UAFAssetData.h"
#include "UAF/UAFAssetFactory.h"

#define LOCTEXT_NAMESPACE "UAFGraphChooserWidget"

namespace UE::UAF::ChooserEditor
{
	TSharedRef<SWidget> CreateUAFGraphWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ResultBaseClass,  UE::ChooserEditor::FChooserWidgetValueChanged ValueChanged)
	{
		FUAFGraphChooser* GraphChooser = static_cast<FUAFGraphChooser*>(Value);
	 
		FAssetReferenceFilterContext AssetReferenceFilterContext;
    	AssetReferenceFilterContext.AddReferencingAssets({&TransactionObject, 1});
    	TSharedPtr<IAssetReferenceFilter> AssetReferenceFilter = GEditor ? GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext) : nullptr;	
		
		return SNew(SObjectPropertyEntryBox)
			.IsEnabled(!bReadOnly)
			.AllowedClass(UObject::StaticClass())
			.OnShouldFilterAsset_Lambda([AssetReferenceFilter](const FAssetData& AssetData)
				{
					if (AssetReferenceFilter.IsValid())
					{
						 if (!AssetReferenceFilter->PassesFilter(AssetData))
						 {
						 	// filter out assets that can't be referenced due to to being in unrelated game feature plugins
							return true;
						 }
					}

					if (UClass* Class = AssetData.GetClass())
					{
						return !UE::UAF::FAssetDataFactory::GetRegisteredObjectClasses().Contains(Class);
					}

					return true;
				}
				)
			.ObjectPath_Lambda([GraphChooser]()
				{
					const UObject* Object = GraphChooser->GetReferencedObject(); 
					return Object != nullptr ? Object->GetPathName() : "";
				})
			.bOnlyRecognizeOnDragEnter(true)
			.OnObjectChanged_Lambda([TransactionObject, GraphChooser, ValueChanged](const FAssetData& AssetData)
			{
				const FScopedTransaction Transaction(LOCTEXT("Edit Asset", "Edit Asset"));
				TransactionObject->Modify(true);
				UObject* Asset = AssetData.GetAsset();

				if (Asset)
				{
					// Create a UAF asset from the uobject
					FGraphAssetHandle UAFAsset = UE::UAF::FAssetDataFactory::CreateUAFAssetDataFromObject<FUAFGraphFactoryAsset>(Asset);
					if (UAFAsset.IsValid() && UAFAsset.Get().Validate())
					{
						GraphChooser->AssetData = MoveTemp(UAFAsset);
					}
				}
				else
				{
					GraphChooser->AssetData.Reset();
				}
				TransactionObject->PostEditChange();
					
				ValueChanged.ExecuteIfBound();
			});
	}
	
	void RegisterChooserWidgets()
	{
		UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterWidgetCreator(FUAFGraphChooser::StaticStruct(), CreateUAFGraphWidget);
	}
}

#undef LOCTEXT_NAMESPACE
