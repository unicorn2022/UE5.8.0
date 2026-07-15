// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterStickySelectionExtension.h"
#include "ControlRigStickySelectionFilterExtension.generated.h"

/** Extends FSequencerTrackFilter_StickySelection such that clicking control rig controls in the viewport clears sticky selection. */
UCLASS()
class UControlRigStickySelectionFilterExtension : public USequencerTrackFilterStickySelectionExtension
{
	GENERATED_BODY()
public:
	
	virtual bool IsPerformingSelectionThatShouldClearStickySelection(const ISequencer& InSequencer) override;
};
