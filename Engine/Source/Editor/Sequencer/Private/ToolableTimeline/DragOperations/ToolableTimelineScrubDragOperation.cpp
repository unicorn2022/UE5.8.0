// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolableTimelineScrubDragOperation.h"
#include "ToolableTimeline/MouseInputData.h"

namespace UE::Sequencer::ToolableTimeline
{

FToolableTimelineScrubDragOperation::FToolableTimelineScrubDragOperation(const FMouseInputData& InMouseInput
	, const TRange<double>& InViewRange)
	: FToolableTimelineDragOperation(InMouseInput, InViewRange)
{
}

void FToolableTimelineScrubDragOperation::UpdateDrag(const FMouseInputData& InMouseInput)
{
	FToolableTimelineDragOperation::UpdateDrag(InMouseInput);
}

void FToolableTimelineScrubDragOperation::ResetDrag()
{
}

} // UE::Sequencer::ToolableTimeline
