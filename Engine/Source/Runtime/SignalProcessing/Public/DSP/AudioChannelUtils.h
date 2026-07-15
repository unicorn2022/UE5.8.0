// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioMixerChannel.h"

#define UE_API SIGNALPROCESSING_API

namespace Audio::ChannelUtils
{	
	/** Helper function to get the channel map type at the given index. */
	UE_API bool GetChannelTypeAtIndex(const int32 Index, EAudioMixerChannel::Type& OutType);
	
	/** Helper function to get the channel weight given a channel type. */
	UE_API float GetChannelWeight(EAudioMixerChannel::Type InType);
} // namespace Audio::ChannelUtils

#undef UE_API
