// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Templates/Function.h"

class ISequencer;

namespace UE::Sequencer
{
	class FChannelModel;
}

namespace UE::Sequencer::ToolableTimeline
{

/** @return True to filter out the FChannelModel from display keys in the timeline */
using FChannelFilterFunction = TFunction<bool(ISequencer&, const FChannelModel&)>;

/** Channel filter function instance */
struct FRegisteredChannelFilter
{
	FDelegateHandle Handle;
	FChannelFilterFunction FilterFunction;
};

} // namespace UE::Sequencer::ToolableTimeline
