// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ISequencer;
class UMovieSceneAnimationMixerTrack;

// Editor-side bus utilities that require ISequencer for binding resolution.
namespace AnimMixerEditorBusUtils
{
	// Gather all mixer tracks across the sequence hierarchy that are bound to
	// the same object as the given track. Resolves the track's binding through
	// the compiled hierarchy (not just the focused sequence) so this works
	// correctly for tracks in any sub-sequence.
	TArray<UMovieSceneAnimationMixerTrack*> GatherMixerTracksForSameObject(
		UMovieSceneAnimationMixerTrack* Track, ISequencer& Sequencer);
}
