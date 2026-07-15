// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCollection.h"

#include "MetaHumanInstance.h"
#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "MetaHumanCharacterPipelineSpecification.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanItemPipeline.h"
#include "MetaHumanUnpackUtilities.h"
#include "MetaHumanWardrobeItem.h"

#include "ComponentReregisterContext.h"
#include "Logging/StructuredLog.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

bool FMetaHumanCollectionBuiltData::IsValid() const
{
	return PaletteBuiltData.ItemBuiltData.View().SortedElements.Num() > 0;
}

UMetaHumanCollection::UMetaHumanCollection()
{
#if WITH_EDITORONLY_DATA
	DefaultInstance = CreateDefaultSubobject<UMetaHumanInstance>(TEXT("DefaultInstance"));
	// Allow the Default Instance to be referenced from other packages, such as actors in a level
	DefaultInstance->SetFlags(RF_Public);
	DefaultInstance->SetMetaHumanCollection(this);
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UMetaHumanCollection::SetQuality(EMetaHumanCharacterPaletteBuildQuality InQuality)
{
	if (Quality == InQuality)
	{
		// Early-out to avoid clearing built data unnecessarily
		return;
	}

	ClearBuiltData();
	Quality = InQuality;
}

void UMetaHumanCollection::Build(
	const FInstancedStruct& BuildInput,
	const FOnBuildComplete& OnComplete,
	const TArray<FMetaHumanPinnedSlotSelection>& PinnedSlotSelections,
	const TArray<FMetaHumanPaletteItemPath>& ItemsToExclude)
{
	if (!Pipeline
		|| !Pipeline->GetEditorPipeline())
	{
		OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
		return;
	}

	// Give the pipeline a chance to validate and fix up the Collection before building.
	{
		UMetaHumanCollectionEditorPipeline* MutableEditorPipeline = Pipeline->GetMutableEditorPipeline();
		if (!MutableEditorPipeline
			|| !MutableEditorPipeline->ValidateCollection(this))
		{
			OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
			return;
		}
	}

	TArray<FMetaHumanPaletteItemPath> LocalItemsToExclude;
	LocalItemsToExclude.Reserve(ItemsToExclude.Num());

	// Any invalid pinned slot selections detected below will be treated as a build failure, 
	// because they could have significant downstream effects that are hard to detect later, e.g. a
	// large amount of content being unintentionally built.
	{
		for (int32 Index = 0; Index < PinnedSlotSelections.Num(); Index++)
		{
			const FMetaHumanPinnedSlotSelection& PinnedSelection = PinnedSlotSelections[Index];
			if (PinnedSelection.Selection.SlotName == NAME_None)
			{
				OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
				return;
			}

			// Find out if this pinned slot has already been processed
			{
				bool bAlreadyProcessed = false;
				for (int32 CompareIndex = Index - 1; CompareIndex >= 0; CompareIndex--)
				{
					const FMetaHumanPinnedSlotSelection& CompareSelection = PinnedSlotSelections[CompareIndex];
					if (CompareSelection.Selection.ParentItemPath == PinnedSelection.Selection.ParentItemPath
						&& CompareSelection.Selection.SlotName == PinnedSelection.Selection.SlotName)
					{
						bAlreadyProcessed = true;
						break;
					}
				}

				if (bAlreadyProcessed)
				{
					continue;
				}
			}

			const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
			{
				if (PinnedSelection.Selection.ParentItemPath.IsEmpty())
				{
					ContainingPalette = this;
				}
				else
				{
					// TODO: Support nested items. These are currently not possible to create, but we want to support them in future.
					OnComplete.ExecuteIfBound(EMetaHumanBuildStatus::Failed);
					return;
				}
			}

			for (const FMetaHumanCharacterPaletteItem& Item : ContainingPalette->GetItems())
			{
				if (Item.SlotName != PinnedSelection.Selection.SlotName)
				{
					continue;
				}

				const FMetaHumanPaletteItemKey ItemKey = Item.GetItemKey();
				
				if (!PinnedSlotSelections.ContainsByPredicate(
					[&ItemKey, &PinnedSelection](const FMetaHumanPinnedSlotSelection& OtherPinnedSelection)
					{
						return OtherPinnedSelection.Selection.ParentItemPath == PinnedSelection.Selection.ParentItemPath
							&& OtherPinnedSelection.Selection.SlotName == PinnedSelection.Selection.SlotName
							&& OtherPinnedSelection.Selection.SelectedItem == ItemKey;
					}))
				{
					// This item is in the same slot as the pinned item, but is not itself pinned

					// Since each pinned slot is only processed once and each item can only be in one slot,
					// there should be no duplicates in this list.
					LocalItemsToExclude.Emplace(PinnedSelection.Selection.ParentItemPath, ItemKey);
				}
			}
		}
	}

	if (LocalItemsToExclude.Num() > 0)
	{
		for (const FMetaHumanPaletteItemPath& Item : ItemsToExclude)
		{
			LocalItemsToExclude.AddUnique(Item);
		}
	}
	else
	{
		LocalItemsToExclude = ItemsToExclude;
	}

	LocalItemsToExclude.Sort();

	TArray<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections = PinnedSlotSelections;
	SortedPinnedSlotSelections.Sort([](const FMetaHumanPinnedSlotSelection& A, const FMetaHumanPinnedSlotSelection& B) { return A.Selection < B.Selection; });

	TOptional<EMetaHumanBuildStatus> BuildStatus;

	const UMetaHumanCollectionEditorPipeline::FBuildCollectionParams Params
	{
		.Collection = this,
		.OuterForGeneratedAssets = this,
		.SortedPinnedSlotSelections = SortedPinnedSlotSelections,
		.SortedItemsToExclude = LocalItemsToExclude,
		.BuildInput = BuildInput,
	};

	const UMetaHumanCollectionEditorPipeline::FOnBuildComplete OnBuildCollectionComplete = UMetaHumanCollectionEditorPipeline::FOnBuildComplete::CreateWeakLambda(
		this, 
		[this, OnComplete, SortedPinnedSlotSelections, &BuildStatus](EMetaHumanBuildStatus Status, TSharedPtr<FMetaHumanCollectionBuiltData> InBuiltData)
		{
			if (InBuiltData.IsValid())
			{
				// Note that SortedPinnedSlotSelections may reference UObjects, but is not 
				// visible to the GC while stored in the lambda capture. This will need to be 
				// addressed when we make building properly async.
				InBuiltData->SortedPinnedSlotSelections = SortedPinnedSlotSelections;

				BuiltData = MoveTemp(*InBuiltData);
				bIsUnpacked = false;

				// Clear out any old built data objects by marking them transient
				UE::MetaHuman::UnpackUtilities::MarkUnreferencedSubobjectsAsTransient(this);

				MarkPackageDirty();
			}

			OnComplete.ExecuteIfBound(Status);

			if (Status == EMetaHumanBuildStatus::Succeeded)
			{
				// Notify any in-memory MetaHuman Instances that reference this Collection that
				// their assembly output is now stale.
				//
				// Done before broadcasting delegates so that dependent Instances are in a safe
				// state before the delegate listeners have the chance to look at them.
				for (TObjectIterator<UMetaHumanInstance> It; It; ++It)
				{
					UMetaHumanInstance* const Instance = *It;
					if (!Instance
						|| Instance->HasAnyFlags(RF_ClassDefaultObject)
						|| Instance->GetMetaHumanCollection() != this)
					{
						continue;
					}

					Instance->ClearAssemblyOutput();
					Instance->NotifyAssemblyOutputInvalidated();
				}

				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				OnPaletteBuilt.Broadcast(Quality);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS

				OnCollectionBuilt.Broadcast();
			}

			BuildStatus = Status;
		});

	Pipeline->GetEditorPipeline()->BuildCollection(Params, OnBuildCollectionComplete);

	// For now, we rely on BuildCollection completing synchronously.
	//
	// In future, there will be proper support for async builds and a synchronisation mechanism to
	// handle functions such as SetQuality being called during a build.
	check(BuildStatus.IsSet());
}

void UMetaHumanCollection::Build(
	const FInstancedStruct& BuildInput,
	EMetaHumanCharacterPaletteBuildQuality InQuality,
	ITargetPlatform* TargetPlatform,
	const FOnBuildComplete& OnComplete,
	const TArray<FMetaHumanPinnedSlotSelection>& PinnedSlotSelections,
	const TArray<FMetaHumanPaletteItemPath>& ItemsToExclude)
{
	Build(BuildInput, OnComplete, PinnedSlotSelections, ItemsToExclude);
}

void UMetaHumanCollection::ClearBuiltData()
{
	BuiltData = FMetaHumanCollectionBuiltData();
	bIsUnpacked = false;
}

void UMetaHumanCollection::RefreshBuildCacheGuid()
{
	BuildCacheGuid = FGuid::NewGuid();
}

void UMetaHumanCollection::UnpackAssets(const FOnMetaHumanCharacterAssetsUnpacked& OnComplete)
{
	if (bIsUnpacked)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to unpack assets from Collection {Collection}: Already unpacked", GetPathName());

		OnComplete.ExecuteIfBound(EMetaHumanCharacterAssetsUnpackResult::Failed);
		return;
	}

	if (!Pipeline
		|| !Pipeline->GetEditorPipeline())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to unpack assets from Collection {Collection}: No pipeline is set", GetPathName());

		OnComplete.ExecuteIfBound(EMetaHumanCharacterAssetsUnpackResult::Failed);
		return;
	}

	// Unpacking assets causes a lot of calls to LoadPackage to look for existing packages on disk.
	//
	// LoadPackage includes a FGlobalComponentReregisterContext, which is an expensive operation, 
	// so to avoid repeatedly hitting this during unpacking, we do it once here and the inner 
	// FGlobalComponentReregisterContexts will be no-ops.
	FGlobalComponentReregisterContext ComponentReregisterContext;

	Pipeline->GetEditorPipeline()->UnpackCollectionAssets(this, BuiltData, UMetaHumanCharacterEditorPipeline::FOnUnpackComplete::CreateWeakLambda(
		this,
		[OnComplete, this](EMetaHumanBuildStatus Result)
		{
			if (Result == EMetaHumanBuildStatus::Failed)
			{
				UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to unpack assets from Collection {Collection}: Pipeline reported failure", GetPathName());

				OnComplete.ExecuteIfBound(EMetaHumanCharacterAssetsUnpackResult::Failed);
				return;
			}

			bIsUnpacked = true;
			MarkPackageDirty();

			OnComplete.ExecuteIfBound(EMetaHumanCharacterAssetsUnpackResult::Succeeded);
		}));
}

void UMetaHumanCollection::SetDefaultPipeline()
{
	// If this is a blueprint class, the project code should load it at startup to avoid a hitch here.
	TSubclassOf<UMetaHumanCollectionPipeline> PipelineClass = GetDefault<UMetaHumanCharacterPaletteProjectSettings>()->DefaultCharacterPipelineClass.LoadSynchronous();

	if (PipelineClass)
	{
		SetPipeline(NewObject<UMetaHumanCollectionPipeline>(this, PipelineClass));
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error,
			"Failed to load DefaultCharacterPipelineClass: {DefaultClass}", 
			GetDefault<UMetaHumanCharacterPaletteProjectSettings>()->DefaultCharacterPipelineClass.ToString());
	}
}

void UMetaHumanCollection::SetPipeline(UMetaHumanCollectionPipeline* InPipeline)
{		
	Pipeline = InPipeline;

	// It's not always possible for a pipeline to initialize its own editor pipeline when it's 
	// constructed, e.g. if it's in an editor module that the runtime pipeline can't depend on,
	// so we create a default editor pipeline here if one isn't already set.
	//
	// We could require callers to do this instead, but that is more error prone and doesn't have
	// any benefits other than being conceptually more correct.
	if (Pipeline && !Pipeline->GetEditorPipeline())
	{
		Pipeline->SetDefaultEditorPipeline();
	}

	// TODO: Delete any items belonging to slots that don't exist on the new pipeline

	OnPipelineChanged.Broadcast();
}

void UMetaHumanCollection::SetPipelineFromClass(TSubclassOf<UMetaHumanCollectionPipeline> InPipelineClass)
{
	if (InPipelineClass)
	{
		SetPipeline(NewObject<UMetaHumanCollectionPipeline>(this, InPipelineClass));
	}
}

const UMetaHumanCollectionEditorPipeline* UMetaHumanCollection::GetEditorPipeline() const
{
	return Pipeline ? Pipeline->GetEditorPipeline() : nullptr;
}

const UMetaHumanCharacterEditorPipeline* UMetaHumanCollection::GetPaletteEditorPipeline() const
{
	return GetEditorPipeline();
}

bool UMetaHumanCollection::TryRemoveItem(const FMetaHumanPaletteItemKey& ExistingKey)
{
	if (!Super::TryRemoveItem(ExistingKey))
	{
		return false;
	}

	// Remove any slot selections that rely on this item from the Default Instance.
	//
	// Note that there could still be other Instances that will now have "dangling" selections of 
	// this item. These will still need to be handled gracefully, but at least the Default Instance
	// will be in a clean state.

	const FMetaHumanPaletteItemPath ExistingItemPath(ExistingKey);

	TArray<FMetaHumanPipelineSlotSelection> SelectionsToRemove;
	for (const FMetaHumanPipelineSlotSelectionData& SelectionData : DefaultInstance->GetSlotSelectionData())
	{
		if (SelectionData.Selection.GetSelectedItemPath().IsEqualOrChildPathOf(ExistingItemPath))
		{
			SelectionsToRemove.Add(SelectionData.Selection);
		}
	}

	for (const FMetaHumanPipelineSlotSelection& Selection : SelectionsToRemove)
	{
		DefaultInstance->TryRemoveSlotSelection(Selection);
	}

	return true;
}

bool UMetaHumanCollection::TryImportWardrobeItem(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> SourceWardrobeItem, FMetaHumanPaletteItemKey& OutNewItemKey)
{
	UMetaHumanCollectionEditorPipeline* EditorPipeline = Pipeline ? Pipeline->GetMutableEditorPipeline() : nullptr;
	if (!EditorPipeline)
	{
		return false;
	}

	FMetaHumanCharacterPaletteItem NewItem;
	if (!EditorPipeline->TryCreateItemForImport(this, SlotName, SourceWardrobeItem, NewItem))
	{
		return false;
	}

	if (!NewItem.WardrobeItem)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "TryImportWardrobeItem: TryCreateItemForImport returned an item with no WardrobeItem");
		return false;
	}

	if (NewItem.WardrobeItem->IsExternal())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error,
			"TryImportWardrobeItem: TryCreateItemForImport returned an external WardrobeItem. Imported items must be internal, i.e. have the Collection as their Outer.");
		return false;
	}

	NewItem.Variation = GenerateUniqueVariationName(NewItem.GetItemKey());
	NewItem.DisplayName = NewItem.GetOrGenerateDisplayName();

	if (!TryAddItem(NewItem))
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "TryImportWardrobeItem: Failed to add imported item to Collection");
		return false;
	}

	OutNewItemKey = NewItem.GetItemKey();
	return true;
}

void UMetaHumanCollection::CopyContentsFrom(TNotNull<const UMetaHumanCollection*> Other)
{
	ClearBuiltData();

	// Save this collection into the transaction buffer after clearing built data, as it may be large
	Modify();

	CopyContentsFromPalette(Other);

	// Pipeline shouldn't be nullptr for collections that are in use, but handle it just in case
	if (Other->GetPipeline())
	{
		Pipeline = DuplicateObject<UMetaHumanCollectionPipeline>(Other->GetPipeline(), this);
	}
	else
	{
		Pipeline = nullptr;
	}

	DefaultInstance->CopyContentsFrom(Other->DefaultInstance);
	DefaultInstance->SetMetaHumanCollection(this);

	BuildCacheGuid = Other->BuildCacheGuid;
}

UMetaHumanCollectionPipeline* UMetaHumanCollection::GetMutablePipeline()
{
	return Pipeline;
}

#endif // WITH_EDITOR
	
const UMetaHumanCollectionPipeline* UMetaHumanCollection::GetPipeline() const
{
	return Pipeline;
}

const UMetaHumanCharacterPipeline* UMetaHumanCollection::GetPalettePipeline() const
{
	return GetPipeline();
}

const FMetaHumanCollectionBuiltData& UMetaHumanCollection::GetBuiltData() const
{
	return BuiltData;
}

const FMetaHumanCollectionBuiltData& UMetaHumanCollection::GetBuiltData(EMetaHumanCharacterPaletteBuildQuality InQuality) const
{
	return BuiltData;
}

EMetaHumanCharacterPaletteBuildQuality UMetaHumanCollection::GetQuality() const
{
	return Quality;
}

#if WITH_EDITOR

TNotNull<const UMetaHumanInstance*> UMetaHumanCollection::GetDefaultInstance() const
{
	return DefaultInstance;
}

TArray<UClass*> UMetaHumanCollection::GetDisallowedPipelineClasses() const
{
	static const FName MetaHumanCreatorOnlyTag(TEXT("MetaHumanCreatorOnly"));

	TArray<UClass*> Disallowed;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (Class->IsChildOf(UMetaHumanCollectionPipeline::StaticClass())
			&& Class->HasMetaData(MetaHumanCreatorOnlyTag))
		{
			Disallowed.Add(Class);
		}
	}

	return Disallowed;
}

TNotNull<UMetaHumanInstance*> UMetaHumanCollection::GetMutableDefaultInstance()
{
	return DefaultInstance;
}

void UMetaHumanCollection::PostInitProperties()
{
	Super::PostInitProperties();

	if (!IsTemplate())
	{
		BuildCacheGuid = FGuid::NewGuid();
	}
}

void UMetaHumanCollection::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	if (Quality == EMetaHumanCharacterPaletteBuildQuality::Preview)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "MetaHuman Collection {Name} is set to Preview quality. Preview quality Collections should not be saved or cooked.",
			GetPathName());

		// Clear out the Preview built data so that it doesn't get saved if the save goes ahead
		ClearBuiltData();
	}
	
	Super::PreSave(ObjectSaveContext);
}

void UMetaHumanCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCollection, Pipeline))
	{
		SetPipeline(Pipeline);
	}
}

#endif // WITH_EDITOR

TArray<FMetaHumanPipelineSlotSelectionData> UMetaHumanCollection::PropagateVirtualSlotSelections(const TArray<FMetaHumanPipelineSlotSelectionData>& Selections) const
{
	TArray<FMetaHumanPipelineSlotSelectionData> Result;
	Result.Reserve(Selections.Num());

	for (const FMetaHumanPipelineSlotSelectionData& SelectionData : Selections)
	{
		const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
		FMetaHumanCharacterPaletteItem Item;
		if (!TryResolveItem(SelectionData.Selection.GetSelectedItemPath(), ContainingPalette, Item))
		{
			// This selection will be dropped from the result and only the valid selections will be returned
			continue;
		}

		if (!Item.WardrobeItem
			|| Item.WardrobeItem->PrincipalAsset.IsNull())
		{
			// Drop the selection if the item isn't valid
			continue;
		}

		const UMetaHumanCharacterPipeline* ParentPipeline = ContainingPalette->GetPalettePipeline();
		TNotNull<const UMetaHumanCharacterPipelineSpecification*> PipelineSpec = ParentPipeline->GetSpecification();

		TOptional<FName> ResolvedSlotName = PipelineSpec->ResolveRealSlotName(SelectionData.Selection.SlotName);
		if (!ResolvedSlotName.IsSet())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to resolve virtual slot {VirtualSlot} to a real slot on specification {PipelineSpec}",
				SelectionData.Selection.SlotName.ToString(), PipelineSpec->GetPathName());

			continue;
		}

		FMetaHumanPipelineSlotSelectionData& NewSelection = Result.Add_GetRef(SelectionData);
		NewSelection.Selection.SlotName = ResolvedSlotName.GetValue();
	}

	return Result;
}

#if WITH_EDITORONLY_DATA

FString UMetaHumanCollection::GetUnpackFolder() const
{
	switch (UnpackPathMode)
	{
		case EMetaHumanCharacterUnpackPathMode::SubfolderNamedForPalette:
		{
			return GetPackage()->GetName();
		}
		case EMetaHumanCharacterUnpackPathMode::Relative:
		{
			FString UnpackFolder = FPackageName::GetLongPackagePath(GetPackage()->GetName());

			if (UnpackFolderPath.Len() > 0)
			{
				UnpackFolder /= UnpackFolderPath;
			}

			return UnpackFolder;
		}
		case EMetaHumanCharacterUnpackPathMode::Absolute:
		{
			return UnpackFolderPath;
		}
		default:
		{
			checkNoEntry();
			return FString(TEXT(""));
		}
	}
}

#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
void UMetaHumanCollection::OnItemsModified()
{
	// The built data is invalid as soon as any items are modified on the Collection
	ClearBuiltData();
}

#endif // WITH_EDITOR
