// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanInstance.h"

#include "MetaHumanCharacterActorInterface.h"
#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanPinnedSlotSelection.h"
#include "MetaHumanUnpackUtilities.h"

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Engine/World.h"
#include "Logging/StructuredLog.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EngineUtils.h"
#endif

void UMetaHumanInstance::Assemble(
	const FMetaHumanCharacterAssembled& OnAssembled,
	const FMetaHumanCharacterAssembledNative& OnAssembledNative)
{
	if (!CanModifyInstance(TEXT("Assemble")))
	{
		OnAssembled.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		OnAssembledNative.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		return;
	}

	if (!Collection
		|| !Collection->GetPipeline())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to assemble Instance {Instance}, as it doesn't reference a Collection with a valid pipeline", GetPathName());

		OnAssembled.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		OnAssembledNative.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		return;
	}

	if (!Collection->GetBuiltData().IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to assemble Instance {Instance}, as the referenced Collection {Collection} has not been built", 
			GetPathName(), Collection->GetPathName());

		OnAssembled.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		OnAssembledNative.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		return;
	}

	// References to missing items are not removed here, because during editing items may be 
	// temporarily removed from a Collection and we don't want to permanently remove them from the 
	// Instance when it's assembled.
	//
	// Note that the call to PropagateVirtualSlotSelections below ensures that references to 
	// missing items don't reach the pipeline, so pipelines still don't have to deal with slot 
	// selections referencing missing items.
	ValidateAndSanitizeSlotSelections(/*bShouldRemoveMissingItemSelections*/ false);

	// All selections are propagated to real slots, so the pipeline doesn't have to deal with any
	// virtual slots.
	const TArray<FMetaHumanPipelineSlotSelectionData> RealSlotSelectionData = Collection->PropagateVirtualSlotSelections(SlotSelections);

	// Copy the selections from RealSlotSelectionData into RealSlotSelections
	TArray<FMetaHumanPipelineSlotSelection> RealSlotSelections;
	{
		RealSlotSelections.Reserve(RealSlotSelectionData.Num());
		Algo::Transform(RealSlotSelectionData, RealSlotSelections, [](const FMetaHumanPipelineSlotSelectionData& Data) { return Data.Selection; });
	}

	FText DisallowedReason;
	if (!Collection->GetPipeline()->AreSlotSelectionsAllowed(
		Collection,
		RealSlotSelections,
		DisallowedReason))
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "Failed to assemble Instance {Instance} as its slot selections are not allowed by the pipeline. Reason: {Reason}",
			GetPathName(), DisallowedReason.ToString());

		OnAssembled.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		OnAssembledNative.ExecuteIfBound(EMetaHumanCharacterAssemblyResult::Failed);
		return;
	}

	// Clear any previous assembly output so the existing subobjects under this Instance are
	// renamed out to the transient package. This allows the assembly process to generate 
	// new objects with the same names as the old ones, keeping the names deterministic.
	ClearAssemblyOutput();

	// Not used yet
	const FInstancedStruct AssemblyInput;

	UObject* OuterForGeneratedObjects = this;

	// Construct Assembly Parameters using any overrides on the Instance
	{
		const FMetaHumanCollectionBuiltData& BuiltData = Collection->GetBuiltData();
		
		CurrentOverriddenAssemblyParameters.Edit().Empty(OverriddenInstanceParameters.Num());

		for (const TPair<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& OverriddenItemPair : OverriddenInstanceParameters)
		{
			const FMetaHumanPipelineBuiltData* ItemBuiltData = BuiltData.PaletteBuiltData.ItemBuiltData.View().Find(OverriddenItemPair.Key);

			if (!ItemBuiltData)
			{
				// This item doesn't have any parameters, so none of the overridden parameters for it can be used
				continue;
			}

			// Skip any empty keys in case assets have been force-deleted, leaving empty keys behind
			if (OverriddenItemPair.Key.IsEmpty())
			{
				continue;
			}

			if (OverriddenItemPair.Value.GetPropertyBagStruct() == ItemBuiltData->AssemblyParameters.GetPropertyBagStruct())
			{
				// The override struct is identical to the original struct, so use it directly
				CurrentOverriddenAssemblyParameters.Edit().Add(OverriddenItemPair.Key, OverriddenItemPair.Value);
			}
			else
			{
				// Copy the original parameters first, and then any overrides with matching names.
				//
				// Only properties on the override struct that exist in the original struct will be
				// used.
				//
				// Importantly, this also means we're using the type information from the original, 
				// so an original property that is overridden with a property of the same name but 
				// a different type will be passed into the pipeline with the original type.

				FInstancedPropertyBag& CurrentParams = CurrentOverriddenAssemblyParameters.Edit().Add(OverriddenItemPair.Key, ItemBuiltData->AssemblyParameters);

				CurrentParams.CopyMatchingValuesByName(OverriddenItemPair.Value);
			}
		}

		// Add overrides for the Collection's own Instance Parameters
		if (OverriddenCollectionInstanceParameters.IsValid())
		{
			CurrentOverriddenAssemblyParameters.Edit().Add(FMetaHumanPaletteItemPath::Collection, OverriddenCollectionInstanceParameters);
		}
	}
	
	const UMetaHumanCollectionPipeline::FAssembleCollectionParams Params
	{
		.Collection = Collection,
		.OuterForGeneratedObjects = OuterForGeneratedObjects,
		.SlotSelections = MakeConstArrayView(RealSlotSelectionData),
		.AssemblyParameters = CurrentOverriddenAssemblyParameters.View(),
	};

	TOptional<EMetaHumanCharacterAssemblyResult> AssemblyResult;

	const UMetaHumanCollectionPipeline::FOnAssemblyComplete OnAssemblyComplete = UMetaHumanCollectionPipeline::FOnAssemblyComplete::CreateWeakLambda(
		this,
		[this, OnAssembled, OnAssembledNative, &AssemblyResult](FMetaHumanAssemblyOutput&& NewAssemblyOutput)
		{
			const EMetaHumanCharacterAssemblyResult Status = NewAssemblyOutput.PipelineAssemblyOutput.IsValid()
				? EMetaHumanCharacterAssemblyResult::Succeeded
				: EMetaHumanCharacterAssemblyResult::Failed;

			AssemblyResult = Status;

			if (Status == EMetaHumanCharacterAssemblyResult::Succeeded)
			{
				FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();

				ActiveOutput.AssemblyOutput = MoveTemp(NewAssemblyOutput.PipelineAssemblyOutput);
				ActiveOutput.PostAssemblyParametersFromPipeline = MoveTemp(NewAssemblyOutput.PostAssemblyParameters);

#if WITH_EDITORONLY_DATA
				ActiveOutput.AssemblyAssetMetadata = MoveTemp(NewAssemblyOutput.Metadata);
#endif

				// Instance Parameters are deprecated. This code will be removed.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
				ActiveOutput.ItemsUsingDeprecatedInstanceParameters.Empty(NewAssemblyOutput.InstanceParameters.Num());

				// In order to keep the parameter context encapsulated, we have to split the map.
				//
				// It's not ideal, but necessary to prevent the parameter context from becoming an
				// unwanted side channel that will reduce the flexibility we have in future.
				for (TPair<FMetaHumanPaletteItemPath, FMetaHumanInstanceParameterOutput>& Pair : NewAssemblyOutput.InstanceParameters)
				{
					if (ActiveOutput.PostAssemblyParametersFromPipeline.View().IndexOf(Pair.Key) != INDEX_NONE)
					{
						UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
							"While assembling MetaHuman Instance {Instance}, item {Item} has produced both PostAssemblyParameters and InstanceParameters. InstanceParameters are deprecated and these will be ignored.",
							GetPathName(), Pair.Key.ToDebugString());

						continue;
					}

					FMetaHumanPostAssemblyParameterOutput PAPOutput
					{
						.Parameters = MoveTemp(Pair.Value.Parameters),
						.ParameterContext = MoveTemp(Pair.Value.ParameterContext)
					};

					ActiveOutput.PostAssemblyParametersFromPipeline.Edit().Add(Pair.Key, MoveTemp(PAPOutput));

					ActiveOutput.ItemsUsingDeprecatedInstanceParameters.Add(Pair.Key);

					ApplyOverriddenInstanceParameters(Pair.Key);
				}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
					
#if WITH_EDITOR
				if (!IsRunningCookCommandlet()
					|| ResolveShouldCookAsAssembled() == EMetaHumanInstanceCookBehavior::No)
				{
					// Unless we're cooking this assembly output, we need to ensure that other 
					// assets don't save references to assembly output objects, because they're 
					// intended to be regenerated on load.
					//
					// To accomplish this, all the objects under the Instance are made transient
					// immediately after assembly. References to transient objects are silently 
					// nulled on save, so any assets that capture references to these objects can
					// safely be saved without generating errors on save or load. They will of 
					// course have to handle null references gracefully though.
					MarkAssemblyOutputSubobjectsAsTransient();
				}
#endif // WITH_EDITOR
			}
			else
			{
				// ClearAssemblyOutput should have been called before AssembleCollection, so the 
				// assembly output should still be clear from then.
				ensure(!IsAssembled());
			}

			OnAssembled.ExecuteIfBound(Status);
			OnAssembledNative.ExecuteIfBound(Status);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (Status == EMetaHumanCharacterAssemblyResult::Succeeded)
			{
				OnInstanceUpdatedNative.Broadcast();
			}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
			RefreshWillCookAsAssembled();
#endif // WITH_EDITOR
		});

	Collection->GetPipeline()->AssembleCollection(Params, OnAssemblyComplete);

	// For now we assume assembly is synchronous. 
	//
	// To support async assembly, there need to be guards in place to handle calls to Assemble 
	// while an assembly is already in progress.
	check(AssemblyResult.IsSet());
}

void UMetaHumanInstance::Assemble(const FMetaHumanCharacterAssembled& OnAssembled)
{
	Assemble(OnAssembled, FMetaHumanCharacterAssembledNative());
}

void UMetaHumanInstance::Assemble(const FMetaHumanCharacterAssembledNative& OnAssembledNative)
{
	Assemble(FMetaHumanCharacterAssembled(), OnAssembledNative);
}

void UMetaHumanInstance::Assemble(EMetaHumanCharacterPaletteBuildQuality Quality, const FMetaHumanCharacterAssembled& OnAssembled)
{
	Assemble(OnAssembled);
}

void UMetaHumanInstance::Assemble(EMetaHumanCharacterPaletteBuildQuality Quality, const FMetaHumanCharacterAssembledNative& OnAssembledNative)
{
	Assemble(OnAssembledNative);
}

void UMetaHumanInstance::Assemble(
	EMetaHumanCharacterPaletteBuildQuality Quality,
	const FMetaHumanCharacterAssembled& OnAssembled,
	const FMetaHumanCharacterAssembledNative& OnAssembledNative)
{
	Assemble(OnAssembled, OnAssembledNative);
}

const FInstancedStruct& UMetaHumanInstance::GetAssemblyOutput()
{
	const FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();
	if (ActiveOutput.IsValid())
	{
		return ActiveOutput.AssemblyOutput;
	}

	// Assemble synchronously, since the caller expects the assembly output
	TOptional<EMetaHumanCharacterAssemblyResult> AssemblyResult;
	const FMetaHumanCharacterAssembledNative Callback = FMetaHumanCharacterAssembledNative::CreateLambda(
		[&AssemblyResult](EMetaHumanCharacterAssemblyResult Result)
		{
			AssemblyResult = Result;
		});

	Assemble(FMetaHumanCharacterAssembled(), Callback);

	// For now we assume assembly is synchronous. In future we'll have to wait on the result here.
	check(AssemblyResult.IsSet());
		
	return GetActiveAssemblyOutput().AssemblyOutput;
}

const FInstancedStruct& UMetaHumanInstance::GetAssemblyOutput() const
{
	return GetExistingAssemblyOutput();
}

const FInstancedStruct& UMetaHumanInstance::GetExistingAssemblyOutput() const
{
	return GetActiveAssemblyOutput().AssemblyOutput;
}

bool UMetaHumanInstance::IsAssembled() const
{
	return GetExistingAssemblyOutput().IsValid();
}

void UMetaHumanInstance::ClearAssemblyOutput()
{
	if (!CanModifyInstance(TEXT("ClearAssemblyOutput")))
	{
		return;
	}

#if WITH_EDITOR
	// Move any existing assembly subobjects out of the Instance and into the transient package
	// before we drop our references to them. This frees up their names for the next assembly and
	// guarantees they won't be saved.
	MoveAssemblyOutputSubobjectsToTransientPackage();
#endif

	FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();

	ActiveOutput.AssemblyOutput.Reset();
	ActiveOutput.PostAssemblyParametersFromPipeline.Edit().Empty();
	CurrentOverriddenAssemblyParameters.Edit().Empty();
#if WITH_EDITORONLY_DATA
	ActiveOutput.AssemblyAssetMetadata.Reset();
#endif

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ActiveOutput.ItemsUsingDeprecatedInstanceParameters.Empty();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UMetaHumanInstance::SetMetaHumanCollection(UMetaHumanCollection* InCollection)
{
	if (Collection == InCollection)
	{
		// No-op
		return;
	}

	if (!CanModifyInstance(TEXT("SetMetaHumanCollection")))
	{
		return;
	}

	// Ensure we don't keep stale assembly output from a different Collection.
	//
	// This allows code to safely assume that any Instance belonging to a Collection contains
	// assembly output compatible with that Collection.
	ClearAssemblyOutput();

	Collection = InCollection;
}

UMetaHumanCollection* UMetaHumanInstance::GetMetaHumanCollection() const
{
	return Collection;
}

void UMetaHumanInstance::CopyContentsFrom(TNotNull<const UMetaHumanInstance*> Other)
{
	if (!CanModifyInstance(TEXT("CopyContentsFrom")))
	{
		return;
	}

	ClearAssemblyOutput();
	
#if WITH_EDITOR
	// Save this collection into the transaction buffer after clearing assembly output.
	//
	// Assembly output may be large and it's safest to regenerate it after an undo.
	Modify();
#endif // WITH_EDITOR

	SetMetaHumanCollection(Other->GetMetaHumanCollection());

	// All of the data contained in these properties should be portable to another Instance. 
	//
	// There aren't any subobjects under here that would need to be duplicated to a new Outer, for
	// example.
	OverriddenInstanceParameters = Other->OverriddenInstanceParameters;
	OverriddenCollectionInstanceParameters = Other->OverriddenCollectionInstanceParameters;
	SlotSelections = Other->SlotSelections;
}

void UMetaHumanInstance::SetSingleSlotSelection(FName SlotName, const FMetaHumanPaletteItemKey& ItemKey)
{
	SetSingleSlotSelection(FMetaHumanPaletteItemPath::Collection, SlotName, ItemKey);
}

void UMetaHumanInstance::SetSingleSlotSelection(const FMetaHumanPaletteItemPath& ParentItemPath, FName SlotName, const FMetaHumanPaletteItemKey& ItemKey)
{
	if (!CanModifyInstance(TEXT("SetSingleSlotSelection")))
	{
		return;
	}

	ClearAssemblyOutput();

	// This is not the most efficient implementation, but it is very simple and this is not a 
	// performance critical function.

	// Remove all existing entries for this slot
	SlotSelections.RemoveAllSwap([&ParentItemPath, SlotName](const FMetaHumanPipelineSlotSelectionData& Element)
		{
			return Element.Selection.ParentItemPath == ParentItemPath
				&& Element.Selection.SlotName == SlotName;
		});

	if (!ItemKey.IsNull())
	{
		// Add a new entry at the end
		SlotSelections.Emplace(FMetaHumanPipelineSlotSelectionData(FMetaHumanPipelineSlotSelection(ParentItemPath, SlotName, ItemKey)));
	}
}

bool UMetaHumanInstance::TryAddSlotSelection(const FMetaHumanPipelineSlotSelection& Selection)
{
	if (!CanModifyInstance(TEXT("TryAddSlotSelection")))
	{
		return false;
	}

	// TODO: Validation

	FMetaHumanPipelineSlotSelectionData NewSelectionData;
	NewSelectionData.Selection = Selection;

	SlotSelections.Add(NewSelectionData);

	ClearAssemblyOutput();

	return true;
}

bool UMetaHumanInstance::TryGetAnySlotSelection(FName SlotName, FMetaHumanPaletteItemKey& OutItemKey) const
{
	return TryGetAnySlotSelection(SlotSelections, FMetaHumanPaletteItemPath::Collection, SlotName, OutItemKey);
}

bool UMetaHumanInstance::TryGetAnySlotSelection(const FMetaHumanPaletteItemPath& ParentItemPath, FName SlotName, FMetaHumanPaletteItemKey& OutItemKey) const
{
	return TryGetAnySlotSelection(SlotSelections, ParentItemPath, SlotName, OutItemKey);
}

bool UMetaHumanInstance::TryGetAnySlotSelection(
	TConstArrayView<FMetaHumanPipelineSlotSelectionData> SlotSelections, 
	FName SlotName, 
	FMetaHumanPaletteItemKey& OutItemKey)
{
	return TryGetAnySlotSelection(SlotSelections, FMetaHumanPaletteItemPath::Collection, SlotName, OutItemKey);
}

bool UMetaHumanInstance::TryGetAnySlotSelection(
	TConstArrayView<FMetaHumanPipelineSlotSelectionData> SlotSelections, 
	const FMetaHumanPaletteItemPath& ParentItemPath, 
	FName SlotName, 
	FMetaHumanPaletteItemKey& OutItemKey)
{
	const FMetaHumanPipelineSlotSelectionData* Selection = Algo::FindByPredicate(SlotSelections,
		[&ParentItemPath, SlotName](const FMetaHumanPipelineSlotSelectionData& Element)
		{
			return Element.Selection.ParentItemPath == ParentItemPath
				&& Element.Selection.SlotName == SlotName;
		});

	if (!Selection)
	{
		// Initialize this to the null item in case the caller tries to read it
		OutItemKey = FMetaHumanPaletteItemKey();
		return false;
	}

	OutItemKey = Selection->Selection.SelectedItem;
	return true;
}

bool UMetaHumanInstance::TryGetAnySlotSelection(
	TConstArrayView<FMetaHumanPipelineSlotSelection> SlotSelections, 
	FName SlotName, 
	FMetaHumanPaletteItemKey& OutItemKey)
{
	return TryGetAnySlotSelection(SlotSelections, FMetaHumanPaletteItemPath(), SlotName, OutItemKey);
}

bool UMetaHumanInstance::TryGetAnySlotSelection(
	TConstArrayView<FMetaHumanPipelineSlotSelection> SlotSelections, 
	const FMetaHumanPaletteItemPath& ParentItemPath, 
	FName SlotName, 
	FMetaHumanPaletteItemKey& OutItemKey)
{
	const FMetaHumanPipelineSlotSelection* Selection = Algo::FindByPredicate(SlotSelections,
		[&ParentItemPath, SlotName](const FMetaHumanPipelineSlotSelection& Element)
		{
			return Element.ParentItemPath == ParentItemPath
				&& Element.SlotName == SlotName;
		});

	if (!Selection)
	{
		// Initialize this to the null item in case the caller tries to read it
		OutItemKey = FMetaHumanPaletteItemKey();
		return false;
	}

	OutItemKey = Selection->SelectedItem;
	return true;
}

bool UMetaHumanInstance::ContainsSlotSelection(const FMetaHumanPipelineSlotSelection& Selection) const
{
	return SlotSelections.ContainsByPredicate(
		[&Selection](const FMetaHumanPipelineSlotSelectionData& Element)
		{
			return Element.Selection == Selection;
		});
}

bool UMetaHumanInstance::TryRemoveSlotSelection(const FMetaHumanPipelineSlotSelection& Selection)
{
	if (!CanModifyInstance(TEXT("TryRemoveSlotSelection")))
	{
		return false;
	}

	const int32 Index = SlotSelections.IndexOfByPredicate(
		[&Selection](const FMetaHumanPipelineSlotSelectionData& Element)
		{
			return Element.Selection == Selection;
		});

	if (Index == INDEX_NONE)
	{
		return false;
	}

	SlotSelections.RemoveAtSwap(Index);

	ClearAssemblyOutput();

	return true;
}

const TArray<FMetaHumanPipelineSlotSelectionData>& UMetaHumanInstance::GetSlotSelectionData() const
{
	return SlotSelections;
}

TArray<FMetaHumanPinnedSlotSelection> UMetaHumanInstance::ToPinnedSlotSelections(EMetaHumanUnusedSlotBehavior UnusedSlotBehavior) const
{
	TArray<FMetaHumanPinnedSlotSelection> Result;
	Result.Reserve(SlotSelections.Num());

	TArray<FName> UnusedSlotsToPin;
	if (UnusedSlotBehavior == EMetaHumanUnusedSlotBehavior::PinnedToEmpty)
	{
		if (!Collection
			|| !Collection->GetPipeline())
		{
			UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
				"ToPinnedSlotSelections: Can't generate empty pinned slot selections for {Instance}, because there is no Collection Pipeline", 
				GetPathName());

			return Result;
		}

		Collection->GetPipeline()->GetSpecification()->Slots.GenerateKeyArray(UnusedSlotsToPin);
	}

	for (const FMetaHumanPipelineSlotSelectionData& SelectionData : SlotSelections)
	{
		FMetaHumanPinnedSlotSelection& PinnedSelection = Result.AddDefaulted_GetRef();
		PinnedSelection.Selection = SelectionData.Selection;

		const FInstancedPropertyBag* Params = OverriddenInstanceParameters.Find(SelectionData.Selection.GetSelectedItemPath());
		if (Params)
		{
			PinnedSelection.AssemblyParameters = *Params;
		}

		// TODO: Handle sub-items
		if (SelectionData.Selection.ParentItemPath.IsEmpty())
		{
			// This slot is used, so remove it from the unused list
			UnusedSlotsToPin.Remove(SelectionData.Selection.SlotName);
		}
	}

	// Create empty selections for any unused slots
	for (const FName UnusedSlotName : UnusedSlotsToPin)
	{
		FMetaHumanPinnedSlotSelection& PinnedSelection = Result.AddDefaulted_GetRef();
		PinnedSelection.Selection.SlotName = UnusedSlotName;
	}

	return Result;
}

TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> UMetaHumanInstance::GetAssemblyInstanceParameters() const
{
	return GetPostAssemblyParameters();
}

TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> UMetaHumanInstance::GetAssemblyParameters() const
{
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> Result;

	if (!Collection)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetAssemblyParameters failed on MetaHuman Instance {Instance} because no Collection is set", GetPathName());
		return Result;
	}

	const FMetaHumanCollectionBuiltData& BuiltData = Collection->GetBuiltData();
	if (!BuiltData.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetAssemblyParameters failed on MetaHuman Instance {Instance} because the Collection is not built", GetPathName());
		return Result;
	}

	Result.Reserve(BuiltData.PaletteBuiltData.ItemBuiltData.View().SortedElements.Num());

	const FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();

	for (const FMetaHumanPipelineBuiltDataCollectionPair& Pair : BuiltData.PaletteBuiltData.ItemBuiltData.View().SortedElements)
	{
		if (Pair.Value.AssemblyParameters.IsValid())
		{
			Result.Add(Pair.Key, Pair.Value.AssemblyParameters);
		}
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		else if (ActiveOutput.ItemsUsingDeprecatedInstanceParameters.Contains(Pair.Key))
		{
			// In the deprecated Instance Parameters system, the parameters come with the Assembly Output
		
			const FMetaHumanPostAssemblyParameterOutput* PAPOutput = ActiveOutput.PostAssemblyParametersFromPipeline.View().Find(Pair.Key);
			if (PAPOutput)
			{
				Result.Add(Pair.Key, PAPOutput->Parameters);
			}
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return Result;
}

TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> UMetaHumanInstance::GetPostAssemblyParameters() const
{
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> Result;
	
	const FMetaHumanPostAssemblyParameterOutputCollectionView PostAssemblyParameterView = GetActiveAssemblyOutput().PostAssemblyParametersFromPipeline.View();
	Result.Reserve(PostAssemblyParameterView.SortedElements.Num());

	for (const FMetaHumanPostAssemblyParameterOutputCollectionPair& ParamWithContext : PostAssemblyParameterView.SortedElements)
	{
		Result.Add(ParamWithContext.Key, ParamWithContext.Value.Parameters);
	}

	return Result;
}

TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> UMetaHumanInstance::GetOverriddenInstanceParameters() const
{
	TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag> Result = OverriddenInstanceParameters;
	
	if (OverriddenCollectionInstanceParameters.IsValid())
	{
		Result.Add(FMetaHumanPaletteItemPath::Collection, OverriddenCollectionInstanceParameters);
	}

	return Result;
}

FInstancedPropertyBag UMetaHumanInstance::GetCurrentInstanceParametersForItem(const FMetaHumanPaletteItemPath& ItemPath) const
{
	if (!Collection)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetCurrentInstanceParametersForItem failed on MetaHuman Instance {Instance} because no Collection is set", GetPathName());
		return FInstancedPropertyBag();
	}

	const FMetaHumanCollectionBuiltData& BuiltData = Collection->GetBuiltData();
	if (!BuiltData.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, "GetCurrentInstanceParametersForItem failed on MetaHuman Instance {Instance} because the Collection is not built", GetPathName());
		return FInstancedPropertyBag();
	}

	const FInstancedPropertyBag* OriginalParameters = nullptr;

	const FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (ActiveOutput.ItemsUsingDeprecatedInstanceParameters.Contains(ItemPath))
	{
		// In the deprecated Instance Parameters system, the parameters come with the Assembly Output
		
		const FMetaHumanPostAssemblyParameterOutput* PAPOutput = ActiveOutput.PostAssemblyParametersFromPipeline.View().Find(ItemPath);
		if (PAPOutput)
		{
			OriginalParameters = &PAPOutput->Parameters;
		}
	}
	else
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		// In the current system, the parameters come from the built data

		const FMetaHumanPipelineBuiltData* ItemBuiltData = BuiltData.PaletteBuiltData.ItemBuiltData.View().Find(ItemPath);
		if (ItemBuiltData && ItemBuiltData->AssemblyParameters.IsValid())
		{
			OriginalParameters = &ItemBuiltData->AssemblyParameters;
		}
	}

	if (!OriginalParameters)
	{
		return FInstancedPropertyBag();
	}

	const FInstancedPropertyBag* OverriddenParameters = ItemPath.IsEmpty() ? &OverriddenCollectionInstanceParameters : OverriddenInstanceParameters.Find(ItemPath);
	if (!OverriddenParameters || !OverriddenParameters->IsValid())
	{
		return *OriginalParameters;
	}

	FInstancedPropertyBag Result = *OriginalParameters;
	Result.CopyMatchingValuesByName(*OverriddenParameters);
	return Result;
}

EMetaHumanInstanceParameterOverrideResult UMetaHumanInstance::OverrideInstanceParameters(const FMetaHumanPaletteItemPath& ItemPath, const FInstancedPropertyBag& NewParameters)
{
	EMetaHumanInstanceParameterOverrideResult Result = EMetaHumanInstanceParameterOverrideResult::NoParametersWereModified;

	if (!CanModifyInstance(TEXT("OverrideInstanceParameters")))
	{
		return Result;
	}

	if (!NewParameters.GetPropertyBagStruct()
		|| NewParameters.GetPropertyBagStruct()->GetPropertyDescs().Num() == 0)
	{
		return Result;
	}
	
	FInstancedPropertyBag& OverriddenParameters = ItemPath.IsEmpty() ? OverriddenCollectionInstanceParameters : OverriddenInstanceParameters.FindOrAdd(ItemPath);

	// Merge new parameter values into any existing ones
	if (OverriddenParameters.IsValid())
	{
		if (NewParameters.GetPropertyBagStruct() == OverriddenParameters.GetPropertyBagStruct())
		{
			// The property bags use the exact same struct, so simply copy the data over
			NewParameters.GetPropertyBagStruct()->CopyScriptStruct(
				OverriddenParameters.GetMutableValue().GetMemory(),
				NewParameters.GetValue().GetMemory());
		}
		else
		{
			// Add any properties from NewParameters that don't already exist.
			//
			// Note that any existing properties with the same name but of a different type will be 
			// changed to the new type.
			const EPropertyBagAlterationResult AddResult = OverriddenParameters.AddProperties(NewParameters.GetPropertyBagStruct()->GetPropertyDescs());
			if (AddResult != EPropertyBagAlterationResult::Success)
			{
				UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
					"OverrideInstanceParameters: Failed to merge the provided parameters with the existing parameters for {ItemPath}: {Reason}", 
					ItemPath.ToDebugString(), 
					StaticEnum<EPropertyBagAlterationResult>()->GetNameStringByValue(static_cast<int64>(AddResult)));

				return Result;
			}

			// Copy over the property values
			OverriddenParameters.CopyMatchingValuesByName(NewParameters);
		}
	}
	else
	{
		// There is no property bag yet, so just copy the passed-in one
		OverriddenParameters = NewParameters;
	}

	// Don't allow Post-Assembly Parameters to be set for items with no built data, even if 
	// Assembly Output is valid.
	//
	// This helps to prevent unexpected behavior when the Instance is in an invalid state.

	if (!Collection
		|| !Collection->GetBuiltData().IsValid())
	{
		Result |= EMetaHumanInstanceParameterOverrideResult::UnrecognizedParametersWereStored;
		return Result;
	}

	// Note that if ItemPath is empty, this will fetch the Collection's own built data, which is a
	// valid case.
	const FMetaHumanPipelineBuiltData* ItemBuiltData = Collection->GetBuiltData().PaletteBuiltData.ItemBuiltData.View().Find(ItemPath);
	if (!ItemBuiltData)
	{
		Result |= EMetaHumanInstanceParameterOverrideResult::UnrecognizedParametersWereStored;
		return Result;
	}

	if (!ItemPath.IsEmpty())
	{
		// In theory, if there is built data for an item, that item should definitely exist in 
		// the Collection, because built data should be cleared any time an item is modified.
		//
		// As a failsafe, we look up the item here to make sure it exists, so that individual
		// pipelines don't have to handle this case.

		const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
		FMetaHumanCharacterPaletteItem Item;
		if (!ensure(Collection->TryResolveItem(ItemPath, ContainingPalette, Item)))
		{
			Result |= EMetaHumanInstanceParameterOverrideResult::UnrecognizedParametersWereStored;
			return Result;
		}
	}

	FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (ActiveOutput.ItemsUsingDeprecatedInstanceParameters.Contains(ItemPath))
	{
		Result |= ApplyOverriddenInstanceParameters(ItemPath);
		return Result;
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	auto IsPropertyCompatibleWithPipeline = [](const FInstancedPropertyBag& PipelinePropertyBag, const FPropertyBagPropertyDesc& Property)
	{
		const FPropertyBagPropertyDesc* PipelineProperty = PipelinePropertyBag.FindPropertyDescByName(Property.Name);
		return PipelineProperty
			&& PipelineProperty->CompatibleType(Property);
	};

	bool bAreAllNewParamsPostAssemblyParams = false;

	const FMetaHumanPostAssemblyParameterOutput* PostAssemblyParam = ActiveOutput.PostAssemblyParametersFromPipeline.View().Find(ItemPath);
	if (PostAssemblyParam)
	{
		check(ActiveOutput.AssemblyOutput.IsValid());

		bAreAllNewParamsPostAssemblyParams = true;
		for (const FPropertyBagPropertyDesc& NewProperty : NewParameters.GetPropertyBagStruct()->GetPropertyDescs())
		{
			if (!IsPropertyCompatibleWithPipeline(PostAssemblyParam->Parameters, NewProperty))
			{
				bAreAllNewParamsPostAssemblyParams = false;
				break;
			}
		}

		if (bAreAllNewParamsPostAssemblyParams)
		{
			// Fast path for the most common case

			Collection->GetPipeline()->SetPostAssemblyParameters(
				Collection,
				ActiveOutput.PostAssemblyParametersFromPipeline.View(),
				ItemPath,
				NewParameters,
				ActiveOutput.AssemblyOutput);

			Result |= EMetaHumanInstanceParameterOverrideResult::PostAssemblyParametersWereModified;
		}
		else
		{
			// Not all params are compatible (or perhaps not all can be set Post-Assembly), so 
			// copy only the compatible ones into a temporary structure.
				
			TArray<FPropertyBagPropertyDesc> CompatibleNewParameterProperties;
			CompatibleNewParameterProperties.Reserve(NewParameters.GetPropertyBagStruct()->GetPropertyDescs().Num());

			for (const FPropertyBagPropertyDesc& NewProperty : NewParameters.GetPropertyBagStruct()->GetPropertyDescs())
			{
				if (IsPropertyCompatibleWithPipeline(PostAssemblyParam->Parameters, NewProperty))
				{
					CompatibleNewParameterProperties.Add(NewProperty);
				}
			}

			if (CompatibleNewParameterProperties.Num() > 0)
			{
				FInstancedPropertyBag CompatibleNewParameters;
				verify(CompatibleNewParameters.AddProperties(CompatibleNewParameterProperties) == EPropertyBagAlterationResult::Success);
				CompatibleNewParameters.CopyMatchingValuesByName(NewParameters);

				Collection->GetPipeline()->SetPostAssemblyParameters(
					Collection,
					ActiveOutput.PostAssemblyParametersFromPipeline.View(),
					ItemPath,
					CompatibleNewParameters,
					ActiveOutput.AssemblyOutput);

				Result |= EMetaHumanInstanceParameterOverrideResult::PostAssemblyParametersWereModified;
			}
		}
	}

	if (!bAreAllNewParamsPostAssemblyParams)
	{
		// Not all of the parameters being set were Post-Assembly Parameters. 
		//
		// We now check them against the built data to see if they're Assembly Parameters. This 
		// gives the caller useful feedback without them having to do their own lookups.

		for (const FPropertyBagPropertyDesc& NewProperty : NewParameters.GetPropertyBagStruct()->GetPropertyDescs())
		{
			if (PostAssemblyParam && IsPropertyCompatibleWithPipeline(PostAssemblyParam->Parameters, NewProperty))
			{
				// This is a Post-Assembly Parameter
				check(Result & EMetaHumanInstanceParameterOverrideResult::PostAssemblyParametersWereModified);
				continue;
			}
					
			if (IsPropertyCompatibleWithPipeline(ItemBuiltData->AssemblyParameters, NewProperty))
			{
				// This is an Assembly Parameter
				Result |= EMetaHumanInstanceParameterOverrideResult::ReassemblyRequiredToApplyNewParameters;

				ClearAssemblyOutput();
			}
			else
			{
				// This isn't any known parameter, but it was stored in the Instance anyway
				Result |= EMetaHumanInstanceParameterOverrideResult::UnrecognizedParametersWereStored;
			}

			const EMetaHumanInstanceParameterOverrideResult BothPossibleResults = 
				EMetaHumanInstanceParameterOverrideResult::ReassemblyRequiredToApplyNewParameters
				| EMetaHumanInstanceParameterOverrideResult::UnrecognizedParametersWereStored;

			// The remaining properties can't affect the result
			if ((Result & BothPossibleResults) == BothPossibleResults)
			{
				break;
			}
		}
	}

	return Result;
}

void UMetaHumanInstance::ClearAllOverriddenInstanceParameters()
{
	if (!CanModifyInstance(TEXT("ClearAllOverriddenInstanceParameters")))
	{
		return;
	}

	OverriddenInstanceParameters.Reset();
	OverriddenCollectionInstanceParameters.Reset();

	ClearAssemblyOutput();
}

void UMetaHumanInstance::ClearOverriddenInstanceParameters(const FMetaHumanPaletteItemPath& ItemPath)
{
	if (!CanModifyInstance(TEXT("ClearOverriddenInstanceParameters")))
	{
		return;
	}

	if (ItemPath == FMetaHumanPaletteItemPath::Collection)
	{
		OverriddenCollectionInstanceParameters.Reset();
	}
	else
	{
		OverriddenInstanceParameters.Remove(ItemPath);
	}

	ClearAssemblyOutput();
}

#if WITH_EDITOR
bool UMetaHumanInstance::TryUnpack(const FString& TargetFolder)
{
	if (!Collection)
	{
		return false;
	}

	const UMetaHumanCollectionEditorPipeline* Pipeline = Collection->GetEditorPipeline();
	if (!Pipeline)
	{
		return false;
	}

	FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();
	return Pipeline->TryUnpackInstanceAssets(this, ActiveOutput.AssemblyOutput, ActiveOutput.AssemblyAssetMetadata, TargetFolder);
}

void UMetaHumanInstance::NotifyAssemblyOutputInvalidated()
{
	if (!GEditor)
	{
		return;
	}

	// Re-trigger assembly on any actor in the open level that references this Instance.
	//
	// Actors in unloaded levels will call GetAssemblyOutput on load, so no action is needed to
	// update them.
	//
	// We deliberately ignore preview worlds, because asset editors should be in control of their
	// respective worlds.
	UWorld* const EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld
		|| EditorWorld->WorldType != EWorldType::Editor)
	{
		return;
	}

	for (TActorIterator<AActor> ActorIt(EditorWorld); ActorIt; ++ActorIt)
	{
		AActor* const Actor = *ActorIt;
		if (!Actor
			|| !Actor->Implements<UMetaHumanCharacterActorInterface>())
		{
			continue;
		}

		UMetaHumanInstance* const ActorInstance = IMetaHumanCharacterActorInterface::Execute_GetMetaHumanInstance(Actor);
		if (ActorInstance != this)
		{
			continue;
		}

		Actor->Modify();

		IMetaHumanCharacterActorInterface::Execute_SetMetaHumanInstance(Actor, this);
	}
}

void UMetaHumanInstance::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

	RemoveStaleOverriddenInstanceParameters();
	
	// Clear out missing items on save, so that users can clear missing items by re-saving an 
	// Instance.
	//
	// Saving is generally understood to clear out references to missing assets in other asset 
	// types as well, so this fits the existing engine workflow.
	ValidateAndSanitizeSlotSelections(/*bShouldRemoveMissingItemSelections*/ true);

	// This flag is only set when this Instance was loaded from a cooked package. 
	// 
	// We shouldn't be saving in that case, but protect against it anyway.
	if (!ensure(!bIsAssemblyOutputCooked))
	{
		return;
	}

	if (ObjectSaveContext.IsCooking())
	{
		const EMetaHumanInstanceCookBehavior Resolved = ResolveShouldCookAsAssembled();
		if (Resolved == EMetaHumanInstanceCookBehavior::Yes)
		{
			if (!RuntimeAssemblyOutput.IsValid())
			{
				// Synchronously assemble into EditorAssemblyOutput if needed, and copy the result
				// into RuntimeAssemblyOutput.
				//
				// The result is copied rather than moved, so that other calls to GetAssemblyOutput 
				// still find valid assembly output and assembly isn't re-triggered.
				//
				// We could add logic to GetActiveAssemblyOutput to avoid this copy if needed, but
				// it doesn't seem worth the added complexity unless there's a specific issue with
				// doing the copy.
				static_cast<void>(GetAssemblyOutput());
				RuntimeAssemblyOutput = EditorAssemblyOutput;

				if (!RuntimeAssemblyOutput.IsValid())
				{
					UE_LOGFMT(LogMetaHumanCharacterPalette, Error,
						"Cook-time assembly failed for Instance {Instance}. RuntimeAssemblyOutput will be empty in the cooked package.",
						GetPathName());
				}
			}
		}
		else
		{
			// Not baking any assembly output into the cooked package.
			RuntimeAssemblyOutput = FMetaHumanProcessedAssemblyOutput();
		}
	}
	else
	{
		// Outside of cook, RuntimeAssemblyOutput should never be populated. 
		//
		// Clear defensively in case it was set somehow during the editor session.
		RuntimeAssemblyOutput = FMetaHumanProcessedAssemblyOutput();
	}
}

void UMetaHumanInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaHumanInstance, ShouldCookAsAssembled))
	{
		RefreshWillCookAsAssembled();
	}
}

void UMetaHumanInstance::RemoveStaleOverriddenInstanceParameters()
{
	if (!Collection)
	{
		return;
	}

	for (TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag>::TIterator It = OverriddenInstanceParameters.CreateIterator(); It; ++It)
	{
		const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
		FMetaHumanCharacterPaletteItem Item;

		// Find out if the item these overrides belong to is still in the Collection.
		//
		// Note that this will also fail for entries with an empty item path as the key.
		//
		// This is by design, because the Collection's parameters are stored in 
		// OverriddenCollectionInstanceParameters, so any empty keys in here must be due to 
		// force-deleted assets.
		if (!Collection->TryResolveItem(It.Key(), ContainingPalette, Item))
		{
			It.RemoveCurrent();
		}
	}
}

EMetaHumanInstanceCookBehavior UMetaHumanInstance::ResolveShouldCookAsAssembled() const
{
	if (Collection
		&& Collection->GetDefaultInstance() == this)
	{
		// The default instance is editor-only and shouldn't be assembled on cook
		return EMetaHumanInstanceCookBehavior::No;
	}

	if (ShouldCookAsAssembled != EMetaHumanInstanceCookBehavior::PipelineDefault)
	{
		return ShouldCookAsAssembled;
	}

	if (Collection)
	{
		if (const UMetaHumanCollectionEditorPipeline* EditorPipeline = Collection->GetEditorPipeline())
		{
			return EditorPipeline->ShouldCookInstanceAsAssembled(this)
				? EMetaHumanInstanceCookBehavior::Yes
				: EMetaHumanInstanceCookBehavior::No;
		}
	}

	return EMetaHumanInstanceCookBehavior::PipelineDefault;
}

void UMetaHumanInstance::RefreshWillCookAsAssembled()
{
#if WITH_EDITORONLY_DATA
	// This doesn't affect any logic.
	//
	// It's used only to display in the editor UI whether this Instance will be assembled on cook
	// or not.
	WillCookAsAssembled = ResolveShouldCookAsAssembled();
#endif
}

void UMetaHumanInstance::MarkAssemblyOutputSubobjectsAsTransient()
{
	const TSet<UObject*> Subobjects = UE::MetaHuman::UnpackUtilities::GetAllSubobjectsOfOwnerFromStruct(
		FMetaHumanProcessedAssemblyOutput::StaticStruct(),
		&EditorAssemblyOutput,
		this,
		/*bRecursive=*/ true);

	for (UObject* Obj : Subobjects)
	{
		Obj->SetFlags(RF_Transient);
	}
}

void UMetaHumanInstance::MoveAssemblyOutputSubobjectsToTransientPackage()
{
	const TSet<UObject*> Subobjects = UE::MetaHuman::UnpackUtilities::GetAllSubobjectsOfOwnerFromStruct(
		FMetaHumanProcessedAssemblyOutput::StaticStruct(),
		&EditorAssemblyOutput,
		this,
		/*bRecursive=*/ true);

	UPackage* const TransientPackage = GetTransientPackage();

	for (UObject* Obj : Subobjects)
	{
		Obj->SetFlags(RF_Transient);

		// Move the object into the transient package, so any new subobjects produced by a 
		// subsequent assembly are free to take the names that were in use here.
		Obj->Rename(nullptr, TransientPackage, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	}
}
#endif // WITH_EDITOR

void UMetaHumanInstance::ValidateAndSanitizeSlotSelections(bool bShouldRemoveMissingItemSelections)
{
	for (int32 Index = SlotSelections.Num() - 1; Index >= 0; Index--)
	{
		const FMetaHumanPipelineSlotSelectionData& Candidate = SlotSelections[Index];

		// If this selection is identical to an earlier one, remove it
		{
			bool bIsDuplicate = false;
			for (int32 EarlierIndex = 0; EarlierIndex < Index; EarlierIndex++)
			{
				if (SlotSelections[EarlierIndex].Selection == Candidate.Selection)
				{
					bIsDuplicate = true;
					break;
				}
			}

			if (bIsDuplicate)
			{
				UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
					"Removing duplicate slot selection from Instance {Instance}: ParentItemPath={ParentItemPath} SlotName={SlotName} SelectedItem={SelectedItem}",
					GetPathName(),
					Candidate.Selection.ParentItemPath.ToDebugString(),
					Candidate.Selection.SlotName.ToString(),
					Candidate.Selection.SelectedItem.ToDebugString());

				SlotSelections.RemoveAt(Index);
				continue;
			}
		}

		if (Collection)
		{
			const UMetaHumanCharacterPalette* ContainingPalette = nullptr;
			FMetaHumanCharacterPaletteItem Item;
			const FMetaHumanPaletteItemPath SelectedItemPath = Candidate.Selection.GetSelectedItemPath();
			if (!Collection->TryResolveItem(SelectedItemPath, ContainingPalette, Item))
			{
				if (bShouldRemoveMissingItemSelections)
				{
					UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
						"Removing slot selection with missing item from Instance {Instance}. SelectedItem={SelectedItem} is missing from Collection {Collection}.",
						GetPathName(),
						Candidate.Selection.SelectedItem.ToDebugString(),
						Collection->GetPathName());

					SlotSelections.RemoveAt(Index);
				}
				else
				{
					UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
						"Instance {Instance} references item {SelectedItem}, which is missing from Collection {Collection}.",
						GetPathName(),
						Candidate.Selection.SelectedItem.ToDebugString(),
						Collection->GetPathName());
				}

				continue;
			}

			if (Item.SlotName != Candidate.Selection.SlotName)
			{
				UE_LOGFMT(LogMetaHumanCharacterPalette, Warning,
					"Removing slot selection from Instance {Instance} with mismatched slot name. SelectedItem={SelectedItem} is authored for slot {AuthoredSlot} but the selection targets slot {SelectionSlot}.",
					GetPathName(),
					Candidate.Selection.SelectedItem.ToDebugString(),
					Item.SlotName.ToString(),
					Candidate.Selection.SlotName.ToString());

				SlotSelections.RemoveAt(Index);
				continue;
			}
		}
	}
}

void UMetaHumanInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	// Remove slot selections and instance parameter values for items no longer in the Collection
	RemoveStaleOverriddenInstanceParameters();
	ValidateAndSanitizeSlotSelections(/*bShouldRemoveMissingItemSelections*/ true);

	RefreshWillCookAsAssembled();
#endif

	// If we've loaded a cooked Instance with a baked assembly output, lock it into runtime mode.
	{
		const UPackage* const InstancePackage = GetPackage();
		if (InstancePackage
			&& InstancePackage->HasAnyPackageFlags(PKG_Cooked)
			&& RuntimeAssemblyOutput.IsValid())
		{
			bIsAssemblyOutputCooked = true;
		}
	}
}

EMetaHumanInstanceParameterOverrideResult UMetaHumanInstance::ApplyOverriddenInstanceParameters(const FMetaHumanPaletteItemPath& ItemPath) const
{
	const FInstancedPropertyBag* OverriddenParameters = ItemPath.IsEmpty() ? &OverriddenCollectionInstanceParameters : OverriddenInstanceParameters.Find(ItemPath);

	const FMetaHumanProcessedAssemblyOutput& ActiveOutput = GetActiveAssemblyOutput();

	if (!OverriddenParameters
		|| !OverriddenParameters->IsValid()
		|| !Collection
		|| !ActiveOutput.AssemblyOutput.IsValid())
	{
		return EMetaHumanInstanceParameterOverrideResult::NoParametersWereModified;
	}

	const FMetaHumanPostAssemblyParameterOutput* PostAssemblyParameters = ActiveOutput.PostAssemblyParametersFromPipeline.View().Find(ItemPath);
	if (!PostAssemblyParameters)
	{
		// This item doesn't have any instance parameters.
		return EMetaHumanInstanceParameterOverrideResult::NoParametersWereModified;
	}
	
	const UMetaHumanCharacterPipeline* ParameterPipeline = nullptr;
	if (!Collection->TryResolvePipeline(ItemPath, ParameterPipeline))
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error, 
			"ItemPath {ItemPath} couldn't be resolved to an item in Collection {Collection} while applying overridden Instance Parameters", 
			ItemPath.ToDebugString(), 
			GetPathNameSafe(Collection));

		return EMetaHumanInstanceParameterOverrideResult::NoParametersWereModified;
	}

	// Notify the pipeline that instance parameters have been set, so that it can apply them to
	// whatever it is that they control, e.g. set material parameters from the parameter values.
	if (PostAssemblyParameters->Parameters.GetPropertyBagStruct() == OverriddenParameters->GetPropertyBagStruct())
	{
		// Can pass OverriddenParameters directly, as it's the same struct type
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ParameterPipeline->SetInstanceParameters(PostAssemblyParameters->ParameterContext, *OverriddenParameters);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	else
	{
		// The overridden parameters struct is different from the struct that the pipeline is 
		// expecting, so we need to create a temporary property bag and copy the parameters over.

		// If this path gets hit a lot, we could cache this on a transient member variable.
		FInstancedPropertyBag TempParameters = PostAssemblyParameters->Parameters;

		TempParameters.CopyMatchingValuesByName(*OverriddenParameters);
		
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ParameterPipeline->SetInstanceParameters(PostAssemblyParameters->ParameterContext, TempParameters);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return EMetaHumanInstanceParameterOverrideResult::PostAssemblyParametersWereModified;
}

const FMetaHumanProcessedAssemblyOutput& UMetaHumanInstance::GetActiveAssemblyOutput() const
{
#if WITH_EDITOR
	return EditorAssemblyOutput;
#else
	return RuntimeAssemblyOutput;
#endif
}

FMetaHumanProcessedAssemblyOutput& UMetaHumanInstance::GetActiveAssemblyOutput()
{
#if WITH_EDITOR
	return EditorAssemblyOutput;
#else
	return RuntimeAssemblyOutput;
#endif
}

bool UMetaHumanInstance::CanModifyInstance(const TCHAR* DebugOperationName) const
{
	if (bIsAssemblyOutputCooked)
	{
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error,
			"{Operation} is not permitted on Instance {Instance} because its assembly output was baked into the cooked package.",
			DebugOperationName, GetPathName());
		
		return false;
	}

#if WITH_EDITOR
	if (IsAssembled()
		&& IsRunningCookCommandlet() 
		&& ResolveShouldCookAsAssembled() == EMetaHumanInstanceCookBehavior::Yes)
	{
		// We can't allow any kind of edits during cooking after the initial assembly, because 
		// these can cause mismatches between the objects saved into the cooked Instance package
		// and objects in other packages that are referencing them.
		UE_LOGFMT(LogMetaHumanCharacterPalette, Error,
			"{Operation} is not permitted on Instance {Instance} during cooking.",
			DebugOperationName, GetPathName());
		
		return false;
	}
#endif

	return true;
}
