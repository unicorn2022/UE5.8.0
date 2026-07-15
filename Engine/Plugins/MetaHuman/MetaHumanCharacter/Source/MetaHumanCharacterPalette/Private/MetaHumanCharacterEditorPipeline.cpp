// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorPipeline.h"

#include "MetaHumanCharacterEditorPipelineSpecification.h"
#include "MetaHumanCharacterPaletteLog.h"
#include "MetaHumanCharacterPipeline.h"
#include "MetaHumanWardrobeItem.h"

#include "Logging/StructuredLog.h"
#include "Misc/ScopedSlowTask.h"

#if WITH_EDITOR

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorPipeline"

bool UMetaHumanCharacterEditorPipeline::IsPrincipalAssetClassCompatibleWithSlot(FName SlotName, TNotNull<const UClass*> AssetClass) const
{
	const FMetaHumanCharacterPipelineSlotEditorData* SlotEditorEntry = GetSpecification()->SlotEditorData.Find(SlotName);
	if (!SlotEditorEntry)
	{
		// Slot not found, or has no editor-only data declaring supported asset types
		return false;
	}

	return SlotEditorEntry->SupportsAssetType(AssetClass);
}

EMetaHumanWardrobeItemCompatibility UMetaHumanCharacterEditorPipeline::TestWardrobeItemCompatibilityWithSlot(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem) const
{
	const UObject* PrincipalAsset = WardrobeItem->PrincipalAsset.Get();

	if (!PrincipalAsset)
	{
		FScopedSlowTask Progress(0, LOCTEXT("LoadingAssets", "Loading assets..."));
		Progress.MakeDialog();

		PrincipalAsset = WardrobeItem->PrincipalAsset.LoadSynchronous();
	}

	if (!PrincipalAsset)
	{
		return EMetaHumanWardrobeItemCompatibility::None;
	}

	// TODO: Check compatibility of any pipeline set on the WardrobeItem

	if (IsPrincipalAssetClassCompatibleWithSlot(SlotName, PrincipalAsset->GetClass()))
	{
		return EMetaHumanWardrobeItemCompatibility::Add;
	}

	return EMetaHumanWardrobeItemCompatibility::None;
}

bool UMetaHumanCharacterEditorPipeline::IsWardrobeItemCompatibleWithSlot(FName SlotName, TNotNull<const UMetaHumanWardrobeItem*> WardrobeItem) const
{
	return TestWardrobeItemCompatibilityWithSlot(SlotName, WardrobeItem) == EMetaHumanWardrobeItemCompatibility::Add;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
