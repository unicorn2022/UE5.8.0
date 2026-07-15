// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorPipelineSpecification.h"

#include "MetaHumanCharacterPipelineSpecification.h"

#include "AssetRegistry/AssetData.h"

namespace
{
bool SlotSupportsAssetType(const FMetaHumanCharacterPipelineSlotEditorData& InSlot, TNotNull<const UClass*> InAssetType)
{
	for (const TSoftClassPtr<UObject>& SoftSupportedType : InSlot.SupportedPrincipalAssetTypes)
	{
		const UClass* SupportedType = SoftSupportedType.LoadSynchronous();
		if (SupportedType
			&& InAssetType->IsChildOf(SupportedType))
		{
			return true;
		}
	}

	return false;
}
}

bool FMetaHumanCharacterPipelineSlotEditorData::SupportsAsset(const FAssetData& InAsset) const
{
	const TSoftClassPtr<UObject> SoftAssetClass(FSoftObjectPath(InAsset.AssetClassPath));
	const UClass* AssetClass = SoftAssetClass.LoadSynchronous();
	if (!AssetClass)
	{
		return false;
	}

	return SlotSupportsAssetType(*this, AssetClass);
}

bool FMetaHumanCharacterPipelineSlotEditorData::SupportsAssetType(TNotNull<const UClass*> InAssetType) const
{
	return SlotSupportsAssetType(*this, InAssetType);
}

bool UMetaHumanCharacterEditorPipelineSpecification::IsValid(TNotNull<const UMetaHumanCharacterPipelineSpecification*> InRuntimeSpec) const
{
	if (!InRuntimeSpec->IsValid())
	{
		return false;
	}

	// For each virtual slot in the runtime spec, verify that this editor spec's
	// SupportedPrincipalAssetTypes for the slot is a subset of the target slot's.
	for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& SlotPair : InRuntimeSpec->Slots)
	{
		if (!SlotPair.Value.IsVirtual())
		{
			continue;
		}

		const FMetaHumanCharacterPipelineSlotEditorData* SlotEditorEntry = SlotEditorData.Find(SlotPair.Key);
		if (!SlotEditorEntry)
		{
			// No editor-only types declared for this slot; nothing to validate
			continue;
		}

		const FMetaHumanCharacterPipelineSlotEditorData* TargetEditorEntry = SlotEditorData.Find(SlotPair.Value.TargetSlot);
		if (!TargetEditorEntry)
		{
			if (SlotEditorEntry->SupportedPrincipalAssetTypes.IsEmpty())
			{
				continue;
			}

			// Target has no editor-only data but this slot claims to support asset types
			return false;
		}

		for (const TSoftClassPtr<UObject>& SupportedType : SlotEditorEntry->SupportedPrincipalAssetTypes)
		{
			const UClass* SupportedClass = SupportedType.LoadSynchronous();
			if (!SupportedClass)
			{
				continue;
			}

			if (!SlotSupportsAssetType(*TargetEditorEntry, SupportedClass))
			{
				return false;
			}
		}
	}

	return true;
}
