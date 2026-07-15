// Copyright Epic Games, Inc. All Rights Reserved.

#include "ILinkedFilterViewModel.h"

#include "Filters/ISequencerTrackFilters.h"
#include "Filters/SequencerFilterData.h"

namespace UE::Sequencer
{
bool ILinkedFilterViewModel::IsFilteredOut(const TViewModelPtr<IOutlinerExtension>& InItem) const
{
	return GetActiveFilterBar()->GetFilterData().IsFilteredOut(InItem);
}
} // namespace UE::Sequencer
