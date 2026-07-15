// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"

class SWidget;

namespace UE::Sequencer::ToolableTimeline
{

struct FMouseInputData;

/**  */
class FToolableTimelineKeyContextMenu
{
public:
	static TSharedRef<SWidget> GenerateWidget(const FMouseInputData& InMouseInput);

protected:

};

} // namespace UE::Sequencer::ToolableTimeline
