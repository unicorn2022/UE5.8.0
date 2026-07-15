// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplaceItemDialog.h"

#include "MetaHumanInstance.h"
#include "MetaHumanCharacterPaletteEditorLog.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionPipeline.h"

#include "MetaHumanCharacterPipelineSpecification.h"

#include "Editor/EditorEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

extern UNREALED_API class UEditorEngine* GEditor;

#define LOCTEXT_NAMESPACE "MetaHumanCharacterPalette"

// ---------------------------------------------------------------------------
// Replace Item dialog
// ---------------------------------------------------------------------------

/** Row data for the item list views in the Replace Item dialog */
struct FReplaceItemEntry
{
	FMetaHumanPaletteItemKey ItemKey;
	FText DisplayName;
	FName SlotName;
	int32 InstanceCount = 0;
	bool bSelected = false;
	bool bStale = false;
};

/**
 * A two-step modal dialog for the Replace Item workflow.
 *
 * Step 1 -- choose one or more Source Items from the Collection.
 * Step 2 -- choose one or more Target Items (or none) to replace them with.
 *          When multiple targets are chosen the replacements are distributed
 *          randomly but evenly across the selected targets.
 *
 * The dialog is shown as a modal window and the result can be queried after
 * it has been closed.
 */
class SReplaceItemDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReplaceItemDialog) {}
		SLATE_ARGUMENT(TArray<TSharedPtr<FReplaceItemEntry>>, Items)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SWindow>& InParentWindow)
	{
		ParentWindow = InParentWindow;
		AllItems = InArgs._Items;

		// Build the target list: same items + a "(None)" entry at the top
		TargetNoneEntry = MakeShared<FReplaceItemEntry>();
		TargetNoneEntry->DisplayName = LOCTEXT("ReplaceItem_None", "(None -- remove item)");
		TargetNoneEntry->SlotName = NAME_None;
		bFilterBySlot = true;

		ChildSlot
		[
			SNew(SVerticalBox)

			// --- Source selection (Step 1) ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 8.0f, 8.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ReplaceItem_SourceHeader", "Select one or more Source Items to replace:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(SourceListBox, SVerticalBox)
				]
			]

			// --- Target selection (Step 2) ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 12.0f, 8.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ReplaceItem_TargetHeader", "Select one or more Target Items to replace them with:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(TargetListBox, SVerticalBox)
				]
			]

			// --- Filter checkbox ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bFilterBySlot ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bFilterBySlot = (NewState == ECheckBoxState::Checked);
						RebuildSourceList();
						RebuildTargetList();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ReplaceItem_FilterBySlot", "Filter lists by selected source slot"))
				]
			]

			// --- Buttons ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(8.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ReplaceItem_OK", "OK"))
					.IsEnabled(this, &SReplaceItemDialog::IsOKEnabled)
					.OnClicked(this, &SReplaceItemDialog::OnOKClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ReplaceItem_Cancel", "Cancel"))
					.OnClicked(this, &SReplaceItemDialog::OnCancelClicked)
				]
			]
		];

		RebuildSourceList();
		RebuildTargetList();
	}

	bool WasConfirmed() const { return bConfirmed; }

	TArray<FMetaHumanPaletteItemKey> GetSelectedSourceItems() const
	{
		TArray<FMetaHumanPaletteItemKey> Result;
		for (const TSharedPtr<FReplaceItemEntry>& Entry : AllItems)
		{
			if (Entry->bSelected)
			{
				Result.Add(Entry->ItemKey);
			}
		}
		return Result;
	}

	TArray<FMetaHumanPaletteItemKey> GetSelectedTargetItems() const
	{
		TArray<FMetaHumanPaletteItemKey> Result;
		for (const TSharedPtr<FReplaceItemEntry>& Entry : SelectedTargetEntries)
		{
			Result.Add(Entry->ItemKey);
		}
		return Result;
	}

	bool IsTargetNoneSelected() const
	{
		return bTargetNoneSelected;
	}

private:

	TSet<FName> GetSelectedSourceSlots() const
	{
		TSet<FName> Slots;
		for (const TSharedPtr<FReplaceItemEntry>& Entry : AllItems)
		{
			if (Entry->bSelected)
			{
				Slots.Add(Entry->SlotName);
			}
		}
		return Slots;
	}

	bool ShouldShowEntry(const TSharedPtr<FReplaceItemEntry>& Entry) const
	{
		if (!bFilterBySlot)
		{
			return true;
		}
		const TSet<FName> SelectedSlots = GetSelectedSourceSlots();
		if (SelectedSlots.Num() == 0)
		{
			return true;
		}
		return SelectedSlots.Contains(Entry->SlotName);
	}

	void RebuildSourceList()
	{
		SourceListBox->ClearChildren();
		for (const TSharedPtr<FReplaceItemEntry>& Entry : AllItems)
		{
			if (!ShouldShowEntry(Entry))
			{
				continue;
			}
			TSharedPtr<FReplaceItemEntry> PinnedEntry = Entry;
			SourceListBox->AddSlot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([PinnedEntry]()
					{
						return PinnedEntry->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, PinnedEntry](ECheckBoxState NewState)
					{
						PinnedEntry->bSelected = (NewState == ECheckBoxState::Checked);
						if (bFilterBySlot)
						{
							RebuildSourceList();
							RebuildTargetList();
						}
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
						SNew(STextBlock)
					.Text(FText::Format(
						Entry->bStale
							? LOCTEXT("ReplaceItem_SourceFormatStale", "{0} [{1}] ({2} {2}|plural(one=instance,other=instances)) (Missing from Collection)")
							: LOCTEXT("ReplaceItem_SourceFormat", "{0} [{1}] ({2} {2}|plural(one=instance,other=instances))"),
						Entry->DisplayName,
						FText::FromName(Entry->SlotName),
						FText::AsNumber(Entry->InstanceCount)))
				]
			];
		}
	}

	void RebuildTargetList()
	{
		TargetListBox->ClearChildren();

		// Add the "(None)" option
		AddTargetRow(TargetNoneEntry);

		// Add all non-stale items that pass the slot filter
		for (const TSharedPtr<FReplaceItemEntry>& Entry : AllItems)
		{
			if (Entry->bStale || !ShouldShowEntry(Entry))
			{
				continue;
			}
			AddTargetRow(Entry);
		}
	}

	void AddTargetRow(const TSharedPtr<FReplaceItemEntry>& Entry)
	{
		TSharedPtr<FReplaceItemEntry> PinnedEntry = Entry;
		FText Label;
		if (Entry == TargetNoneEntry)
		{
			Label = Entry->DisplayName;
		}
		else
		{
			Label = FText::Format(
				LOCTEXT("ReplaceItem_TargetFormat", "{0} [{1}] ({2} {2}|plural(one=instance,other=instances))"),
				Entry->DisplayName,
				FText::FromName(Entry->SlotName),
				FText::AsNumber(Entry->InstanceCount));
		}

		TargetListBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, PinnedEntry]()
				{
					if (PinnedEntry == TargetNoneEntry)
					{
						return bTargetNoneSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
					return SelectedTargetEntries.Contains(PinnedEntry) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, PinnedEntry](ECheckBoxState NewState)
				{
					if (PinnedEntry == TargetNoneEntry)
					{
						bTargetNoneSelected = (NewState == ECheckBoxState::Checked);
						if (bTargetNoneSelected)
						{
							SelectedTargetEntries.Empty();
						}
					}
					else if (NewState == ECheckBoxState::Checked)
					{
						bTargetNoneSelected = false;
						SelectedTargetEntries.Add(PinnedEntry);
					}
					else
					{
						SelectedTargetEntries.Remove(PinnedEntry);
					}
				})
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Label)
			]
		];
	}

	bool IsOKEnabled() const
	{
		// Need at least one source selected and at least one target chosen (or None)
		if (!bTargetNoneSelected && SelectedTargetEntries.Num() == 0)
		{
			return false;
		}

		for (const TSharedPtr<FReplaceItemEntry>& Entry : AllItems)
		{
			if (Entry->bSelected)
			{
				return true;
			}
		}
		return false;
	}

	FReply OnOKClicked()
	{
		bConfirmed = true;
		if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	FReply OnCancelClicked()
	{
		bConfirmed = false;
		if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	TArray<TSharedPtr<FReplaceItemEntry>> AllItems;
	TSharedPtr<FReplaceItemEntry> TargetNoneEntry;
	TArray<TSharedPtr<FReplaceItemEntry>> SelectedTargetEntries;
	bool bTargetNoneSelected = false;
	TSharedPtr<SVerticalBox> SourceListBox;
	TSharedPtr<SVerticalBox> TargetListBox;
	TWeakPtr<SWindow> ParentWindow;
	bool bFilterBySlot = true;
	bool bConfirmed = false;
};

namespace UE::MetaHuman
{

void OpenReplaceItemDialog(const TArray<UMetaHumanInstance*>& InInstances)
{
	TArray<UMetaHumanInstance*> Instances = InInstances;
	if (Instances.Num() == 0)
	{
		return;
	}

	// Check for open asset editors and warn the user
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	TArray<UMetaHumanInstance*> InstancesWithOpenEditors;
	for (UMetaHumanInstance* Instance : Instances)
	{
		if (AssetEditorSubsystem->FindEditorForAsset(Instance, /*bFocusIfOpen=*/ false) != nullptr)
		{
			InstancesWithOpenEditors.Add(Instance);
		}
	}

	if (InstancesWithOpenEditors.Num() > 0)
	{
		FString OpenEditorList;
		for (const UMetaHumanInstance* OpenInstance : InstancesWithOpenEditors)
		{
			OpenEditorList += TEXT("\n  - ") + OpenInstance->GetName();
		}

		const FText WarningMessage = FText::Format(
			LOCTEXT("ReplaceItem_OpenEditors",
				"The following {0}|plural(one=Instance has an,other=Instances have) "
				"open asset {0}|plural(one=editor,other=editors). Any unapplied edits will be lost:{1}\n\n"
				"Do you want to continue?"),
			FText::AsNumber(InstancesWithOpenEditors.Num()),
			FText::FromString(OpenEditorList));

		const EAppReturnType::Type Response = FMessageDialog::Open(
			EAppMsgCategory::Warning,
			EAppMsgType::YesNo,
			WarningMessage);

		if (Response == EAppReturnType::No)
		{
			return;
		}

		for (UMetaHumanInstance* OpenInstance : InstancesWithOpenEditors)
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(OpenInstance);
		}
	}

	// All instances must use the same Collection
	UMetaHumanCollection* SharedCollection = Instances[0]->GetMetaHumanCollection();
	if (SharedCollection == nullptr)
	{
		FMessageDialog::Open(
			EAppMsgCategory::Error,
			EAppMsgType::Ok,
			LOCTEXT("ReplaceItem_NoCollection", "The selected MetaHuman Instance does not have a Collection assigned."));
		return;
	}

	for (int32 Index = 1; Index < Instances.Num(); ++Index)
	{
		if (Instances[Index]->GetMetaHumanCollection() != SharedCollection)
		{
			FMessageDialog::Open(
				EAppMsgCategory::Error,
				EAppMsgType::Ok,
				LOCTEXT("ReplaceItem_DifferentCollections",
					"All selected MetaHuman Instances must use the same MetaHuman Collection.\n\n"
					"Please select only Instances that share a Collection and try again."));
			return;
		}
	}

	// Build the list of items from the Collection
	TArray<TSharedPtr<FReplaceItemEntry>> ItemEntries;
	for (const FMetaHumanCharacterPaletteItem& PaletteItem : SharedCollection->GetItems())
	{
		TSharedPtr<FReplaceItemEntry> Entry = MakeShared<FReplaceItemEntry>();
		Entry->ItemKey = PaletteItem.GetItemKey();
		Entry->DisplayName = PaletteItem.GetOrGenerateDisplayName();
		Entry->SlotName = PaletteItem.SlotName;
		ItemEntries.Add(Entry);
	}

	// Count how many Instances reference each item, and discover stale references
	TSet<FMetaHumanPaletteItemKey> CollectionItemKeys;
	for (const TSharedPtr<FReplaceItemEntry>& Entry : ItemEntries)
	{
		CollectionItemKeys.Add(Entry->ItemKey);
	}

	for (const UMetaHumanInstance* Instance : Instances)
	{
		// Track which items this Instance has already been counted for
		TSet<FMetaHumanPaletteItemKey> CountedKeysForInstance;

		for (const FMetaHumanPipelineSlotSelectionData& SelectionData : Instance->GetSlotSelectionData())
		{
			const FMetaHumanPaletteItemKey& SelectedItem = SelectionData.Selection.SelectedItem;

			// Only count each item once per instance
			bool bAlreadyCounted = false;
			CountedKeysForInstance.Add(SelectedItem, &bAlreadyCounted);
			if (bAlreadyCounted)
			{
				continue;
			}

			// Check if this is a stale reference (not in the Collection)
			if (!CollectionItemKeys.Contains(SelectedItem))
			{
				// See if we've already created a stale entry for this item
				TSharedPtr<FReplaceItemEntry>* ExistingStale = ItemEntries.FindByPredicate(
					[&SelectedItem](const TSharedPtr<FReplaceItemEntry>& E) { return E->ItemKey == SelectedItem; });

				if (ExistingStale)
				{
					++(*ExistingStale)->InstanceCount;
				}
				else
				{
					TSharedPtr<FReplaceItemEntry> StaleEntry = MakeShared<FReplaceItemEntry>();
					StaleEntry->ItemKey = SelectedItem;
					StaleEntry->DisplayName = FText::FromString(SelectedItem.ToDebugString());
					StaleEntry->SlotName = SelectionData.Selection.SlotName;
					StaleEntry->InstanceCount = 1;
					StaleEntry->bStale = true;
					ItemEntries.Add(StaleEntry);
				}
				continue;
			}

			// Count references for Collection items
			TSharedPtr<FReplaceItemEntry>* MatchingEntry = ItemEntries.FindByPredicate(
				[&SelectedItem](const TSharedPtr<FReplaceItemEntry>& E) { return E->ItemKey == SelectedItem; });
			if (MatchingEntry)
			{
				++(*MatchingEntry)->InstanceCount;
			}
		}
	}

	// Sort items by slot name then display name
	ItemEntries.Sort([](const TSharedPtr<FReplaceItemEntry>& A, const TSharedPtr<FReplaceItemEntry>& B)
	{
		const int32 SlotCompare = A->SlotName.Compare(B->SlotName);
		if (SlotCompare != 0)
		{
			return SlotCompare < 0;
		}
		return A->DisplayName.CompareTo(B->DisplayName) < 0;
	});

	if (ItemEntries.Num() == 0)
	{
		FMessageDialog::Open(
			EAppMsgCategory::Error,
			EAppMsgType::Ok,
			LOCTEXT("ReplaceItem_NoItems", "The Collection does not contain any items."));
		return;
	}

	// Show the dialog
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("ReplaceItem_DialogTitle", "Replace Item"))
		.HasCloseButton(true)
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(500.0f, 600.0f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedRef<SReplaceItemDialog> DialogContent = SNew(SReplaceItemDialog, DialogWindow)
		.Items(ItemEntries);

	DialogWindow->SetContent(DialogContent);

	FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelRegularWindow());

	if (!DialogContent->WasConfirmed())
	{
		return;
	}

	const TArray<FMetaHumanPaletteItemKey> SourceKeys = DialogContent->GetSelectedSourceItems();
	const TArray<FMetaHumanPaletteItemKey> TargetKeys = DialogContent->GetSelectedTargetItems();
	const bool bRemoveOnly = DialogContent->IsTargetNoneSelected();

	if (SourceKeys.Num() == 0)
	{
		return;
	}

	// When distributing across multiple targets, build a shuffled assignment list.
	// Collect the indices of all instances that have a matching source selection,
	// shuffle them, then assign targets round-robin.
	TArray<int32> AffectedInstanceIndices;
	if (!bRemoveOnly && TargetKeys.Num() > 1)
	{
		for (int32 Idx = 0; Idx < Instances.Num(); ++Idx)
		{
			for (const FMetaHumanPipelineSlotSelectionData& SelectionData : Instances[Idx]->GetSlotSelectionData())
			{
				if (SourceKeys.Contains(SelectionData.Selection.SelectedItem))
				{
					AffectedInstanceIndices.Add(Idx);
					break;
				}
			}
		}

		// Shuffle for random distribution
		const int32 LastIndex = AffectedInstanceIndices.Num() - 1;
		for (int32 i = LastIndex; i > 0; --i)
		{
			const int32 j = FMath::RandRange(0, i);
			AffectedInstanceIndices.Swap(i, j);
		}
	}

	// Get the pipeline and its specification to check single-select constraints
	const UMetaHumanCollectionPipeline* Pipeline = SharedCollection->GetPipeline();
	const UMetaHumanCharacterPipelineSpecification* PipelineSpec = nullptr;
	if (Pipeline != nullptr)
	{
		PipelineSpec = Pipeline->GetSpecification();
	}
	int32 SuccessCount = 0;
	TArray<FString> FailedInstances;
	bool bAskedPipelineOverride = false;
	bool bAllowPipelineOverride = false;

	for (int32 InstanceIdx = 0; InstanceIdx < Instances.Num(); ++InstanceIdx)
	{
		UMetaHumanInstance* Instance = Instances[InstanceIdx];

		// Determine which target key to use for this instance
		FMetaHumanPaletteItemKey TargetKeyForInstance;
		if (!bRemoveOnly)
		{
			if (TargetKeys.Num() == 1)
			{
				TargetKeyForInstance = TargetKeys[0];
			}
			else if (TargetKeys.Num() > 1)
			{
				// Round-robin assignment based on shuffled order
				const int32 ShuffledPosition = AffectedInstanceIndices.IndexOfByKey(InstanceIdx);
				if (ShuffledPosition != INDEX_NONE)
				{
					TargetKeyForInstance = TargetKeys[ShuffledPosition % TargetKeys.Num()];
				}
			}
		}

		// Copy the current selections since we'll be modifying the instance
		const TArray<FMetaHumanPipelineSlotSelectionData> CurrentSelections = Instance->GetSlotSelectionData();
		TArray<FMetaHumanPipelineSlotSelection> NewSelections;
		bool bChanged = false;

		for (const FMetaHumanPipelineSlotSelectionData& SelectionData : CurrentSelections)
		{
			const FMetaHumanPipelineSlotSelection& Selection = SelectionData.Selection;

			if (SourceKeys.Contains(Selection.SelectedItem))
			{
				bChanged = true;

				// If target is null (None/remove), we simply omit this selection to remove it
				if (!bRemoveOnly && !TargetKeyForInstance.IsNull())
				{
					FMetaHumanPipelineSlotSelection ReplacedSelection;
					ReplacedSelection.ParentItemPath = Selection.ParentItemPath;
					ReplacedSelection.SlotName = Selection.SlotName;
					ReplacedSelection.SelectedItem = TargetKeyForInstance;
					NewSelections.Add(ReplacedSelection);
				}
			}
			else
			{
				NewSelections.Add(Selection);
			}
		}

		if (!bChanged)
		{
			// No matching selections found -- nothing to do for this Instance
			continue;
		}

		// Check for single-select slot violations: if replacing would cause a
		// single-select slot to have multiple different selections, reject it.
		if (PipelineSpec != nullptr)
		{
			bool bHasDuplicateSlot = false;
			TMap<FName, int32> SlotSelectionCounts;
			for (const FMetaHumanPipelineSlotSelection& Sel : NewSelections)
			{
				int32& Count = SlotSelectionCounts.FindOrAdd(Sel.SlotName, 0);
				++Count;

				if (Count > 1)
				{
					const FMetaHumanCharacterPipelineSlot* SlotSpec = PipelineSpec->Slots.Find(Sel.SlotName);
					if (SlotSpec != nullptr && !SlotSpec->bAllowsMultipleSelection)
					{
						FailedInstances.Add(FString::Printf(TEXT("%s: slot '%s' does not allow multiple selections"),
							*Instance->GetName(), *Sel.SlotName.ToString()));
						bHasDuplicateSlot = true;
						break;
					}
				}
			}

			if (bHasDuplicateSlot)
			{
				continue;
			}
		}

		// Validate the new selections with the pipeline
		if (Pipeline != nullptr)
		{
			FText DisallowedReason;
			if (!Pipeline->AreSlotSelectionsAllowed(SharedCollection, NewSelections, DisallowedReason))
			{
				if (!bAskedPipelineOverride)
				{
					bAskedPipelineOverride = true;

					const FText ConfirmMessage = FText::Format(
						LOCTEXT("ReplaceItem_PipelineWarning",
							"{0}: {1}\n\n"
							"The MetaHuman Instance will not assemble correctly if it contains "
							"selections that are not allowed by the pipeline.\n\n"
							"Do you want to apply this replacement anyway?"),
						FText::FromString(Instance->GetName()),
						DisallowedReason);

					const EAppReturnType::Type ConfirmResponse = FMessageDialog::Open(
						EAppMsgCategory::Warning,
						EAppMsgType::YesNo,
						ConfirmMessage);

					bAllowPipelineOverride = (ConfirmResponse == EAppReturnType::Yes);
				}

				if (!bAllowPipelineOverride)
				{
					FailedInstances.Add(FString::Printf(TEXT("%s: %s"), *Instance->GetName(), *DisallowedReason.ToString()));
					continue;
				}
			}
		}

		// Apply the new selections: clear all existing selections and re-add
		// We need to remove old selections and add new ones through the public API
		// First remove all current selections
		for (const FMetaHumanPipelineSlotSelectionData& SelectionData : CurrentSelections)
		{
			Instance->TryRemoveSlotSelection(SelectionData.Selection);
		}

		// Then add the new selections
		bool bAddFailed = false;
		for (const FMetaHumanPipelineSlotSelection& NewSelection : NewSelections)
		{
			if (!Instance->TryAddSlotSelection(NewSelection))
			{
				bAddFailed = true;
				FailedInstances.Add(FString::Printf(TEXT("%s: failed to add slot selection for slot '%s'"),
					*Instance->GetName(), *NewSelection.SlotName.ToString()));
				break;
			}
		}

		if (bAddFailed)
		{
			// Rollback: restore the original selections to avoid leaving the
			// instance in a partially-mutated state.
			const TArray<FMetaHumanPipelineSlotSelectionData> SuccessfulNewSelections = Instance->GetSlotSelectionData();
			for (const FMetaHumanPipelineSlotSelectionData& PartialData : SuccessfulNewSelections)
			{
				Instance->TryRemoveSlotSelection(PartialData.Selection);
			}

			for (const FMetaHumanPipelineSlotSelectionData& OriginalData : CurrentSelections)
			{
				// This is a best effort to recover from the failure.
				//
				// It shouldn't fail, and if it does there's nothing more we can do.
				ensure(Instance->TryAddSlotSelection(OriginalData.Selection));
			}

			continue;
		}

		Instance->MarkPackageDirty();
		Instance->NotifyAssemblyOutputInvalidated();
		++SuccessCount;
	}

	// Show results dialog
	FText ResultMessage;
	if (FailedInstances.Num() == 0)
	{
		ResultMessage = FText::Format(
			LOCTEXT("ReplaceItem_AllSuccess", "Successfully updated {0} {0}|plural(one=Instance,other=Instances)."),
			FText::AsNumber(SuccessCount));
	}
	else
	{
		FString FailedList;
		for (const FString& FailedEntry : FailedInstances)
		{
			FailedList += TEXT("\n  - ") + FailedEntry;
		}

		ResultMessage = FText::Format(
			LOCTEXT("ReplaceItem_PartialSuccess",
				"Successfully updated {0} {0}|plural(one=Instance,other=Instances).\n\n"
				"{1} {1}|plural(one=Instance,other=Instances) could not be updated because the pipeline "
				"does not allow the resulting slot selections:{2}"),
			FText::AsNumber(SuccessCount),
			FText::AsNumber(FailedInstances.Num()),
			FText::FromString(FailedList));
	}

	FMessageDialog::Open(EAppMsgCategory::Info, EAppMsgType::Ok, ResultMessage);
}

} // namespace UE::MetaHuman

#undef LOCTEXT_NAMESPACE
