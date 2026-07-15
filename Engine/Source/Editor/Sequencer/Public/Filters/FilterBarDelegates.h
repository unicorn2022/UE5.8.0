// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "MVVM/ViewModelPtr.h"

namespace UE::Sequencer
{
class IOutlinerExtension;

DECLARE_MULTICAST_DELEGATE_TwoParams(FChangeOutlinerExtensionFilterState, const TViewModelPtr<IOutlinerExtension>&, bool bIsFilteredOut);
}