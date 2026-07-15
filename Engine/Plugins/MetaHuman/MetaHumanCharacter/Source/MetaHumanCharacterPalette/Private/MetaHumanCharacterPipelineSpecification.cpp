// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterPipelineSpecification.h"

const FName UE::MetaHuman::CharacterPipelineSlots::Character = "Character";
const FName UE::MetaHuman::CharacterPipelineSlots::Hair = "Hair";
const FName UE::MetaHuman::CharacterPipelineSlots::Eyebrows = "Eyebrows";
const FName UE::MetaHuman::CharacterPipelineSlots::Beard = "Beard";
const FName UE::MetaHuman::CharacterPipelineSlots::Mustache = "Mustache";
const FName UE::MetaHuman::CharacterPipelineSlots::Eyelashes = "Eyelashes";
const FName UE::MetaHuman::CharacterPipelineSlots::Peachfuzz = "Peachfuzz";

bool FMetaHumanCharacterPipelineSlot::IsVirtual() const
{
	return TargetSlot != NAME_None;
}

bool UMetaHumanCharacterPipelineSpecification::IsValid() const
{
	// The set of slots that are known not to be part of any cycles in the virtual slot graph
	TSet<FName> AcyclicSlots;
	AcyclicSlots.Reserve(Slots.Num());

	for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& Slot : Slots)
	{
		if (Slot.Key == NAME_None)
		{
			// All slots must have non-empty names
			return false;
		}

		if (!Slot.Value.bAllowsMultipleSelection)
		{
			// Ensure there isn't more than one virtual slot targeting this slot

			bool bFoundVirtualSlot = false;
			for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& OtherSlot : Slots)
			{
				if (OtherSlot.Key == Slot.Key)
				{
					// Only process other slots
					continue;
				}

				if (OtherSlot.Value.TargetSlot == Slot.Key)
				{
					if (bFoundVirtualSlot)
					{
						// Multiple slots target this slot, which doesn't allow multiple selections.
						//
						// This isn't currently supported.
						return false;
					}

					// Found one virtual slot targeting this slot. Not a problem unless we find a second one.
					bFoundVirtualSlot = true;
				}
			}
		}

		if (Slot.Value.TargetSlot != NAME_None)
		{
			const FMetaHumanCharacterPipelineSlot* FoundTargetSlot = Slots.Find(Slot.Value.TargetSlot);
			if (!FoundTargetSlot)
			{
				return false;
			}

			// The target slot must be able to accept all valid selections made on this slot, otherwise
			// this slot is invalid.

			if (Slot.Value.bAllowsMultipleSelection && !FoundTargetSlot->bAllowsMultipleSelection)
			{
				// This slot allows multiple selection, but the target slot doesn't
				return false;
			}

			// Check for cycles in the virtual slot graph
			{
				TSet<FName, DefaultKeyFuncs<FName>, TInlineSetAllocator<8>> VisitedSlots;
			
				FName CurrentSlotName = Slot.Key;
				const FMetaHumanCharacterPipelineSlot* CurrentSlot = &Slot.Value;

				while (CurrentSlot->IsVirtual())
				{
					if (AcyclicSlots.Contains(CurrentSlotName))
					{
						// Reached a known acyclic slot, so there are no cycles reachable from here
						break;
					}

					if (VisitedSlots.Contains(CurrentSlotName))
					{
						// There is a cycle in the graph of virtual slots
						return false;
					}

					VisitedSlots.Add(CurrentSlotName);
				
					CurrentSlotName = CurrentSlot->TargetSlot;
					CurrentSlot = Slots.Find(CurrentSlot->TargetSlot);
					
					if (!CurrentSlot)
					{
						// Target doesn't exist
						return false;
					}
				}

				AcyclicSlots.Append(VisitedSlots);
			}
		}
	}

	return true;
}

TOptional<FName> UMetaHumanCharacterPipelineSpecification::ResolveRealSlotName(FName SlotName) const
{
	FName CurrentSlotName = SlotName;
	const FMetaHumanCharacterPipelineSlot* CurrentSlot;

	// This loop should always terminate, because any cycles in the virtual slot graph will be
	// detected by UMetaHumanCharacterPipelineSpecification::IsValid, which should be called 
	// before this function.
	while (true)
	{
		CurrentSlot = Slots.Find(CurrentSlotName);
		if (!CurrentSlot)
		{
			// Slot couldn't be found by name
			return TOptional<FName>();
		}

		if (!CurrentSlot->IsVirtual())
		{
			// We've reached the end of the chain and CurrentSlotName is now set to a real slot
			return CurrentSlotName;
		}

		CurrentSlotName = CurrentSlot->TargetSlot;
	}
}
