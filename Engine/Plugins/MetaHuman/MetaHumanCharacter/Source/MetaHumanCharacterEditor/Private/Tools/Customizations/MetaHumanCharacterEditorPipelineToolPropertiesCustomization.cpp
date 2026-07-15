// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/Customizations/MetaHumanCharacterEditorPipelineToolPropertiesCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"

#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "Tools/MetaHumanCharacterEditorPipelineTools.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorPipelineToolPropertiesCustomization"

void FMetaHumanCharacterEditorPipelineToolPropertiesCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TArray<TWeakObjectPtr<UMetaHumanCharacterEditorPipelineToolProperties>> CustomizedPipelineProperties = InDetailBuilder.GetObjectsOfTypeBeingCustomized<UMetaHumanCharacterEditorPipelineToolProperties>();
	if (!CustomizedPipelineProperties.IsValidIndex(0))
	{
		return;
	}

	UMetaHumanCharacterEditorPipelineToolProperties* PipelineToolProperties = CustomizedPipelineProperties[0].Get();
	if (!PipelineToolProperties)
	{
		return;
	}

	if (UMetaHumanCharacterEditorPipeline* ActiveEditorPipeline = PipelineToolProperties->GetSelectedEditorPipeline())
	{
		// Mark the pipeline as transactional so that undo/redo works for properties exposed via PipelineDisplay
		ActiveEditorPipeline->SetFlags(RF_Transactional);

		// Displayed pipeline properties are split into Target, Textures and Advanced Options categories
		const FString TargetsName = StaticEnum<EMetaHumanPipelineDisplayCategory>()->GetNameStringByValue(static_cast<int64>(EMetaHumanPipelineDisplayCategory::Targets));
		const FString MaterialsName = StaticEnum<EMetaHumanPipelineDisplayCategory>()->GetNameStringByValue(static_cast<int64>(EMetaHumanPipelineDisplayCategory::Materials));
		const FString TexturesName = StaticEnum<EMetaHumanPipelineDisplayCategory>()->GetNameStringByValue(static_cast<int64>(EMetaHumanPipelineDisplayCategory::Textures));
		const FString AdvancedName = StaticEnum<EMetaHumanPipelineDisplayCategory>()->GetNameStringByValue(static_cast<int64>(EMetaHumanPipelineDisplayCategory::Advanced));

		IDetailCategoryBuilder& TargetsCategory = InDetailBuilder.EditCategory(FName(*TargetsName), FText::GetEmpty(), ECategoryPriority::Uncommon);
		IDetailCategoryBuilder& TexturesCategory = InDetailBuilder.EditCategory(FName(*TexturesName), FText::GetEmpty(), ECategoryPriority::Uncommon);
		IDetailCategoryBuilder& MaterialsCategory = InDetailBuilder.EditCategory(FName(*MaterialsName), FText::GetEmpty(), ECategoryPriority::Uncommon);

		IDetailCategoryBuilder& PipelineCategory = InDetailBuilder.EditCategory("Advanced Options", LOCTEXT("PipelineToolCustomization_SectionOptions", "Advanced Options"), ECategoryPriority::Uncommon);
		PipelineCategory.SetToolTip(LOCTEXT("MetaHumanCharacterPipelineCustomization_AdvancedOptionsToolTip", "Experimental Features"));

		TArray<UObject*> PipelineAsArray = { ActiveEditorPipeline };
		UClass* FinalClass = ActiveEditorPipeline->GetClass();
		IDetailPropertyRow* BakeMaterialsRow = nullptr;
		for (TFieldIterator<FProperty> PropertyIt(FinalClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (Property && Property->HasMetaData(UMetaHumanCharacterEditorPipeline::PipelineDisplay))
			{
				if (Property->GetMetaData(UMetaHumanCharacterEditorPipeline::PipelineDisplay) == TargetsName)
				{
					TargetsCategory.AddExternalObjectProperty(PipelineAsArray, Property->GetFName());
				}
				else if (Property->GetMetaData(UMetaHumanCharacterEditorPipeline::PipelineDisplay) == TexturesName)
				{
					TexturesCategory.AddExternalObjectProperty(PipelineAsArray, Property->GetFName());
				}
				else if (Property->GetMetaData(UMetaHumanCharacterEditorPipeline::PipelineDisplay) == MaterialsName)
				{
					IDetailPropertyRow* MaterialRow = MaterialsCategory.AddExternalObjectProperty(PipelineAsArray, Property->GetFName());
					if (MaterialRow && Property->GetFName() == TEXT("bBakeMaterials"))
					{
						BakeMaterialsRow = MaterialRow;
					}
				}
				else if (Property->GetMetaData(UMetaHumanCharacterEditorPipeline::PipelineDisplay) == AdvancedName)
				{
					PipelineCategory.AddExternalObjectProperty(PipelineAsArray, Property->GetFName());
				}
				else
				{
					// TODO: log error?
				}
			}
		}

		if (BakeMaterialsRow)
		{
			// Disable bBakeMaterials when PipelineType is not Cinematic
			TWeakObjectPtr<UMetaHumanCharacterEditorPipelineToolProperties> WeakToolProperties = PipelineToolProperties;
			BakeMaterialsRow->IsEnabled(TAttribute<bool>::CreateLambda([WeakToolProperties]()
			{
				const UMetaHumanCharacterEditorPipelineToolProperties* ToolProps = WeakToolProperties.Get();
				return ToolProps && ToolProps->PipelineType == EMetaHumanDefaultPipelineType::Cinematic;
			}));
		}

		PipelineCategory.InitiallyCollapsed(true);
	}

	UMetaHumanCharacterEditorSettings* Settings = GetMutableDefault<UMetaHumanCharacterEditorSettings>();
	if (Settings)
	{
		if (!Settings->bEnableExperimentalWorkflows)
		{
			InDetailBuilder.HideCategory(TEXT("Advanced Options"));
		}

		// Register to changes in the experimental assembly options state
		if (!Settings->OnExperimentalAssemblyOptionsStateChanged.IsBoundToObject(this))
		{
			Settings->OnExperimentalAssemblyOptionsStateChanged.BindSP(this, &FMetaHumanCharacterEditorPipelineToolPropertiesCustomization::RebuildDetailsView);
		}
	}

	// Register to changes in the pipeline selection to rebuild the details view
	if (!PipelineToolProperties->OnPipelineSelectionChanged.IsBoundToObject(this))
	{
		PipelineToolProperties->OnPipelineSelectionChanged.BindSP(this, &FMetaHumanCharacterEditorPipelineToolPropertiesCustomization::RebuildDetailsView);
	}
}

void FMetaHumanCharacterEditorPipelineToolPropertiesCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	// Keep a wwak reference of the detail builder since it is re-created on every forced refresh
	CachedDetailBuilder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FMetaHumanCharacterEditorPipelineToolPropertiesCustomization::RebuildDetailsView()
{
	if (IDetailLayoutBuilder* DetailBuilder = CachedDetailBuilder.Pin().Get())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

#undef LOCTEXT_NAMESPACE
