// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Experimental/Containers/RobinHoodHashTable.h"
#include "Templates/Function.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Mass/EntityHandle.h"

class UAnimSequenceTransformProviderDataInstance;

/**
 * Manages shared ASTPD track allocation for ISKM animation.
 *
 * Instead of one track per entity, maintains:
 * - Steady-state tracks: N looping tracks per sequence at fixed phase offsets (persistent)
 * - Blend tracks: temporary per-entity tracks used during sequence transitions
 *
 * Entities in steady-state all share one of the N phase-offset tracks.
 * During a blend, an entity gets its own track. When the blend completes,
 * it switches to the nearest-phase steady-state track on the target sequence.
 *
 * Steady-state tracks are allocated on-demand per sequence — only sequences that
 * entities actually use get GPU tracks allocated.
 *
 * Graceful degradation: when blend tracks are exhausted, snap directly to
 * steady-state. Steady-state count is configurable (min 1 = full lockstep).
 */
struct FSharedAnimTrackPool
{
	static constexpr int32 DefaultSteadyStateTracksPerSequence = 3;
	static constexpr int32 DefaultMaxBlendTracks = 50;

	/** A steady-state looping track at a fixed phase offset */
	struct FSteadyStateTrack
	{
		int32 TrackIndex = INDEX_NONE;
		double ReferenceTimestamp = 0.0;
		float PhaseOffset = 0.0f;  // 0.0 to 1.0 within the sequence duration
	};

	/** A temporary blend track for a single entity */
	struct FBlendTrack
	{
		int32 TrackIndex = INDEX_NONE;
		FMassEntityHandle OwnerEntity;
		int32 TargetSteadyStateIndex = INDEX_NONE;  // which steady-state track to join after blend
		double BlendCompleteWorldTime = 0.0;  // when to switch to steady-state
	};

	/** Per-sequence steady-state tracks */
	struct FSequenceTracks
	{
		TArray<FSteadyStateTrack> SteadyStateTracks;
		float SequenceLength = 0.0f;
	};

	/** Per-entity tracking: which track (steady-state or blend) an entity is currently using */
	struct FEntityTrackState
	{
		int32 CurrentTrackIndex = INDEX_NONE;
		int32 CurrentSequenceIndex = INDEX_NONE;
		bool bIsBlending = false;
		int32 BlendTrackSlot = INDEX_NONE;  // index into ActiveBlendTracks, or INDEX_NONE if steady-state
	};

	/** Steady-state tracks indexed by sequence index */
	TArray<FSequenceTracks> SequenceTracks;

	/** Active blend tracks (fixed-size pool) */
	TArray<FBlendTrack> ActiveBlendTracks;
	TArray<int32> FreeBlendSlots;

	/** Per-entity state */
	Experimental::TRobinHoodHashMap<FMassEntityHandle, FEntityTrackState> EntityStates;

	/** Configuration */
	int32 SteadyStateTracksPerSequence = DefaultSteadyStateTracksPerSequence;
	int32 MaxBlendTracks = DefaultMaxBlendTracks;
	bool bInitialized = false;

	/** Initialize the pool structure for a given ASTPD. Does not allocate GPU tracks — those are on-demand. */
	void Initialize(UAnimSequenceTransformProviderDataInstance* ASTPDI, double WorldTime);

	/** Get or assign a track for an entity. Returns the TrackIndex to use as AnimationIndex. */
	int32 UpdateEntityTrack(
		UAnimSequenceTransformProviderDataInstance* ASTPDI,
		const FMassEntityHandle& EntityHandle,
		const FAnimSequenceTrackAutoPlayData& AnimData,
		double WorldTime);

	/** Remove an entity from the pool */
	void RemoveEntity(
		UAnimSequenceTransformProviderDataInstance* ASTPDI,
		const FMassEntityHandle& EntityHandle);

	/** Lazily allocate steady-state tracks for a sequence on first use */
	void EnsureSteadyStateTracksForSequence(
		UAnimSequenceTransformProviderDataInstance* ASTPDI,
		int32 SequenceIndex,
		double WorldTime);

	/** Find the best steady-state track for a given sequence.
	 *  Deterministic per-entity: the same entity always maps to the same track.
	 *  Allocates steady-state tracks on demand if they don't exist yet. */
	int32 FindBestSteadyStateTrack(
		UAnimSequenceTransformProviderDataInstance* ASTPDI,
		int32 SequenceIndex,
		const FMassEntityHandle& EntityHandle,
		double WorldTime);

	/** Process completed blends — switch entities from blend tracks to steady-state */
	void ProcessCompletedBlends(
		UAnimSequenceTransformProviderDataInstance* ASTPDI,
		double WorldTime,
		TFunctionRef<void(const FMassEntityHandle&, int32 /*NewTrackIndex*/)> OnTrackChanged);
};
