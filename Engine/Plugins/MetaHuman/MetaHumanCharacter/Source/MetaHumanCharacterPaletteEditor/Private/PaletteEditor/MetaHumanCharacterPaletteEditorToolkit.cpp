// Copyright Epic Games, Inc. All Rights Reserved.

#include "PaletteEditor/MetaHumanCharacterPaletteEditorToolkit.h"
#include "PaletteEditor/MetaHumanCharacterPaletteEditorViewportClient.h"
#include "PaletteEditor/SMetaHumanCharacterPaletteEditorViewport.h"
#include "PaletteEditor/MetaHumanCharacterPaletteAssetEditor.h"
#include "PaletteEditor/MetaHumanCharacterPaletteEditorCommands.h"
#include "MetaHumanCharacterPaletteEditorAnalytics.h"
#include "MetaHumanCharacterActorInterface.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCharacterPaletteItemWrapper.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCharacterPaletteEditorLog.h"

#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "Editor/EditorEngine.h"
#include "EditorViewportTabContent.h"
#include "Framework/Docking/LayoutExtender.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Selection.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "ToolMenus.h"
#include "Dialog/SCustomDialog.h"
#include "Widgets/SCollectionItemTileView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/StructuredLog.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPaletteEditor"

static const FName PartsViewTabID("PartsView");
static const FName ItemDetailsTabID("ItemDetails");
static const FName ItemInstanceParametersTabID("ItemInstanceParameters");


namespace UE::MetaHuman::Private
{
	static void ShowFailureNotification(const FText& InMessage)
	{
		UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Error, "{Message}", InMessage.ToString());

		FNotificationInfo Info{ InMessage };
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 3.0f;
		Info.ExpireDuration = 5.0f;
		Info.bFireAndForget = true;
		Info.bUseThrobber = false;
		Info.bUseSuccessFailIcons = true;
		Info.bUseLargeFont = true;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationItem)
		{
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
			NotificationItem->ExpireAndFadeout();
		}
	}
}


FMetaHumanCharacterPaletteEditorToolkit::FMetaHumanCharacterPaletteEditorToolkit(UMetaHumanCharacterPaletteAssetEditor* InOwningAssetEditor)
	: FBaseAssetToolkit{ InOwningAssetEditor }
{
	ItemWrapper = TStrongObjectPtr(NewObject<UMetaHumanCharacterPaletteItemWrapper>());
	InstanceParameterWrapper = TStrongObjectPtr(NewObject<UMetaHumanPaletteEditorCollectionInstanceParameters>());

	StandaloneDefaultLayout = FTabManager::NewLayout(TEXT("MetaHumanCharacterPaletteEditorLayout_3"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(PartsViewTabID, ETabState::OpenedTab)
					->SetExtensionId("PartsView")
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(ViewportTabID, ETabState::OpenedTab)
					->SetExtensionId(TEXT("ViewportArea"))
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(ItemDetailsTabID, ETabState::OpenedTab)
						->SetExtensionId(TEXT("ItemDetailsArea"))
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(ItemInstanceParametersTabID, ETabState::OpenedTab)
						->SetExtensionId(TEXT("ItemInstanceParametersArea"))
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.5f)
						->AddTab(DetailsTabID, ETabState::OpenedTab)
						->SetExtensionId(TEXT("DetailsArea"))
					)
				)
			)
		);

	LayoutExtender = MakeShared<FLayoutExtender>();

	PreviewScene = MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues{});

	UMetaHumanCharacterPaletteAssetEditor* Editor = GetMutableCharacterEditor();

	if (Editor->IsCollectionEditable())
	{
		PreviewCollection = TStrongObjectPtr(DuplicateObject<UMetaHumanCollection>(Editor->GetMetaHumanCollection(), Editor));
		PreviewCollection->SetQuality(EMetaHumanCharacterPaletteBuildQuality::Preview);
		bPreviewCollectionNeedsBuild = true;

		// Never auto-apply instance edits to a Collection. They have to be applied along with the 
		// rest of the Collection edits, if any.
		//
		// This is because it's confusing to have two different kinds of Apply operation on the
		// Collection editor (one for the Collection and one for its default Instance), and you 
		// wouldn't want to auto-apply Collection edits, because it triggers a Production build
		// of the Collection, which can take a while.
		bShouldAutoApplyInstanceEdits = false;
	}

	PreviewInstance = TStrongObjectPtr(DuplicateObject<UMetaHumanInstance>(Editor->GetMetaHumanInstance(), Editor));
}

void FMetaHumanCharacterPaletteEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FBaseAssetToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PartsViewTabID, FOnSpawnTab::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::SpawnTab_PartsView))
		.SetDisplayName(LOCTEXT("PartsViewTab", "Items"));

	InTabManager->RegisterTabSpawner(ItemInstanceParametersTabID, FOnSpawnTab::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::SpawnTab_ItemInstanceParameters))
		.SetDisplayName(LOCTEXT("ItemInstanceParametersTab", "Instance Params"));

	if (GetCharacterEditor()->IsCollectionEditable())
	{
		// This tab edits build parameters of a part, so is only visible if the Character is editable
		InTabManager->RegisterTabSpawner(ItemDetailsTabID, FOnSpawnTab::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::SpawnTab_ItemDetails))
			.SetDisplayName(LOCTEXT("ItemDetailsTab", "Item Details"));
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FBaseAssetToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(PartsViewTabID);
	if (GetCharacterEditor()->IsCollectionEditable())
	{
		InTabManager->UnregisterTabSpawner(ItemDetailsTabID);
	}
}

TSharedRef<SDockTab> FMetaHumanCharacterPaletteEditorToolkit::SpawnTab_PartsView(const FSpawnTabArgs& Args)
{
	check(PreviewInstance.IsValid());

	return SNew(SDockTab)
		.Label(LOCTEXT("PartsViewTab", "Items"))
		.ToolTipText(LOCTEXT("PartsViewTabTooltip", "Shows the items currently in this Collection"))
		[
			SAssignNew(PartsViewWidget, SCollectionItemTileView)
			.MetaHumanInstance(PreviewInstance.Get())
			.MetaHumanCollection(PreviewCollection.Get())
			.OnSelectionChanged(this, &FMetaHumanCharacterPaletteEditorToolkit::OnPartsViewSelectionChanged)
			.OnMouseButtonDoubleClick(this, &FMetaHumanCharacterPaletteEditorToolkit::OnPartsViewDoubleClick)
			.IsCollectionEditable(GetCharacterEditor()->IsCollectionEditable())
			.OnCollectionModified(this, &FMetaHumanCharacterPaletteEditorToolkit::OnCollectionModified)
		];
}

TSharedRef<SDockTab> FMetaHumanCharacterPaletteEditorToolkit::SpawnTab_ItemDetails(const FSpawnTabArgs& Args)
{
	if (!ItemDetailsView.IsValid())
	{
		ItemDetailsView = CreateDetailsView();
		ItemDetailsView->OnFinishedChangingProperties().AddSP(this, &FMetaHumanCharacterPaletteEditorToolkit::OnFinishedChangingItemProperties);
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("ItemDetailsTab", "Item Details"))
		.ToolTipText(LOCTEXT("ItemDetailsTabTooltip", "The details of the currently selected item in the Items view"))
		[
			ItemDetailsView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FMetaHumanCharacterPaletteEditorToolkit::SpawnTab_ItemInstanceParameters(const FSpawnTabArgs& Args)
{
	if (!ItemInstanceParametersDetailsView.IsValid())
	{
		ItemInstanceParametersDetailsView = CreateDetailsView();
		ItemInstanceParametersDetailsView->OnFinishedChangingProperties().AddSP(this, &FMetaHumanCharacterPaletteEditorToolkit::OnFinishedChangingInstanceParameters);
	}

	return SNew(SDockTab)
		.Label(LOCTEXT("ItemInstanceParametersTab", "Instance Params"))
		.ToolTipText(LOCTEXT("ItemInstanceParametersTabTooltip", "The Instance Parameters for the currently selected item in the Items view"))
		[
			ItemInstanceParametersDetailsView.ToSharedRef()
		];
}

void FMetaHumanCharacterPaletteEditorToolkit::OnPartsViewSelectionChanged(TSharedPtr<FMetaHumanCharacterPaletteItem> NewSelectedItem, ESelectInfo::Type SelectInfo)
{
	if (NewSelectedItem.IsValid())
	{
		ItemWrapper->Item = *NewSelectedItem;

		if (ItemDetailsView)
		{
			const bool bForceRefresh = true;
			ItemDetailsView->SetObject(ItemWrapper.Get(), bForceRefresh);
		}
	}
	else
	{
		if (ItemDetailsView)
		{
			ItemDetailsView->SetObject(nullptr);
		}
	}

	CurrentlySelectedItem = NewSelectedItem;
	CurrentlySelectedItemKey = NewSelectedItem ? NewSelectedItem->GetItemKey() : FMetaHumanPaletteItemKey();
}

void FMetaHumanCharacterPaletteEditorToolkit::OnPartsViewDoubleClick(TSharedPtr<FMetaHumanCharacterPaletteItem> Item, FName SlotName)
{
	if (SlotName == NAME_None)
	{
		return;
	}

	// If Item is null, the slot selection will be cleared
	const FMetaHumanPaletteItemKey NewItemKey = Item.IsValid() ? Item->GetItemKey() : FMetaHumanPaletteItemKey();

	bInstanceHasUnappliedEdits = true;
	PreviewInstance->SetSingleSlotSelection(SlotName, NewItemKey);

	if (PartsViewWidget.IsValid())
	{
		PartsViewWidget->InvalidateSelectableState();
	}

	if (!ShouldBuildAllItemsOnEdit())
	{
		// Selected item has changed, so need to rebuild
		bPreviewCollectionNeedsBuild = true;
	}

	UpdatePreview();

	if (bShouldAutoApplyInstanceEdits)
	{
		ApplyEditsToAsset();
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::OnFinishedChangingItemProperties(const FPropertyChangedEvent& Event)
{
	if (!CurrentlySelectedItem.IsValid()
		|| !GetCharacterEditor()->IsCollectionEditable())
	{
		return;
	}

	check(PreviewCollection.IsValid());

	const FMetaHumanPaletteItemKey NewItemKey = ItemWrapper->Item.GetItemKey();
	const bool bKeyChanged = NewItemKey != CurrentlySelectedItemKey;
	const bool bKeyConflicts = bKeyChanged && PreviewCollection->ContainsItem(NewItemKey);

	if (bKeyConflicts)
	{
		// The user has modified the item such that its key collides with another item in the
		// collection. How we recover depends on which property they touched:
		//
		//   * Variation: the user explicitly chose a value, so respect their intent. Reject the
		//     edit and explain why.
		//   * WardrobeItem.PrincipalAsset (or any other Wardrobe-Item-internal property): the
		//     conflict is incidental, so silently permute Variation to keep the key unique.
		//   * Ambiguous (no MemberProperty supplied, e.g. some undo paths): default to rejection
		//     rather than silently overwriting whatever the user just did.
		const FName MemberPropertyName = Event.MemberProperty ? Event.MemberProperty->GetFName() : NAME_None;
		const FName VariationPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPaletteItem, Variation);
		const FName WardrobeItemPropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPaletteItem, WardrobeItem);

		const bool bShouldAutoPermute = (MemberPropertyName == WardrobeItemPropertyName);

		if (!bShouldAutoPermute)
		{
			const bool bIsVariationEdit = (MemberPropertyName == VariationPropertyName);
			const FName AttemptedVariation = ItemWrapper->Item.Variation;

			// Revert the wrapper to the pre-edit state so the details panel reflects reality.
			ItemWrapper->Item = *CurrentlySelectedItem;

			if (ItemDetailsView.IsValid())
			{
				const bool bForceRefresh = true;
				ItemDetailsView->SetObject(ItemWrapper.Get(), bForceRefresh);
			}

			const FText DialogTitle = bIsVariationEdit
				? LOCTEXT("VariationConflictTitle", "Variation Already In Use")
				: LOCTEXT("ItemKeyConflictTitle", "Item Key Already In Use");

			const FText DialogMessage = bIsVariationEdit
				? FText::Format(
					LOCTEXT(
						"VariationConflictMessage",
						"Another item in this Collection already uses the Variation \"{0}\" for this Wardrobe Item.\n\nEach (Wardrobe Item, Variation) pair must be unique. Pick a different Variation."),
					FText::FromName(AttemptedVariation))
				: LOCTEXT(
					"ItemKeyConflictMessage",
					"This change would make the item's key collide with another item in this Collection.\n\nThe edit was reverted. Try a different value.");

			TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
				.Title(DialogTitle)
				.Content()
				[
					SNew(STextBlock)
					.Text(DialogMessage)
					.AutoWrapText(true)
				]
				.Buttons({
					SCustomDialog::FButton(LOCTEXT("ItemKeyConflictOK", "OK"))
						.SetPrimary(true)
						.SetButtonRole(SCustomDialog::EButtonRole::Confirm)
						.SetFocus()
				});

			Dialog->ShowModal();
			return;
		}

		// Auto-permute for non-Variation edits.
		ItemWrapper->Item.Variation = PreviewCollection->GenerateUniqueVariationName(ItemWrapper->Item.GetItemKey());
		check(!PreviewCollection->ContainsItem(ItemWrapper->Item.GetItemKey()));

		if (ItemDetailsView.IsValid())
		{
			// Refresh so the user sees the new Variation we picked for them.
			const bool bForceRefresh = true;
			ItemDetailsView->SetObject(ItemWrapper.Get(), bForceRefresh);
		}
	}

	// Save off the key so that we can update CurrentlySelectedItemKey before calling 
	// WriteItemToCollection, in case that call triggers another function on this object.
	//
	// This ensures that CurrentlySelectedItem and CurrentlySelectedItemKey are guaranteed to be in
	// sync when any function is called.
	const FMetaHumanPaletteItemKey OldItemKey = CurrentlySelectedItemKey;

	// Copy property values from wrapper object back to actual selected item
	*CurrentlySelectedItem = ItemWrapper->Item;
	CurrentlySelectedItemKey = ItemWrapper->Item.GetItemKey();

	// Cosmetic-only edits shouldn't invalidate the collection build.
	const FName ChangedMemberPropertyName = Event.MemberProperty ? Event.MemberProperty->GetFName() : NAME_None;
	const FName DisplayNamePropertyName = GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterPaletteItem, DisplayName);
	const bool bAffectsBuild = (ChangedMemberPropertyName != DisplayNamePropertyName);

	// Commit the change back to the MetaHuman Collection asset as well
	PartsViewWidget->WriteItemToCollection(OldItemKey, CurrentlySelectedItem.ToSharedRef(), bAffectsBuild);

	if (!bAffectsBuild)
	{
		// Still mark the asset as having unapplied edits so the user is prompted to save, but
		// don't trigger a preview rebuild.
		bCollectionHasUnappliedEdits = true;
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::OnFinishedChangingInstanceParameters(const FPropertyChangedEvent& Event)
{
	TMap<FString, int32> ArrayIndicesPerObject;
	if (!Event.GetArrayIndicesPerObject(0, ArrayIndicesPerObject))
	{
		// TODO: log
		return;
	}

	const FName PropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanPaletteEditorCollectionInstanceParameters, UMetaHumanPaletteEditorCollectionInstanceParameters_Items);
	const int32* ArrayIndexPtr = ArrayIndicesPerObject.Find(PropertyName.ToString());
	if (!ArrayIndexPtr)
	{
		// TODO: log
		return;
	}

	if (!InstanceParameterWrapper->UMetaHumanPaletteEditorCollectionInstanceParameters_Items.IsValidIndex(*ArrayIndexPtr))
	{
		// TODO: log
		return;
	}

	const FMetaHumanPaletteEditorItemInstanceParameters& EditedParameters = InstanceParameterWrapper->UMetaHumanPaletteEditorCollectionInstanceParameters_Items[*ArrayIndexPtr];
	
	// TODO: Override only the property that has changed
	const EMetaHumanInstanceParameterOverrideResult OverrideResult = PreviewInstance->OverrideInstanceParameters(EditedParameters.ItemPath, EditedParameters.InstanceParameters);
	bInstanceHasUnappliedEdits = true;

	if (!IsCollectionEditable() && bShouldAutoApplyInstanceEdits)
	{
		ApplyEditsToAsset();
	}

	if (EnumHasAnyFlags(OverrideResult, EMetaHumanInstanceParameterOverrideResult::ReassemblyRequiredToApplyNewParameters))
	{
		UpdatePreview();
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::OnCollectionModified()
{
	bCollectionHasUnappliedEdits = true;
	bPreviewCollectionNeedsBuild = true;

	UpdatePreview();
}

void FMetaHumanCharacterPaletteEditorToolkit::OnFinishedChangingCollectionProperties(const FPropertyChangedEvent& Event)
{
	bCollectionHasUnappliedEdits = true;

	// UnpackPathMode and UnpackFolderPath only affect where the cooked assets land on disk, so
	// editing them shouldn't invalidate the build.
	const FName ChangedMemberPropertyName = Event.MemberProperty ? Event.MemberProperty->GetFName() : NAME_None;
	const FName UnpackPathModePropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanCollection, UnpackPathMode);
	const FName UnpackFolderPathPropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanCollection, UnpackFolderPath);

	const bool bAffectsBuild =
		ChangedMemberPropertyName != UnpackPathModePropertyName
		&& ChangedMemberPropertyName != UnpackFolderPathPropertyName;

	if (bAffectsBuild)
	{
		bPreviewCollectionNeedsBuild = true;
		UpdatePreview();
	}
}

AssetEditorViewportFactoryFunction FMetaHumanCharacterPaletteEditorToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction ViewportDelegateFunction = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SMetaHumanCharacterPaletteEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient);
	};

	return ViewportDelegateFunction;
}

TSharedPtr<FEditorViewportClient> FMetaHumanCharacterPaletteEditorToolkit::CreateEditorViewportClient() const
{
	return MakeShared<FMetaHumanCharacterPaletteViewportClient>(EditorModeManager.Get(), PreviewScene.Get());
}

TSharedRef<IDetailsView> FMetaHumanCharacterPaletteEditorToolkit::CreateDetailsView()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	// Use Automatic visibility for EditDefaultsOnly properties so that they are hidden when editing 
	// a Collection's pipeline properties.
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	return PropertyEditorModule.CreateDetailView(DetailsViewArgs);
}

void FMetaHumanCharacterPaletteEditorToolkit::RegisterToolbar()
{
	ToolkitCommands->MapAction(
		FMetaHumanCharacterPaletteEditorCommands::Get().Apply,
		FExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ApplyEditsToAsset),
		FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsApplyButtonEnabled));

	ToolkitCommands->MapAction(
		FMetaHumanCharacterPaletteEditorCommands::Get().AutoApplyInstanceEdits,
		FExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ToggleAutoApplyInstanceEdits),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ShouldAutoApplyInstanceEdits),
		FIsActionButtonVisible::CreateSPLambda(this, [this](){ return !IsCollectionEditable(); }));

	ToolkitCommands->MapAction(
		FMetaHumanCharacterPaletteEditorCommands::Get().RebuildProduction,
		FExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::BuildForProduction),
		FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ShouldViewportShowProductionBuild),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsCollectionEditable));

	ToolkitCommands->MapAction(
		FMetaHumanCharacterPaletteEditorCommands::Get().AutoBuildPreview,
		FExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ToggleAutoBuildForPreview),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsAutoBuildForPreviewEnabled),
		FIsActionButtonVisible::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsCollectionEditable));

	ToolkitCommands->MapAction(
		FMetaHumanCharacterPaletteEditorCommands::Get().BuildAllItemsOnEdit,
		FExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ToggleBuildAllItemsOnEdit),
		FCanExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsAutoBuildForPreviewEnabled),
		FIsActionChecked::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ShouldBuildAllItemsOnEdit),
		FIsActionButtonVisible::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsCollectionEditable));

	ToolkitCommands->MapAction(
		FMetaHumanCharacterPaletteEditorCommands::Get().ClearBuildCache,
		FExecuteAction::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::ClearBuildCache),
		FCanExecuteAction(),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::IsCollectionEditable));

	FName ParentToolbarName;
	const FName ToolbarName = GetToolMenuToolbarName(ParentToolbarName);
	UToolMenu* AssetToolbar = UToolMenus::Get()->ExtendMenu(ToolbarName);
	FToolMenuSection& Section = AssetToolbar->FindOrAddSection(TEXT("Asset"));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMetaHumanCharacterPaletteEditorCommands::Get().Apply));
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMetaHumanCharacterPaletteEditorCommands::Get().AutoApplyInstanceEdits));
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMetaHumanCharacterPaletteEditorCommands::Get().RebuildProduction));
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMetaHumanCharacterPaletteEditorCommands::Get().AutoBuildPreview));
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMetaHumanCharacterPaletteEditorCommands::Get().BuildAllItemsOnEdit));
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(FMetaHumanCharacterPaletteEditorCommands::Get().ClearBuildCache));
}

void FMetaHumanCharacterPaletteEditorToolkit::PostInitAssetEditor()
{
	UpdatePreview();

	// When editing a Collection, listen for property changes on the base Details view so we can
	// mark the collection as having unapplied edits when pipeline properties are modified.
	if (IsCollectionEditable())
	{
		DetailsView->OnFinishedChangingProperties().AddSP(this, &FMetaHumanCharacterPaletteEditorToolkit::OnFinishedChangingCollectionProperties);
	}

	// When the pipeline on the preview collection changes (e.g. pipeline swapped or specification
	// updated), refresh the items view to rebuild slot filter buttons and items from the new 
	// specification.
	if (PreviewCollection.IsValid())
	{
		PreviewCollection->OnPipelineChanged.AddSPLambda(this, [this]()
		{
			if (PartsViewWidget.IsValid())
			{
				PartsViewWidget->Refresh();
			}
			bPreviewCollectionNeedsBuild = true;
			UpdatePreview();
		});
	}

	// Refresh the UI when the target Collection is rebuilt.
	//
	// Note that this could happen outside of this editor, e.g. if this is an Instance
	// editor and the Collection it references is rebuilt by the Collection editor.
	if (UMetaHumanCollection* const CollectionToWatch = PreviewInstance.IsValid()
		? PreviewInstance->GetMetaHumanCollection()
		: nullptr)
	{
		CollectionToWatch->OnCollectionBuilt.AddSPLambda(this, [this]()
		{
			if (PartsViewWidget.IsValid())
			{
				PartsViewWidget->Refresh();
			}
			UpdatePreview();
		});
	}

	// We need the viewport client to start out focused, or else it won't get ticked until
	// we click inside it. This makes sure streaming of assets will actually finish before
	// the user clicks on the viewport
	if (ViewportClient->Viewport)
	{
		ViewportClient->ReceivedFocus(ViewportClient->Viewport);
	}

	// TODO: hard-coded values to set the camera in a sensible initial location.
	// This should really be handled by a focus viewport to selection
	ViewportClient->SetViewLocation(FVector{ 0, 155, 115 });
	ViewportClient->SetViewRotation(FRotator{ 0, -90, 0 });
	ViewportClient->SetLookAtLocation(FVector{ 0, 0, 115 });
	ViewportClient->ViewFOV = 18.001738f; // Same FoV used in MHC

	// Enable the orbit camera by default
	ViewportClient->ToggleOrbitCamera(true);
}

void FMetaHumanCharacterPaletteEditorToolkit::SetEditingObject(UObject* InObject)
{
	// When editing a Collection, point the Details view at the PreviewCollection instead of
	// the underlying asset, so that pipeline property edits go to the preview copy.
	if (IsCollectionEditable() && PreviewCollection.IsValid())
	{
		FBaseAssetToolkit::SetEditingObject(PreviewCollection.Get());
	}
	else
	{
		FBaseAssetToolkit::SetEditingObject(InObject);
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::ToggleAutoBuildForPreview()
{
	UMetaHumanCollection* Collection = GetCharacterEditor()->GetMetaHumanCollection();
	check(Collection);

	Collection->bAutoBuildForPreview = !Collection->bAutoBuildForPreview;
	Collection->MarkPackageDirty();
}

bool FMetaHumanCharacterPaletteEditorToolkit::IsAutoBuildForPreviewEnabled() const
{
	return GetCharacterEditor()->GetMetaHumanCollection()->bAutoBuildForPreview;
}

void FMetaHumanCharacterPaletteEditorToolkit::ApplyEditsToAsset()
{
	if (IsCollectionEditable())
	{
		if (bCollectionHasUnappliedEdits)
		{
			UMetaHumanCollection* CollectionAsset = GetCharacterEditor()->GetMetaHumanCollection();
			CollectionAsset->CopyContentsFrom(PreviewCollection.Get());

			// The Instance being edited is PreviewInstance, so any user choices will be stored 
			// there rather than the PreviewCollection's default Instance.
			UMetaHumanInstance* AssetDefaultInstance = CollectionAsset->GetMutableDefaultInstance();
			AssetDefaultInstance->CopyContentsFrom(PreviewInstance.Get());
			AssetDefaultInstance->SetMetaHumanCollection(CollectionAsset);

			FScopedSlowTask SlowTask(0, LOCTEXT("BuildProductionCollectionSlowTask", "Building Collection for Production..."));
			SlowTask.MakeDialog();

			UE::MetaHuman::Analytics::RecordBuildCollectionEvent(CollectionAsset);

			CollectionAsset->Build(
				FInstancedStruct(),
				UMetaHumanCollection::FOnBuildComplete::CreateLambda(
				[Toolkit = SharedThis(this)](EMetaHumanBuildStatus Status)
				{
					if (Status == EMetaHumanBuildStatus::Succeeded)
					{
						Toolkit->bCollectionHasUnappliedEdits = false;
						Toolkit->bInstanceHasUnappliedEdits = false;

						// The asset may have been mutated by ValidateCollection during the build. 
						//
						// Sync any fix-ups back to the preview Collection.
						if (Toolkit->PreviewCollection.IsValid())
						{
							UMetaHumanCollection* AssetCollection = Toolkit->GetCharacterEditor()->GetMetaHumanCollection();
							if (AssetCollection)
							{
								Toolkit->PreviewCollection->CopyContentsFrom(AssetCollection);

								if (Toolkit->DetailsView.IsValid())
								{
									Toolkit->DetailsView->ForceRefresh();
								}
							}
						}

						Toolkit->UpdatePreview();

						if (Toolkit->PartsViewWidget.IsValid())
						{
							Toolkit->PartsViewWidget->InvalidateSelectableState();
						}
					}
					else
					{
						UE::MetaHuman::Private::ShowFailureNotification(LOCTEXT(
							"BuildProductionCollectionFailed",
							"Failed to build MetaHuman Collection for production. See the log for details."));
					}
				}));
		}
		else if (bInstanceHasUnappliedEdits)
		{
			UMetaHumanCollection* CollectionAsset = GetCharacterEditor()->GetMetaHumanCollection();
			UMetaHumanInstance* AssetDefaultInstance = CollectionAsset->GetMutableDefaultInstance();
			AssetDefaultInstance->CopyContentsFrom(PreviewInstance.Get());

			if (!ensure(AssetDefaultInstance->GetMetaHumanCollection() == CollectionAsset))
			{
				// PreviewInstance should already have been pointing to the collection asset, but set 
				// it here anyway in case of a logic error somewhere else.
				AssetDefaultInstance->SetMetaHumanCollection(CollectionAsset);
			}

			AssetDefaultInstance->NotifyAssemblyOutputInvalidated();
			bInstanceHasUnappliedEdits = false;
		}

		// No changes to apply

		return;
	}

	// Instance editor
	if (bInstanceHasUnappliedEdits)
	{
		UMetaHumanCollection* CollectionAsset = GetCharacterEditor()->GetMetaHumanCollection();

		UMetaHumanInstance* InstanceAsset = GetCharacterEditor()->GetMetaHumanInstance();
		InstanceAsset->CopyContentsFrom(PreviewInstance.Get());

		if (!ensure(InstanceAsset->GetMetaHumanCollection() == CollectionAsset))
		{
			// PreviewInstance should already have been pointing to the collection asset, but set 
			// it here anyway in case of a logic error somewhere else.
			InstanceAsset->SetMetaHumanCollection(CollectionAsset);
		}

		InstanceAsset->NotifyAssemblyOutputInvalidated();
		bInstanceHasUnappliedEdits = false;
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::BuildForProduction()
{
	// This will copy the preview collection and instance back to the Collection asset and trigger 
	// a full Production build. 
	//
	// Useful if the source assets have changed and you need to rebuild the Collection.

	bCollectionHasUnappliedEdits = true;
	ApplyEditsToAsset();
}

bool FMetaHumanCharacterPaletteEditorToolkit::IsApplyButtonEnabled() const
{
	return bCollectionHasUnappliedEdits || bInstanceHasUnappliedEdits;
}

void FMetaHumanCharacterPaletteEditorToolkit::SaveAsset_Execute()
{
	if (PromptToApplyEditsBeforeSave())
	{
		FBaseAssetToolkit::SaveAsset_Execute();
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::SaveAssetAs_Execute()
{
	if (PromptToApplyEditsBeforeSave())
	{
		FBaseAssetToolkit::SaveAssetAs_Execute();
	}
}

FString FMetaHumanCharacterPaletteEditorToolkit::GetEditingAssetName() const
{
	const UMetaHumanCharacterPaletteAssetEditor* Editor = GetCharacterEditor();
	if (!Editor)
	{
		return FString();
	}

	const UObject* EditingAsset = Editor->IsCollectionEditable()
		? static_cast<const UObject*>(Editor->GetMetaHumanCollection())
		: static_cast<const UObject*>(Editor->GetMetaHumanInstance());

	return EditingAsset ? EditingAsset->GetName() : FString();
}

bool FMetaHumanCharacterPaletteEditorToolkit::PromptToApplyEditsBeforeSave()
{
	if (!IsApplyButtonEnabled())
	{
		return true;
	}

	TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(FText::Format(LOCTEXT("ApplyChangesOnSaveTitleFmt", "Apply Changes? -- {0}"), FText::FromString(GetEditingAssetName())))
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ApplyChangesOnSaveMessage",
				"This asset has unapplied edits.\n\n"
				"Saving without applying will mean your edits aren't saved into the asset.\n\n"
				"Would you like to apply your changes before saving?"))
			.AutoWrapText(true)
		]
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("ApplyAndSave", "Apply changes and save"))
				.SetPrimary(true)
				.SetButtonRole(SCustomDialog::EButtonRole::Confirm)
				.SetFocus(),
			SCustomDialog::FButton(LOCTEXT("SaveWithoutApplying", "Save without applying changes")),
			SCustomDialog::FButton(LOCTEXT("CancelSave", "Cancel"))
				.SetButtonRole(SCustomDialog::EButtonRole::Cancel)
		});

	// ShowModal returns the index of the button pressed, or -1 if the window was closed
	const int32 Pressed = Dialog->ShowModal();
	if (Pressed == 0)
	{
		ApplyEditsToAsset();
		return true;
	}
	if (Pressed == 1)
	{
		return true;
	}

	// Pressed == 2 (Cancel) or -1 (window dismissed) -- abort the save.
	return false;
}

bool FMetaHumanCharacterPaletteEditorToolkit::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
	// Skip the prompt for non-interactive close paths.
	if (InCloseReason == EAssetEditorCloseReason::AssetForceDeleted
		|| InCloseReason == EAssetEditorCloseReason::AssetUnloadingOrInvalid
		|| InCloseReason == EAssetEditorCloseReason::EditorRefreshRequested)
	{
		return FBaseAssetToolkit::OnRequestClose(InCloseReason);
	}

	if (IsApplyButtonEnabled())
	{
		TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
			.Title(FText::Format(LOCTEXT("ApplyChangesOnCloseTitleFmt", "Apply Changes? -- {0}"), FText::FromString(GetEditingAssetName())))
			.Content()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyChangesOnCloseMessage",
					"This asset has unapplied edits.\n\n"
					"Closing the editor without applying will discard those edits.\n\n"
					"Would you like to apply your changes before closing?"))
				.AutoWrapText(true)
			]
			.Buttons({
				SCustomDialog::FButton(LOCTEXT("ApplyChangesOnClose", "Apply changes"))
					.SetPrimary(true)
					.SetButtonRole(SCustomDialog::EButtonRole::Confirm)
					.SetFocus(),
				SCustomDialog::FButton(LOCTEXT("DiscardChangesOnClose", "Discard changes")),
				SCustomDialog::FButton(LOCTEXT("CancelClose", "Cancel"))
					.SetButtonRole(SCustomDialog::EButtonRole::Cancel)
			});

		// ShowModal returns the index of the button pressed, or -1 if the window was closed
		const int32 Pressed = Dialog->ShowModal();
		if (Pressed == 0)
		{
			ApplyEditsToAsset();
		}
		else if (Pressed == 2 || Pressed == -1)
		{
			// User cancelled or dismissed the dialog -- abort the close.
			return false;
		}
		// Pressed == 1 (Discard) -- fall through to close without applying.
	}

	return FBaseAssetToolkit::OnRequestClose(InCloseReason);
}

void FMetaHumanCharacterPaletteEditorToolkit::ToggleAutoApplyInstanceEdits()
{
	bShouldAutoApplyInstanceEdits = !bShouldAutoApplyInstanceEdits;
}

bool FMetaHumanCharacterPaletteEditorToolkit::ShouldAutoApplyInstanceEdits() const
{
	return bShouldAutoApplyInstanceEdits;
}

void FMetaHumanCharacterPaletteEditorToolkit::ToggleBuildAllItemsOnEdit()
{
	const UMetaHumanCharacterPaletteAssetEditor* Editor = GetCharacterEditor();
	UMetaHumanCollection* Collection = Editor->GetMetaHumanCollection();
	check(Collection);
	
	Collection->bBuildAllItemsForPreview = !Collection->bBuildAllItemsForPreview;

	// Force a build on the next edit to make sure the new policy is used
	bPreviewCollectionNeedsBuild = true;
}

bool FMetaHumanCharacterPaletteEditorToolkit::ShouldBuildAllItemsOnEdit() const
{
	const UMetaHumanCharacterPaletteAssetEditor* Editor = GetCharacterEditor();
	check(Editor->GetMetaHumanCollection());
	
	return Editor->GetMetaHumanCollection()->bBuildAllItemsForPreview;
}

void FMetaHumanCharacterPaletteEditorToolkit::ClearBuildCache()
{
	check(PreviewCollection.IsValid());

	TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(LOCTEXT("ClearBuildCacheTitle", "Clear Build Cache"))
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ClearBuildCacheMessage",
				"This will clear the build cache for this Collection.\n\n"
				"The next time the Collection is built, all items will be rebuilt from scratch, which may take a long time.\n\n"
				"Do you want to continue?"))
			.AutoWrapText(true)
		]
		.Buttons({
			SCustomDialog::FButton(LOCTEXT("ClearBuildCacheConfirm", "Clear Build Cache"))
				.SetPrimary(true),
			SCustomDialog::FButton(LOCTEXT("ClearBuildCacheCancel", "Cancel"))
		});

	// ShowModal returns the index of the button pressed, or -1 if the window was closed
	if (Dialog->ShowModal() == 0)
	{
		PreviewCollection->RefreshBuildCacheGuid();
		bCollectionHasUnappliedEdits = true;
	}
}

void FMetaHumanCharacterPaletteEditorToolkit::UpdatePreview()
{
	TSharedRef<FMetaHumanCharacterPaletteViewportClient> PaletteViewportClient = StaticCastSharedPtr<FMetaHumanCharacterPaletteViewportClient>(ViewportClient).ToSharedRef();

	// Set Collection to use
	UMetaHumanCollection* Collection;
	if (bCollectionHasUnappliedEdits)
	{
		check(PreviewCollection.IsValid());

		Collection = PreviewCollection.Get();
		PaletteViewportClient->OverlayText = LOCTEXT("ShowingPreviewQuality", "Build Quality: Preview");
	}
	else
	{
		const UMetaHumanCharacterPaletteAssetEditor* Editor = GetCharacterEditor();
		check(Editor->GetMetaHumanCollection());

		Collection = Editor->GetMetaHumanCollection();
		PaletteViewportClient->OverlayText = LOCTEXT("ShowingProductionQuality", "Build Quality: Production");
	}

	if (!Collection->GetEditorPipeline())
	{
		if (PreviewActor.IsValid())
		{
			PreviewActor->Destroy();
			PreviewActor = nullptr;
		}

		return;
	}

	if (PreviewInstance->GetMetaHumanCollection() != Collection)
	{
		if (PreviewActor.IsValid())
		{
			PreviewActor->Destroy();
			PreviewActor = nullptr;
		}

		PreviewInstance->SetMetaHumanCollection(Collection);
	}

	const bool bCollectionNeedsBuild = !ShouldViewportShowProductionBuild() && bPreviewCollectionNeedsBuild;

	if (!bCollectionNeedsBuild)
	{
		PreviewInstance->Assemble(
			FMetaHumanCharacterAssembledNative::CreateSP(this, &FMetaHumanCharacterPaletteEditorToolkit::OnMetaHumanCharacterAssembled));
		return;
	}

	if (!IsAutoBuildForPreviewEnabled())
	{
		// User doesn't want to build for preview
		return;
	}

	// We should never be building a Production collection to show a preview
	check(Collection->GetQuality() != EMetaHumanCharacterPaletteBuildQuality::Production);

	TArray<FMetaHumanPinnedSlotSelection> PinnedSlotSelections;
	if (!ShouldBuildAllItemsOnEdit())
	{
		PinnedSlotSelections = PreviewInstance->ToPinnedSlotSelections(EMetaHumanUnusedSlotBehavior::PinnedToEmpty);
	}

	FScopedSlowTask SlowTask(0, LOCTEXT("BuildPreviewCollectionSlowTask", "Building Collection for Preview..."));
	SlowTask.MakeDialog();

	Collection->Build(
		FInstancedStruct(),
		UMetaHumanCollection::FOnBuildComplete::CreateLambda(
			[Toolkit = SharedThis(this)](EMetaHumanBuildStatus Status)
			{
				if (Status == EMetaHumanBuildStatus::Succeeded)
				{
					Toolkit->bPreviewCollectionNeedsBuild = false;
					if (Toolkit->PreviewInstance.IsValid())
					{
						Toolkit->PreviewInstance->Assemble(
							FMetaHumanCharacterAssembledNative::CreateSP(Toolkit, &FMetaHumanCharacterPaletteEditorToolkit::OnMetaHumanCharacterAssembled));
					}

					if (Toolkit->PartsViewWidget.IsValid())
					{
						Toolkit->PartsViewWidget->InvalidateSelectableState();
						Toolkit->PartsViewWidget->OnPreviewBuildComplete(true);
					}
				}
				else
				{
					if (Toolkit->PreviewActor.IsValid())
					{
						Toolkit->PreviewActor->Destroy();
						Toolkit->PreviewActor = nullptr;
					}

					if (Toolkit->PartsViewWidget.IsValid())
					{
						Toolkit->PartsViewWidget->OnPreviewBuildComplete(false);
					}

					UE::MetaHuman::Private::ShowFailureNotification(LOCTEXT(
						"BuildPreviewCollectionFailed",
						"Failed to build MetaHuman Collection for preview. See the log for details."));
				}
			}),
		PinnedSlotSelections);
}

void FMetaHumanCharacterPaletteEditorToolkit::OnMetaHumanCharacterAssembled(EMetaHumanCharacterAssemblyResult Status)
{
	check(PreviewInstance.IsValid());

	if (PreviewActor.IsValid())
	{
		PreviewActor->Destroy();
		PreviewActor = nullptr;
	}

	// Clear instance parameter details view
	{
		InstanceParameterWrapper->UMetaHumanPaletteEditorCollectionInstanceParameters_Items.Empty();

		if (ItemInstanceParametersDetailsView)
		{
			ItemInstanceParametersDetailsView->SetObject(nullptr);
		}
	}

	if (Status != EMetaHumanCharacterAssemblyResult::Succeeded)
	{
		UE::MetaHuman::Private::ShowFailureNotification(LOCTEXT(
			"AssembleInstanceFailed",
			"Failed to assemble MetaHuman Instance. See the log for details."));
		return;
	}

	UMetaHumanCollection* Collection = PreviewInstance->GetMetaHumanCollection();
	if (!Collection
		|| !Collection->GetPipeline()
		|| !Collection->GetPipeline()->GetActorClass()
		|| !Collection->GetPipeline()->GetActorClass()->ImplementsInterface(UMetaHumanCharacterActorInterface::StaticClass()))
	{
		return;
	}

	// Populate instance parameter details view
	{
		for (const FMetaHumanPipelineSlotSelectionData& SlotSelectionData : PreviewInstance->GetSlotSelectionData())
		{
			const FMetaHumanPaletteItemPath ItemPath = SlotSelectionData.Selection.GetSelectedItemPath();
			FInstancedPropertyBag ItemInstanceParameters = PreviewInstance->GetCurrentInstanceParametersForItem(ItemPath);

			if (ItemInstanceParameters.IsValid())
			{
				FMetaHumanPaletteEditorItemInstanceParameters& ItemParameters = 
					InstanceParameterWrapper->UMetaHumanPaletteEditorCollectionInstanceParameters_Items.AddDefaulted_GetRef();

				ItemParameters.ItemPath = ItemPath;
				ItemParameters.InstanceParameters = MoveTemp(ItemInstanceParameters);

				// Resolve a friendly display name for the details panel category header.
				const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
				FMetaHumanCharacterPaletteItem ResolvedItem;
				if (Collection->TryResolveItem(ItemPath, ContainingPalette, ResolvedItem))
				{
					ItemParameters.DisplayName = ResolvedItem.GetOrGenerateDisplayName();
				}
			}
		}
		
		if (ItemInstanceParametersDetailsView)
		{
			const bool bForceRefresh = true;
			ItemInstanceParametersDetailsView->SetObject(InstanceParameterWrapper.Get(), bForceRefresh);
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PreviewActor = TWeakObjectPtr(PreviewScene->GetWorld()->SpawnActor<AActor>(Collection->GetPipeline()->GetActorClass(), SpawnParameters));
	if (!PreviewActor.IsValid())
	{
		// Failed to spawn
		return;
	}

	check(PreviewActor->Implements<UMetaHumanCharacterActorInterface>());

	IMetaHumanCharacterActorInterface::Execute_SetMetaHumanInstance(PreviewActor.Get(), PreviewInstance.Get());
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Old name for SetMetaHumanInstance
	IMetaHumanCharacterActorInterface::Execute_SetCharacterInstance(PreviewActor.Get(), PreviewInstance.Get());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

UMetaHumanCharacterPaletteAssetEditor* FMetaHumanCharacterPaletteEditorToolkit::GetMutableCharacterEditor()
{
	return Cast<UMetaHumanCharacterPaletteAssetEditor>(OwningAssetEditor);
}

const UMetaHumanCharacterPaletteAssetEditor* FMetaHumanCharacterPaletteEditorToolkit::GetCharacterEditor() const
{
	return Cast<UMetaHumanCharacterPaletteAssetEditor>(OwningAssetEditor);
}

bool FMetaHumanCharacterPaletteEditorToolkit::IsCollectionEditable() const
{
	return GetCharacterEditor() && GetCharacterEditor()->IsCollectionEditable();
}

bool FMetaHumanCharacterPaletteEditorToolkit::ShouldViewportShowProductionBuild() const
{
	return !bCollectionHasUnappliedEdits;
}

#undef LOCTEXT_NAMESPACE
