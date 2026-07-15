// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_MetaHumanCollection.h"

#include "MetaHumanInstance.h"
#include "MetaHumanCollection.h"
#include "PaletteEditor/MetaHumanCharacterPaletteAssetEditor.h"
#include "MetaHumanCharacterPaletteEditorAnalytics.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

FText UAssetDefinition_MetaHumanCollection::GetAssetDisplayName() const
{
	return LOCTEXT("MetaHumanCollectionDisplayName", "MetaHuman Collection");
}

FLinearColor UAssetDefinition_MetaHumanCollection::GetAssetColor() const
{
	return FColor::Orange;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaHumanCollection::GetAssetClass() const
{
	return UMetaHumanCollection::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaHumanCollection::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath{ LOCTEXT("MetaHumanAssetCategoryPath", "MetaHuman") } };
	return Categories;
}

EAssetCommandResult UAssetDefinition_MetaHumanCollection::OpenAssets(const FAssetOpenArgs& InOpenArgs) const
{
	EAssetCommandResult HandleResult = EAssetCommandResult::Unhandled;

	for (UMetaHumanCollection* Collection : InOpenArgs.LoadObjects<UMetaHumanCollection>())
	{
		UMetaHumanCharacterPaletteAssetEditor* PaletteEditor = NewObject<UMetaHumanCharacterPaletteAssetEditor>(GetTransientPackage(), NAME_None, RF_Transient);
		PaletteEditor->SetObjectToEdit(Collection);
		PaletteEditor->Initialize();

		UE::MetaHuman::Analytics::RecordOpenCollectionEditorEvent(Collection);

		HandleResult = EAssetCommandResult::Handled;
	}

	return HandleResult;
}

namespace MenuExtension_MetaHumanCollection
{

void ExecuteNewInstance(const FToolMenuContext& InContext)
{
	const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);
	check(Context);

	const FString DefaultSuffix = TEXT("_Instance");
	TArray<UObject*> ObjectsToSync;

	for (UMetaHumanCollection* Collection : Context->LoadSelectedObjects<UMetaHumanCollection>())
	{
		// Determine an appropriate name
		FString Name;
		FString PackageName;
		// Create Unique asset name
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().CreateUniqueAssetName(Collection->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);
		}
				
		UPackage* Pkg = CreatePackage(*PackageName);
		if (Pkg)
		{
			UMetaHumanInstance* Instance = NewObject<UMetaHumanInstance>(Pkg, FName(*Name), RF_Public | RF_Standalone);
			
			if (Instance)
			{
				Pkg->MarkPackageDirty();

				Instance->SetMetaHumanCollection(Collection);
				FAssetRegistryModule::AssetCreated(Instance);

				UE::MetaHuman::Analytics::RecordCreateInstanceEvent(Collection, Instance);

				ObjectsToSync.Add(Instance);
			}
		}
	}

	if (ObjectsToSync.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync, /*bAllowLockedBrowsers=*/true);

		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ObjectsToSync[0]);
	}
}

void ExecuteUnpackAssets(const FToolMenuContext& InContext)
{
	const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);
	check(Context);

	for (UMetaHumanCollection* Collection : Context->LoadSelectedObjects<UMetaHumanCollection>())
	{
		Collection->UnpackAssets();
	}
}

static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit,
	[]
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda(
			[]()
			{
				FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
				UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UMetaHumanCollection::StaticClass());

				FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
				Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda(
					[](FToolMenuSection& InSection)
					{
						// New instance
						{
							const TAttribute<FText> Label = LOCTEXT("ContextMenu_NewInstance", "Create New Instance");
							const TAttribute<FText> ToolTip = LOCTEXT("ContextMenu_NewInstanceTooltip", "Creates a new MetaHuman Instance from this Collection");
							const FSlateIcon Icon = FSlateIcon();

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteNewInstance);
							InSection.AddMenuEntry("MetaHumanCollection_ExecuteNewInstance", Label, ToolTip, Icon, UIAction);
						}
												
						// Unpack assets
						{
							const TAttribute<FText> Label = LOCTEXT("ContextMenu_UnpackAssets", "Unpack Assets");
							const TAttribute<FText> ToolTip = LOCTEXT("ContextMenu_UnpackAssetsTooltip", "Unpacks the built assets stored inside this Collection");
							const FSlateIcon Icon = FSlateIcon();

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteUnpackAssets);
							InSection.AddMenuEntry("MetaHumanCollection_ExecuteUnpackAssets", Label, ToolTip, Icon, UIAction);
						}
					}));
			}));
	});

} // MenuExtension_MetaHumanCollection

#undef LOCTEXT_NAMESPACE