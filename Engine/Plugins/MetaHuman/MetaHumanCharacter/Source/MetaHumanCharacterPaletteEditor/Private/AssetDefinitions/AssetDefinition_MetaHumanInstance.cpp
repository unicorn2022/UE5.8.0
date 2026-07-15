// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_MetaHumanInstance.h"

#include "ReplaceItemDialog.h"

#include "PaletteEditor/MetaHumanCharacterPaletteAssetEditor.h"
#include "MetaHumanCharacterPaletteEditorAnalytics.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCharacterPaletteEditorLog.h"
#include "MetaHumanInstanceActorFactory.h"

#include "Logging/StructuredLog.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPalette"

FText UAssetDefinition_MetaHumanInstance::GetAssetDisplayName() const
{
	return LOCTEXT("MetaHumanInstanceDisplayName", "MetaHuman Instance");
}

FLinearColor UAssetDefinition_MetaHumanInstance::GetAssetColor() const
{
	return FColor::Orange;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaHumanInstance::GetAssetClass() const
{
	return UMetaHumanInstance::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaHumanInstance::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath{ LOCTEXT("MetaHumanAssetCategoryPath", "MetaHuman") } };
	return Categories;
}

EAssetCommandResult UAssetDefinition_MetaHumanInstance::OpenAssets(const FAssetOpenArgs& InOpenArgs) const
{
	EAssetCommandResult HandleResult = EAssetCommandResult::Unhandled;

	for (UMetaHumanInstance* Instance : InOpenArgs.LoadObjects<UMetaHumanInstance>())
	{
		if (Instance->GetMetaHumanCollection() != nullptr)
		{
			UMetaHumanCharacterPaletteAssetEditor* PaletteEditor = NewObject<UMetaHumanCharacterPaletteAssetEditor>(GetTransientPackage(), NAME_None, RF_Transient);
			PaletteEditor->SetObjectToEdit(Instance);
			PaletteEditor->Initialize();

			UE::MetaHuman::Analytics::RecordOpenInstanceEditorEvent(Instance->GetMetaHumanCollection(), Instance);

			HandleResult = EAssetCommandResult::Handled;
		}
	}

	return HandleResult;
}

namespace MenuExtension_MetaHumanInstance
{

void ExecutePlaceInLevel(const FToolMenuContext& InContext)
{
	const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);
	check(Context);

	UActorFactory* ActorFactory = GEditor->FindActorFactoryByClass(UMetaHumanInstanceActorFactory::StaticClass());
	if (!ActorFactory)
	{
		UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Error, "Failed to find UMetaHumanInstanceActorFactory");
		return;
	}

	TArray<UMetaHumanInstance*> Instances = Context->LoadSelectedObjects<UMetaHumanInstance>();
	if (Instances.Num() == 0)
	{
		// Nothing to do
		return;
	}

	const int32 GridWidth = FMath::CeilToInt32(FMath::Sqrt(static_cast<float>(Instances.Num())));
	const int32 GridDepth = FMath::CeilToInt32(static_cast<float>(Instances.Num()) / static_cast<float>(GridWidth));
	const float WidthSpacing = 150.0f;
	const float DepthSpacing = 200.0f;
	
	int32 Count = 0;
	while (Instances.Num() > 0)
	{
		const int32 Index = FMath::RandRange(0, Instances.Num() - 1);
		const UMetaHumanInstance* Instance = Instances[Index];

		// Place the actors near the origin for now. 
		//
		// In future this could be based on the currently selected actor position or similar, to 
		// make it more controllable if we wanted to.
		FVector SpawnLocation;
		SpawnLocation.X = (Count % GridWidth) * WidthSpacing;
		SpawnLocation.Y = (Count / GridWidth) * DepthSpacing;
		SpawnLocation.Z = 0.0f;

		const FTransform SpawnTransform(SpawnLocation);
		GEditor->UseActorFactory(ActorFactory, FAssetData(Instance), &SpawnTransform);

		Instances.RemoveAtSwap(Index);
		Count++;
	}
}

static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit,
	[]
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda(
			[]()
			{
				FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
				UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UMetaHumanInstance::StaticClass());

				FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
				Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda(
					[](FToolMenuSection& InSection)
					{
						// Place in Level
						{
							const TAttribute<FText> Label = LOCTEXT("ContextMenu_PlaceInLevel", "Place in Level");
							const TAttribute<FText> ToolTip = LOCTEXT("ContextMenu_PlaceInLevelTooltip", "Place the selected MetaHuman Instances in the level");
							const FSlateIcon Icon = FSlateIcon();

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecutePlaceInLevel);
							InSection.AddMenuEntry("MetaHumanInstance_ExecutePlaceInLevel", Label, ToolTip, Icon, UIAction);
						}

						// Replace Item
						{
							const TAttribute<FText> Label = LOCTEXT("ContextMenu_ReplaceItem", "Replace Item...");
							const TAttribute<FText> ToolTip = LOCTEXT("ContextMenu_ReplaceItemTooltip", "Replace items in the selected MetaHuman Instances");
							const FSlateIcon Icon = FSlateIcon();

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda(
								[](const FToolMenuContext& InContext)
								{
									const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);
									check(Context);
									UE::MetaHuman::OpenReplaceItemDialog(Context->LoadSelectedObjects<UMetaHumanInstance>());
								});
							InSection.AddMenuEntry("MetaHumanInstance_ExecuteReplaceItem", Label, ToolTip, Icon, UIAction);
						}
					}));
			}));
	});

} // MenuExtension_MetaHumanInstance

#undef LOCTEXT_NAMESPACE
