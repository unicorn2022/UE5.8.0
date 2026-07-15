// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFAnimNodeWidget.h"
#include "CoreMinimal.h"
#include "DetailCategoryBuilder.h"
#include "IObjectChooser.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "ObjectChooserWidgetFactories.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "AnimNode/UAFChooserPlayerNode.h"
#include "UAF/AnimNodes/IUAFAnimNodeDataHasAsset.h"
#include "UAFAnimNodeEditor/Public/UAFAnimNodeDataFactory.h"

#define LOCTEXT_NAMESPACE "UAFAnimNodeWidget"

namespace UE::UAF::ChooserEditor
{

using namespace UE::UAF::Chooser;

TSharedRef<SWidget> CreateUAFAnimNodeWidget(bool bReadOnly, UObject* TransactionObject, void* Value, UClass* ResultBaseClass,  UE::ChooserEditor::FChooserWidgetValueChanged ValueChanged)
{
	Chooser::FAnimNodeResult*  AnimNodeResult = static_cast<FAnimNodeResult*>(Value);

	const FUAFAnimNodeData* AnimNodeData = AnimNodeResult->NodeData.Get().GetPtr();

	UObject* Asset = nullptr;

	if (AnimNodeData)
	{
		if (const IUAFAnimNodeDataHasAsset* HasAsset = AnimNodeData->GetInterface<IUAFAnimNodeDataHasAsset>())
		{
			Asset = HasAsset->GetAsset();
		}
	}
	
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
				if (const UClass* AssetClass = AssetData.GetClass())
				{
					if (FUAFAnimNodeDataFactory::IsAssetRegistered(AssetClass))
					{
						return false;
					}
				}
				return true;
			}
			)
		.ObjectPath_Lambda([AnimNodeResult]()
			{
				if (const FUAFAnimNodeData* AnimNodeData = AnimNodeResult->NodeData.Get().GetPtr())
				{
					if (const IUAFAnimNodeDataHasAsset* HasAsset = AnimNodeData->GetInterface<IUAFAnimNodeDataHasAsset>())
					{
						if (UObject* Asset = HasAsset->GetAsset())
						{
							return Asset->GetPathName();
						}
					}
				}
				return FString();
			})
		.bOnlyRecognizeOnDragEnter(true)
		.OnObjectChanged_Lambda([TransactionObject, AnimNodeResult, ValueChanged](const FAssetData& AssetData)
		{
			const FScopedTransaction Transaction(LOCTEXT("Edit Asset", "Edit Asset"));
			TransactionObject->Modify(true);
			UObject* Asset = AssetData.GetAsset();
			
			if (Asset)
			{
				AnimNodeResult->NodeData.SetBase(FUAFAnimNodeDataFactory::CreateUAFAnimNodeDataFromObject(Asset));
			}
			else
			{
				AnimNodeResult->NodeData.SetBase(TInstancedStruct<FUAFAnimNodeData>());
			}
				
			ValueChanged.ExecuteIfBound();
			TransactionObject->PostEditChange();
	});
}

void RegisterChooserAnimNodeWidgets()
{
	UE::ChooserEditor::FObjectChooserWidgetFactories::RegisterWidgetCreator(Chooser::FAnimNodeResult::StaticStruct(), CreateUAFAnimNodeWidget);
}
	
}

#undef LOCTEXT_NAMESPACE
