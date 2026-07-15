// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Mass/MetaHumanMassSharedAnimTrackPool.h"

#if WITH_AUTOMATION_TESTS

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — Construction & Configuration
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolDefaults,
	"MetaHumanMassCrowd.SharedAnimTrackPool.Defaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolDefaults::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;

	TestEqual(TEXT("Default SteadyStateTracksPerSequence"), Pool.SteadyStateTracksPerSequence, FSharedAnimTrackPool::DefaultSteadyStateTracksPerSequence);
	TestEqual(TEXT("Default MaxBlendTracks"), Pool.MaxBlendTracks, FSharedAnimTrackPool::DefaultMaxBlendTracks);
	TestFalse(TEXT("Should not be initialized by default"), Pool.bInitialized);
	TestEqual(TEXT("SequenceTracks should be empty"), Pool.SequenceTracks.Num(), 0);
	TestEqual(TEXT("ActiveBlendTracks should be empty"), Pool.ActiveBlendTracks.Num(), 0);
	TestEqual(TEXT("FreeBlendSlots should be empty"), Pool.FreeBlendSlots.Num(), 0);
	TestEqual(TEXT("EntityStates should be empty"), Pool.EntityStates.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolCustomConfig,
	"MetaHumanMassCrowd.SharedAnimTrackPool.CustomConfig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolCustomConfig::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	Pool.SteadyStateTracksPerSequence = 7;
	Pool.MaxBlendTracks = 100;

	TestEqual(TEXT("Custom SteadyStateTracksPerSequence"), Pool.SteadyStateTracksPerSequence, 7);
	TestEqual(TEXT("Custom MaxBlendTracks"), Pool.MaxBlendTracks, 100);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — Initialize without ASTPDI (null guard)
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolInitializeNull,
	"MetaHumanMassCrowd.SharedAnimTrackPool.Initialize.NullASTDP",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolInitializeNull::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	Pool.Initialize(nullptr, 0.0);

	TestFalse(TEXT("Should not initialize with null ASTPDI"), Pool.bInitialized);
	TestEqual(TEXT("SequenceTracks should remain empty"), Pool.SequenceTracks.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolInitializeIdempotent,
	"MetaHumanMassCrowd.SharedAnimTrackPool.Initialize.Idempotent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolInitializeIdempotent::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;

	// First call with null — should not initialize
	Pool.Initialize(nullptr, 0.0);
	TestFalse(TEXT("Should not be initialized after null"), Pool.bInitialized);

	// Force the flag to simulate already-initialized
	Pool.bInitialized = true;

	// Second call (even with null) should be a no-op due to bInitialized guard
	Pool.Initialize(nullptr, 1.0);
	TestTrue(TEXT("Should still be initialized"), Pool.bInitialized);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — UpdateEntityTrack fallback (uninitialized)
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolUpdateFallback,
	"MetaHumanMassCrowd.SharedAnimTrackPool.UpdateEntityTrack.Fallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolUpdateFallback::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	// Not initialized — should return INDEX_NONE with null ASTPDI
	FAnimSequenceTrackAutoPlayData AnimData;
	AnimData.SequenceIndex = 0;

	FMassEntityHandle Entity;
	int32 Result = Pool.UpdateEntityTrack(nullptr, Entity, AnimData, 0.0);
	TestEqual(TEXT("Should return INDEX_NONE with null ASTPDI"), Result, INDEX_NONE);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — RemoveEntity (empty pool)
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolRemoveNonexistent,
	"MetaHumanMassCrowd.SharedAnimTrackPool.RemoveEntity.Nonexistent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolRemoveNonexistent::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	FMassEntityHandle Entity;

	// Should not crash when removing from empty pool
	Pool.RemoveEntity(nullptr, Entity);
	TestEqual(TEXT("EntityStates should still be empty"), Pool.EntityStates.Num(), 0);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — ProcessCompletedBlends (empty pool)
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolProcessBlendsEmpty,
	"MetaHumanMassCrowd.SharedAnimTrackPool.ProcessCompletedBlends.Empty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolProcessBlendsEmpty::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	int32 CallbackCount = 0;

	// Should not crash on empty pool
	Pool.ProcessCompletedBlends(nullptr, 100.0, [&CallbackCount](const FMassEntityHandle&, int32)
	{
		++CallbackCount;
	});

	TestEqual(TEXT("Callback should not be called on empty pool"), CallbackCount, 0);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — EnsureSteadyStateTracksForSequence (null guards)
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolEnsureSSNull,
	"MetaHumanMassCrowd.SharedAnimTrackPool.EnsureSteadyState.NullGuards",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolEnsureSSNull::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;

	// Should not crash with null ASTPDI
	Pool.EnsureSteadyStateTracksForSequence(nullptr, 0, 0.0);
	TestEqual(TEXT("SequenceTracks should remain empty"), Pool.SequenceTracks.Num(), 0);

	// Should not crash with out-of-range index
	Pool.SequenceTracks.SetNum(2);
	Pool.EnsureSteadyStateTracksForSequence(nullptr, 5, 0.0);
	TestEqual(TEXT("Out-of-range should not crash"), Pool.SequenceTracks.Num(), 2);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — FindBestSteadyStateTrack (invalid indices)
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolFindSSInvalid,
	"MetaHumanMassCrowd.SharedAnimTrackPool.FindBestSteadyState.InvalidIndex",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolFindSSInvalid::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	FMassEntityHandle DummyEntity(1, 1);

	// Empty pool — should return INDEX_NONE
	int32 Result = Pool.FindBestSteadyStateTrack(nullptr, 0, DummyEntity, 0.0);
	TestEqual(TEXT("Should return INDEX_NONE for empty pool"), Result, INDEX_NONE);

	// Valid index but null ASTPDI and zero-length sequence — should return INDEX_NONE
	Pool.SequenceTracks.SetNum(1);
	Pool.SequenceTracks[0].SequenceLength = 0.0f;
	Result = Pool.FindBestSteadyStateTrack(nullptr, 0, DummyEntity, 0.0);
	TestEqual(TEXT("Should return INDEX_NONE for zero-length sequence"), Result, INDEX_NONE);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — Blend slot bookkeeping
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolBlendSlotBookkeeping,
	"MetaHumanMassCrowd.SharedAnimTrackPool.BlendSlots.Bookkeeping",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolBlendSlotBookkeeping::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;
	Pool.MaxBlendTracks = 5;

	// Manually set up blend pool (simulating what Initialize does)
	Pool.ActiveBlendTracks.SetNum(5);
	Pool.FreeBlendSlots.Reserve(5);
	for (int32 i = 4; i >= 0; --i)
	{
		Pool.FreeBlendSlots.Add(i);
	}

	TestEqual(TEXT("Should have 5 free blend slots"), Pool.FreeBlendSlots.Num(), 5);

	// Simulate consuming all blend slots
	for (int32 i = 0; i < 5; ++i)
	{
		int32 Slot = Pool.FreeBlendSlots.Pop();
		TestTrue(TEXT("Slot should be valid"), Slot >= 0 && Slot < 5);
	}

	TestEqual(TEXT("Should have 0 free blend slots"), Pool.FreeBlendSlots.Num(), 0);
	TestTrue(TEXT("FreeBlendSlots should be empty"), Pool.FreeBlendSlots.IsEmpty());

	// Return some slots
	Pool.FreeBlendSlots.Add(2);
	Pool.FreeBlendSlots.Add(4);
	TestEqual(TEXT("Should have 2 free blend slots after returning"), Pool.FreeBlendSlots.Num(), 2);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — EntityState tracking
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolEntityStateTracking,
	"MetaHumanMassCrowd.SharedAnimTrackPool.EntityState.Tracking",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolEntityStateTracking::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool Pool;

	// Simulate adding entity states — use distinct handles (default FMassEntityHandle has Index=0, SerialNumber=0)
	FMassEntityHandle Entity1(1, 100);
	FMassEntityHandle Entity2(2, 200);
	bool bExists1 = false, bExists2 = false;

	Pool.EntityStates.FindOrAddId(Entity1, FSharedAnimTrackPool::FEntityTrackState(), bExists1);
	TestFalse(TEXT("Entity1 should not exist initially"), bExists1);

	Pool.EntityStates.FindOrAddId(Entity2, FSharedAnimTrackPool::FEntityTrackState(), bExists2);
	TestFalse(TEXT("Entity2 should not exist initially"), bExists2);

	TestEqual(TEXT("Should have 2 entity states"), Pool.EntityStates.Num(), 2);

	// Look up entity1 again
	Experimental::FHashElementId Id1Again = Pool.EntityStates.FindOrAddId(Entity1, FSharedAnimTrackPool::FEntityTrackState(), bExists1);
	TestTrue(TEXT("Entity1 should exist on second lookup"), bExists1);

	// Remove entity1
	Pool.EntityStates.RemoveByElementId(Id1Again);
	TestEqual(TEXT("Should have 1 entity state after removal"), Pool.EntityStates.Num(), 1);

	return true;
}

//----------------------------------------------------------------------//
// FSharedAnimTrackPool — Default state values
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolEntityStateDefaults,
	"MetaHumanMassCrowd.SharedAnimTrackPool.EntityState.Defaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolEntityStateDefaults::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool::FEntityTrackState State;

	TestEqual(TEXT("CurrentTrackIndex should be INDEX_NONE"), State.CurrentTrackIndex, INDEX_NONE);
	TestEqual(TEXT("CurrentSequenceIndex should be INDEX_NONE"), State.CurrentSequenceIndex, INDEX_NONE);
	TestFalse(TEXT("bIsBlending should be false"), State.bIsBlending);
	TestEqual(TEXT("BlendTrackSlot should be INDEX_NONE"), State.BlendTrackSlot, INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolBlendTrackDefaults,
	"MetaHumanMassCrowd.SharedAnimTrackPool.BlendTrack.Defaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolBlendTrackDefaults::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool::FBlendTrack Track;

	TestEqual(TEXT("TrackIndex should be INDEX_NONE"), Track.TrackIndex, INDEX_NONE);
	TestEqual(TEXT("TargetSteadyStateIndex should be INDEX_NONE"), Track.TargetSteadyStateIndex, INDEX_NONE);
	TestEqual(TEXT("BlendCompleteWorldTime should be 0"), Track.BlendCompleteWorldTime, 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedAnimTrackPoolSteadyStateTrackDefaults,
	"MetaHumanMassCrowd.SharedAnimTrackPool.SteadyStateTrack.Defaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSharedAnimTrackPoolSteadyStateTrackDefaults::RunTest(const FString& Parameters)
{
	FSharedAnimTrackPool::FSteadyStateTrack Track;

	TestEqual(TEXT("TrackIndex should be INDEX_NONE"), Track.TrackIndex, INDEX_NONE);
	TestEqual(TEXT("ReferenceTimestamp should be 0"), Track.ReferenceTimestamp, 0.0);
	TestEqual(TEXT("PhaseOffset should be 0"), Track.PhaseOffset, 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
