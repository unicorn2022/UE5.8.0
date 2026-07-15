// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMassSharedAnimTrackPool.h"
#include "Animation/AnimSequenceTransformProviderData.h"

//-----------------------------------------------------------------------------
// FSharedAnimTrackPool
//-----------------------------------------------------------------------------

void FSharedAnimTrackPool::Initialize(UAnimSequenceTransformProviderDataInstance* ASTPDI, double WorldTime)
{
	if (bInitialized || !ASTPDI)
	{
		return;
	}

	// Clamp configuration to safe values in case they were set programmatically or via config
	SteadyStateTracksPerSequence = FMath::Max(1, SteadyStateTracksPerSequence);
	MaxBlendTracks = FMath::Max(0, MaxBlendTracks);

	const TArray<FAnimSequenceTransformProviderSequence>& Sequences = ASTPDI->GetSequences();
	SequenceTracks.SetNum(Sequences.Num());

	// Store sequence lengths but don't allocate steady-state tracks yet.
	// They'll be created on-demand when an entity first needs a given sequence.
	for (int32 SeqIdx = 0; SeqIdx < Sequences.Num(); ++SeqIdx)
	{
		SequenceTracks[SeqIdx].SequenceLength = ASTPDI->GetSequencePlayLength(SeqIdx);
	}

	// Initialize blend track free list
	ActiveBlendTracks.SetNum(MaxBlendTracks);
	FreeBlendSlots.Reserve(MaxBlendTracks);
	for (int32 i = MaxBlendTracks - 1; i >= 0; --i)
	{
		FreeBlendSlots.Add(i);
	}

	bInitialized = true;
}

void FSharedAnimTrackPool::EnsureSteadyStateTracksForSequence(
	UAnimSequenceTransformProviderDataInstance* ASTPDI,
	int32 SequenceIndex,
	double WorldTime)
{
	const int32 LayerIndex = 0;

	if (!SequenceTracks.IsValidIndex(SequenceIndex) || !ASTPDI)
	{
		return;
	}

	FSequenceTracks& SeqTracks = SequenceTracks[SequenceIndex];
	if (!SeqTracks.SteadyStateTracks.IsEmpty())
	{
		// Already allocated
		return;
	}

	SeqTracks.SteadyStateTracks.SetNum(SteadyStateTracksPerSequence);
	for (int32 PhaseIdx = 0; PhaseIdx < SteadyStateTracksPerSequence; ++PhaseIdx)
	{
		FSteadyStateTrack& SSTrack = SeqTracks.SteadyStateTracks[PhaseIdx];
		SSTrack.PhaseOffset = static_cast<float>(PhaseIdx) / static_cast<float>(SteadyStateTracksPerSequence);

		FAnimSequenceTrackAutoPlayData TrackData;
		TrackData.SequenceIndex = SequenceIndex;
		TrackData.Position = SSTrack.PhaseOffset * SeqTracks.SequenceLength;
		TrackData.PlayRate = 1.0f;
		TrackData.BlendTime = 0.0f;
		TrackData.LoopMode = EAnimSequenceTrackLoopMode::Loop;

		SSTrack.TrackIndex = ASTPDI->AllocateTrack();
		ASTPDI->SetAutoPlayData(SSTrack.TrackIndex, LayerIndex, TrackData);
		SSTrack.ReferenceTimestamp = WorldTime - static_cast<double>(TrackData.Position / TrackData.PlayRate);
	}
}

int32 FSharedAnimTrackPool::FindBestSteadyStateTrack(
	UAnimSequenceTransformProviderDataInstance* ASTPDI,
	int32 SequenceIndex,
	const FMassEntityHandle& EntityHandle,
	double WorldTime)
{
	const int32 LayerIndex = 0;

	if (!SequenceTracks.IsValidIndex(SequenceIndex))
	{
		return INDEX_NONE;
	}

	// Lazy-allocate steady-state tracks for this sequence on first use
	EnsureSteadyStateTracksForSequence(ASTPDI, SequenceIndex, WorldTime);

	const FSequenceTracks& SeqTracks = SequenceTracks[SequenceIndex];
	if (SeqTracks.SteadyStateTracks.IsEmpty() || SeqTracks.SequenceLength <= 0.0f)
	{
		return INDEX_NONE;
	}

	// Deterministic per-entity: same entity always maps to the same steady-state track
	const int32 Index = GetTypeHash(EntityHandle) % SeqTracks.SteadyStateTracks.Num();
	return Index;
}

int32 FSharedAnimTrackPool::UpdateEntityTrack(
	UAnimSequenceTransformProviderDataInstance* ASTPDI,
	const FMassEntityHandle& EntityHandle,
	const FAnimSequenceTrackAutoPlayData& AnimData,
	double WorldTime)
{
	const int32 LayerIndex = 0;

	if (!bInitialized || !ASTPDI)
	{
		// Fallback: allocate per-entity track (original behavior)
		const int32 TrackIndex = ASTPDI ? ASTPDI->AllocateTrack() : INDEX_NONE;
		if (TrackIndex != INDEX_NONE)
		{
			ASTPDI->SetAutoPlayData(TrackIndex, LayerIndex, AnimData);
		}
		return TrackIndex;
	}

	bool bEntityExists = false;
	Experimental::FHashElementId ElementId = EntityStates.FindOrAddId(EntityHandle, FEntityTrackState(), bEntityExists);
	FEntityTrackState& State = EntityStates.GetByElementId(ElementId).Value;

	const bool bSequenceChanged = State.CurrentSequenceIndex != AnimData.SequenceIndex;
	const bool bIsNewEntity = !bEntityExists;

	if (bIsNewEntity)
	{
		// New entity: assign directly to a steady-state track (no blend needed on spawn)
		const int32 SSIndex = FindBestSteadyStateTrack(ASTPDI, AnimData.SequenceIndex, EntityHandle, WorldTime);
		if (SSIndex != INDEX_NONE && SequenceTracks.IsValidIndex(AnimData.SequenceIndex))
		{
			State.CurrentTrackIndex = SequenceTracks[AnimData.SequenceIndex].SteadyStateTracks[SSIndex].TrackIndex;
			State.CurrentSequenceIndex = AnimData.SequenceIndex;
			State.bIsBlending = false;
			State.BlendTrackSlot = INDEX_NONE;
			return State.CurrentTrackIndex;
		}
		// Fallback
		State.CurrentTrackIndex = ASTPDI->AllocateTrack();
		ASTPDI->SetAutoPlayData(State.CurrentTrackIndex, LayerIndex, AnimData);
		State.CurrentSequenceIndex = AnimData.SequenceIndex;
		return State.CurrentTrackIndex;
	}

	if (bSequenceChanged)
	{
		// Sequence transition: try to allocate a blend track
		if (State.bIsBlending && State.BlendTrackSlot != INDEX_NONE)
		{
			// Already blending — recycle the existing blend track
			ASTPDI->SetAutoPlayData(State.CurrentTrackIndex, LayerIndex, AnimData);
		}
		else if (!FreeBlendSlots.IsEmpty())
		{
			// Allocate a new blend track
			const int32 BlendSlot = FreeBlendSlots.Pop();
			FBlendTrack& Blend = ActiveBlendTracks[BlendSlot];
			Blend.TrackIndex = ASTPDI->AllocateTrack();
			ASTPDI->SetAutoPlayData(Blend.TrackIndex, LayerIndex, AnimData);
			Blend.OwnerEntity = EntityHandle;

			// Choose target steady-state track: pick one whose phase we'll be near when blend completes
			const double BlendCompleteTime = WorldTime + static_cast<double>(AnimData.BlendTime);
			Blend.TargetSteadyStateIndex = FindBestSteadyStateTrack(ASTPDI, AnimData.SequenceIndex, EntityHandle, BlendCompleteTime);
			Blend.BlendCompleteWorldTime = BlendCompleteTime;

			State.CurrentTrackIndex = Blend.TrackIndex;
			State.bIsBlending = true;
			State.BlendTrackSlot = BlendSlot;
		}
		else
		{
			// No blend tracks available: snap directly to steady-state (graceful degradation)
			const int32 SSIndex = FindBestSteadyStateTrack(ASTPDI, AnimData.SequenceIndex, EntityHandle, WorldTime);
			if (SSIndex != INDEX_NONE && SequenceTracks.IsValidIndex(AnimData.SequenceIndex))
			{
				State.CurrentTrackIndex = SequenceTracks[AnimData.SequenceIndex].SteadyStateTracks[SSIndex].TrackIndex;
				State.bIsBlending = false;
				State.BlendTrackSlot = INDEX_NONE;
			}
		}
		State.CurrentSequenceIndex = AnimData.SequenceIndex;
		return State.CurrentTrackIndex;
	}

	// No change — return current track
	return State.CurrentTrackIndex;
}

void FSharedAnimTrackPool::RemoveEntity(
	UAnimSequenceTransformProviderDataInstance* ASTPDI,
	const FMassEntityHandle& EntityHandle)
{
	const int32 LayerIndex = 0;

	Experimental::FHashElementId ElementId = EntityStates.FindId(EntityHandle);
	if (!ElementId.IsValid())
	{
		return;
	}

	FEntityTrackState& State = EntityStates.GetByElementId(ElementId).Value;

	// Free blend track if entity was mid-blend
	if (State.bIsBlending && State.BlendTrackSlot != INDEX_NONE)
	{
		FBlendTrack& Blend = ActiveBlendTracks[State.BlendTrackSlot];
		if (ASTPDI && Blend.TrackIndex != INDEX_NONE)
		{
			ASTPDI->DeallocateTrack(Blend.TrackIndex);
		}
		Blend = FBlendTrack();
		FreeBlendSlots.Add(State.BlendTrackSlot);
	}
	// Steady-state tracks are NOT deallocated — they're shared and persistent

	EntityStates.RemoveByElementId(ElementId);
}

void FSharedAnimTrackPool::ProcessCompletedBlends(
	UAnimSequenceTransformProviderDataInstance* ASTPDI,
	double WorldTime,
	TFunctionRef<void(const FMassEntityHandle&, int32)> OnTrackChanged)
{
	const int32 LayerIndex = 0;

	for (int32 SlotIdx = 0; SlotIdx < ActiveBlendTracks.Num(); ++SlotIdx)
	{
		FBlendTrack& Blend = ActiveBlendTracks[SlotIdx];
		if (Blend.TrackIndex == INDEX_NONE)
		{
			continue;
		}

		if (WorldTime >= Blend.BlendCompleteWorldTime)
		{
			// Blend complete — switch entity to steady-state track
			Experimental::FHashElementId EntityElementId = EntityStates.FindId(Blend.OwnerEntity);
			if (EntityElementId.IsValid())
			{
				FEntityTrackState& State = EntityStates.GetByElementId(EntityElementId).Value;
				// Find the target steady-state track
				int32 NewTrackIndex = INDEX_NONE;
				if (Blend.TargetSteadyStateIndex != INDEX_NONE
					&& SequenceTracks.IsValidIndex(State.CurrentSequenceIndex)
					&& SequenceTracks[State.CurrentSequenceIndex].SteadyStateTracks.IsValidIndex(Blend.TargetSteadyStateIndex))
				{
					NewTrackIndex = SequenceTracks[State.CurrentSequenceIndex].SteadyStateTracks[Blend.TargetSteadyStateIndex].TrackIndex;
				}

				// Always clear blending state — the blend track is being freed regardless
				State.bIsBlending = false;
				State.BlendTrackSlot = INDEX_NONE;

				if (NewTrackIndex != INDEX_NONE)
				{
					State.CurrentTrackIndex = NewTrackIndex;

					// Notify caller to update the ISKM instance's AnimationIndex
					OnTrackChanged(Blend.OwnerEntity, NewTrackIndex);
				}
				else if (ASTPDI)
				{
					// Steady-state lookup failed — allocate a fresh per-entity track so we
					// never leave State.CurrentTrackIndex pointing at the blend track we're
					// about to deallocate.
					FAnimSequenceTrackAutoPlayData FallbackData;
					FallbackData.SequenceIndex = State.CurrentSequenceIndex;
					FallbackData.PlayRate = 1.0f;
					FallbackData.BlendTime = 0.0f;
					FallbackData.LoopMode = EAnimSequenceTrackLoopMode::Loop;
					State.CurrentTrackIndex = ASTPDI->AllocateTrack();
					ASTPDI->SetAutoPlayData(State.CurrentTrackIndex, LayerIndex, FallbackData);
					OnTrackChanged(Blend.OwnerEntity, State.CurrentTrackIndex);
				}
			}

			// Free the blend track
			if (ASTPDI)
			{
				ASTPDI->DeallocateTrack(Blend.TrackIndex);
			}
			Blend = FBlendTrack();
			FreeBlendSlots.Add(SlotIdx);
		}
	}
}
