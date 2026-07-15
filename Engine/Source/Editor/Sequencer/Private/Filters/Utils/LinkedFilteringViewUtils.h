// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"

namespace UE::Sequencer
{
class ILinkedFilterViewModel;

/** Utility for binding ToggleLinkedFiltering to FExecuteAction. */
void ToggleLinkedFiltering(TWeakPtr<ILinkedFilterViewModel> InWeakViewModel);
/** Utility for binding ToggleLinkedFiltering to FIsActionChecked. */
bool IsLinkedFilteringActionChecked(TWeakPtr<ILinkedFilterViewModel> InWeakViewModel);
}
