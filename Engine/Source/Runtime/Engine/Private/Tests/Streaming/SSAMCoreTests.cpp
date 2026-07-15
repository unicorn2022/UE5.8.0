// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================================
// FSimpleStreamableAssetManager (SSAM) — Core Test Suite
//
// Tests for the lightweight streaming asset manager that replaces the legacy
// FStreamingManagerTexture path for computing per-asset screen sizes.
//
// Tests:
// ┌────────┬─────────────────────────────────────────────────────┬────────────────────────────────────────────────────────┬──────────────────────────┐
// │ Group  │ Test                                                │ What it verifies                                       │ Registration path        │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 1      │ Registration.SingleObject                           │ 1 component, 2 assets → valid indices + bounds         │ Primary (proxy)          │
// │ 1      │ Registration.MultipleObjectsSharingAsset            │ 2 components share 1 asset → 2 bounds                  │ Primary (proxy)          │
// │ 1      │ Registration.MultipleAssetsPerObject                │ 1 component, 5 assets → unique indices                 │ Primary (proxy)          │
// │ 1      │ Registration.AssetDeduplication                     │ Same asset twice → merged to 1 bound (highest texel)   │ Primary (proxy)          │
// │ 1      │ Registration.ZeroTexelFactorFiltered                │ TexelFactor=0 → filtered out, no registration          │ Primary (proxy)          │
// │ 1      │ Registration.DoubleRegisterWithoutUnregister        │ Register twice, no unregister → self-heal, 1 bound     │ Primary (proxy)          │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 2      │ Unregistration.Object                               │ DestroyComponent → shared index reset, bounds cleared  │ Primary (proxy)          │
// │ 2      │ Unregistration.OneOfTwoObjects                      │ Destroy 1 of 2 → 1 bound remains                       │ Primary (proxy)          │
// │ 2      │ Unregistration.SameFrame                            │ Register+destroy before Process → clean state          │ Primary (proxy)          │
// │ 2      │ Unregistration.Asset                                │ UnregisterAsset → asset index freed                    │ Primary (proxy)          │
// │ 2      │ Unregistration.InstanceRemovedTimestamp             │ Unregister → InstanceRemovedTimestamp                  │ Primary (proxy)          │
// │ 2      │ Unregistration.RecoverMissedUnregisterPath          │ BeginDestroy w/ live handle → recovery path runs       │ Primary (proxy)          │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 2b     │ AutoRTFM.ReleaseAssetHandleCommitInvalidates        │ OnCommit-deferred Release fires → handle stale         │ Asset handle (no world)  │
// │ 2b     │ AutoRTFM.ReleaseAssetHandleAbortPreserves           │ OnCommit-deferred Release skipped → handle live        │ Asset handle (no world)  │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 3      │ Fallback.Registration                               │ Proxy w/o streaming → fallback component path          │ Fallback (component)     │
// │ 3      │ Fallback.DoubleRegistrationBug                      │ RecreateRenderState double-register guard              │ Fallback (component)     │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 4      │ StreamingQuery.MathCorrectness                      │ Exact screen size vs bUseNewMetrics formula            │ Primary (proxy)          │
// │ 4      │ StreamingQuery.RelativeCorrectness                  │ Nearer > farther, higher texel > lower, 2x = 2x        │ Primary (proxy)          │
// │ 4      │ StreamingQuery.UpdatesOnMove                        │ Far view → near view → screen size increases           │ Primary (proxy)          │
// │ 4      │ StreamingQuery.ForceMipStreaming                    │ bForceMipStreaming → MaxSize = FLT_MAX                 │ Primary (proxy)          │
// │ 4      │ StreamingQuery.VisibilityBased                      │ ConsiderVisibility CVar → VisibleOnly = 0 if unrendered│ Primary (proxy)          │
// │ 4      │ StreamingQuery.ViewInsideBounds                     │ Camera inside box → dist=0, max quality                │ Primary (proxy)          │
// │ 4      │ StreamingQuery.MultipleViews                        │ Split-screen: result = max across views                │ Primary (proxy)          │
// │ 4      │ StreamingQuery.MaxDrawDistance                      │ Beyond CachedMaxDrawDistance → VisibleOnly = 0         │ Primary (proxy)          │
// │ 4      │ StreamingQuery.ScaleFactor                          │ 2x world scale → >2x screen size (scale + extents)     │ Primary (proxy)          │
// │ 4      │ StreamingQuery.BoostFactor                          │ 2x ExtraBoost → 2x VisibleOnly                         │ Primary (proxy)          │
// │ 4      │ StreamingQuery.ObjectMovement                       │ SetWorldLocation + FUpdate path → bounds update        │ Primary (proxy) + RT     │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 5      │ ComponentScale.FlagOptOut                           │ Texture flag=false on TS=2 comp → no *TS multiply      │ Primary (proxy)          │
// │ 5      │ ComponentScale.FallbackComponentScale               │ Fallback path: TS=2 comp scales texel factor           │ Fallback (component)     │
// │ 5      │ ComponentScale.MeshTransformScaleNotApplied         │ Mesh query skips *ComponentScale (texture-only gate)   │ Primary (proxy)          │
// │ 5      │ ComponentScale.FallbackFlagOptOut                   │ Fallback texture flag=false → no *TS multiply          │ Fallback (component)     │
// │ 5      │ ComponentScale.FallbackMeshTransformScaleNotApplied │ Fallback mesh: AssetType gate keeps multiply off       │ Fallback (component)     │
// │ 5      │ ComponentScale.MeshScaleViaBoundsOnly               │ Mesh: scale=2 → ratio ~1.125 (distance bonus only)     │ Primary (proxy)          │
// │ 5      │ ComponentScale.FallbackMeshScaleViaBoundsOnly       │ Fallback mesh: scale=2 → ratio ~1.125 (no *TS)         │ Fallback (component)     │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 6      │ Lifecycle.EmptyProcess                              │ Process() with no pending records → safe no-op         │ N/A                      │
// │ 6      │ Lifecycle.IndexRecyclingNoLeak                      │ Freed indices reused, memory stays bounded             │ Primary (proxy)          │
// │ 6      │ Lifecycle.RapidCreateDestroy                        │ 50 create/destroy cycles → no leak, no lingering bounds│ Primary (proxy)          │
// │ 6      │ Lifecycle.MemoryBloatOnLevelTransition              │ UE-363476: inner sparse arrays freed                   │ Primary (proxy)          │
// │ 6      │ Lifecycle.BulkRegisterUnregister                    │ 100 components, 3 assets → bulk add/remove correctness │ Primary (proxy)          │
// ├────────┼─────────────────────────────────────────────────────┼────────────────────────────────────────────────────────┼──────────────────────────┤
// │ 7      │ Concurrency.ParallelRegistration                    │ ParallelFor + AddPrimitive via FPrimitiveSceneDesc     │ FPrimitiveSceneDesc (GT) │
// │ 7      │ Concurrency.ConcurrentProcessAndRegister            │ BG Process() loop + GT register bursts (cooked build)  │ Primary (proxy)          │
// │ 7      │ Concurrency.ConcurrentProcessAndUnregister          │ BG Process() loop + GT DestroyComponent (cooked build) │ Primary (proxy)          │
// │ 7      │ Concurrency.ConcurrentProcessAndUpdate              │ BG Process() loop + GT moves via RT (cooked build)     │ Primary (proxy) + RT     │
// │ 7      │ Concurrency.TripleProducerConcurrency               │ BG Process + parallel GT desc-register + GT unregister │ Both (desc + proxy)      │
// │ 7      │ Concurrency.GarbageCollectionDuringProcess          │ BG Process() + GT CollectGarbage (real GC path)        │ Primary (proxy)          │
// │ 7      │ Concurrency.ConcurrentOperationsStress              │ 5 iterations of parallel add/remove (stress test)      │ FPrimitiveSceneDesc (GT) │
// └────────┴─────────────────────────────────────────────────────┴────────────────────────────────────────────────────────┴──────────────────────────┘
//
// Threading model:
// ┌──────────────────┬────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
// │ Thread           │ Role in SSAM                                                                                                           │
// ├──────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
// │ Game Thread      │ RegisterComponent/DestroyComponent push FRegister/FUnregister to MPMC queues.                                          │
// │                  │ SetWorldLocation triggers SendRenderTransform_Concurrent which enqueues an RT command.                                 │
// ├──────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
// │ Parallel GT      │ FastGeo calls CreateRenderState from parallel workers (ParallelFor + EParallelGameThread),                             │
// │ (EParallelGT)    │ which internally calls CreateSceneProxy → AddPrimitive → FRegister push.                                               │
// ├──────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
// │ Render Thread    │ Executes deferred commands from GT: UpdatePrimitiveTransform_RenderThread pushes FUpdate records to SSAM queues.       │
// │                  │ FlushRenderingCommands() synchronizes.                                                                                 │
// ├──────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
// │ Async Worker     │ In cooked builds, FDoWorkAsyncTask calls Process() every frame. Process() acquires CriticalSection,                    │
// │ (thread pool)    │ then drains all MPMC queues via ConsumeAll + updates bounds.                                                           │
// └──────────────────┴────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
//
// =============================================================================================================================================

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SSAMTestTypes.h"
#include "Streaming/SimpleStreamableAssetManager.h"
#include "ContentStreaming.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "Streaming/TextureInstanceView.h"
#include "Streaming/StreamingManagerTexture.h"
#include "Streaming/StreamingTexture.h"
#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "SceneInterface.h"
#include "PrimitiveSceneDesc.h"
#include "PrimitiveSceneProxyDesc.h"
#include "Tasks/Task.h"
#include "AutoRTFM.h"

#define SSAM_TEST_NAME "System.Engine.Streaming.SSAM"

namespace SSAMTests
{
	static const EAutomationTestFlags TestFlags =
		EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	// ===================================================================
	// GROUP 1: Registration (Primary Path)
	//
	// Tests the primary SSAM registration flow: component registration
	// triggers CreateSceneProxy → AddPrimitive → BatchAddPrimitivesInternal
	// → Register → FRegister(proxy) → queue → UpdateTask_Async.
	// All tests in this group use FSSAMTestSceneProxy with
	// bImplementsStreamableAssetGathering = true.
	// ===================================================================

	// -----------------------------------------------------------------------
	// Register one component with two assets. Verify that after Process(),
	// both assets get valid SSAM indices and GetAssetReferenceBounds
	// returns the component's bounds for each.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMSingleObjectRegistration, SSAM_TEST_NAME ".Registration.SingleObject", TestFlags)

	bool FSSAMSingleObjectRegistration::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* AssetA = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetB = Env.CreateTestAsset();

		const FBoxSphereBounds TestBounds(FVector(100.f, 200.f, 300.f), FVector(50.f), 50.f);

		Env.SpawnTestComponent({ { AssetA, 512.f }, { AssetB, 256.f } }, TestBounds);
		Env.ProcessSSAM();

		TestTrue(TEXT("AssetA has valid SSAM index"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetA->GetSSAMAssetHandle()));
		TestTrue(TEXT("AssetB has valid SSAM index"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetB->GetSSAMAssetHandle()));

		TArray<FBox> BoxesA, BoxesB;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(AssetA, BoxesA);
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(AssetB, BoxesB);
		TestEqual(TEXT("AssetA has 1 reference bound"), BoxesA.Num(), 1);
		TestEqual(TEXT("AssetB has 1 reference bound"), BoxesB.Num(), 1);

		return true;
	}

	// -----------------------------------------------------------------------
	// Two components reference the same asset. Verify that
	// GetAssetReferenceBounds returns two boxes and the asset index
	// is shared (assigned once, used by both bound elements).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMultipleObjectsSharingAsset, SSAM_TEST_NAME ".Registration.MultipleObjectsSharingAsset", TestFlags)

	bool FSSAMMultipleObjectsSharingAsset::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* SharedAsset = Env.CreateTestAsset();

		Env.SpawnTestComponent({ { SharedAsset, 512.f } }, FBoxSphereBounds(FVector(0.f), FVector(100.f), 100.f));
		Env.SpawnTestComponent({ { SharedAsset, 256.f } }, FBoxSphereBounds(FVector(500.f, 0.f, 0.f), FVector(100.f), 100.f));

		Env.ProcessSSAM();

		TestTrue(TEXT("SharedAsset has valid SSAM index"), FSimpleStreamableAssetManager::IsAssetRegistered(SharedAsset->GetSSAMAssetHandle()));

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(SharedAsset, Boxes);
		TestEqual(TEXT("SharedAsset has 2 reference bounds"), Boxes.Num(), 2);

		return true;
	}

	// -----------------------------------------------------------------------
	// Register a single component that references 5 different assets.
	// Verify each asset gets its own unique SSAM index and exactly one
	// reference bound.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMultipleAssetsPerObject, SSAM_TEST_NAME ".Registration.MultipleAssetsPerObject", TestFlags)

	bool FSSAMMultipleAssetsPerObject::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		constexpr int32 NumAssets = 5;
		TArray<USSAMTestStreamableAsset*> Assets;
		TArray<FSSAMTestAssetEntry> Entries;
		for (int32 i = 0; i < NumAssets; ++i)
		{
			USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
			Assets.Add(Asset);
			Entries.Add({ Asset, (float)(100 * (i + 1)) });
		}

		Env.SpawnTestComponent(Entries);
		Env.ProcessSSAM();

		TSet<int32> SeenIndices;
		for (int32 i = 0; i < NumAssets; ++i)
		{
			TestTrue(FString::Printf(TEXT("Asset[%d] has valid SSAM index"), i), FSimpleStreamableAssetManager::IsAssetRegistered(Assets[i]->GetSSAMAssetHandle()));

			bool bAlreadyInSet = false;
			SeenIndices.Add(FSimpleStreamableAssetManager::GetAssetRegistrationIndex(Assets[i]->GetSSAMAssetHandle()), &bAlreadyInSet);
			TestFalse(FString::Printf(TEXT("Asset[%d] index is unique"), i), bAlreadyInSet);

			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Assets[i], Boxes);
			TestEqual(FString::Printf(TEXT("Asset[%d] has 1 reference bound"), i), Boxes.Num(), 1);
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// Register a component that references the SAME asset twice with
	// different texel factors. With deduplication enabled (default CVar
	// GSimpleStreamableAssetManagerEnsureAssetUniqueOnRegistration = 1),
	// SSAM should merge them into a single bound element, keeping the
	// highest texel factor.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMAssetDeduplication, SSAM_TEST_NAME ".Registration.AssetDeduplication", TestFlags)

	bool FSSAMAssetDeduplication::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		// Guard: this test requires deduplication to be enabled (default CVar = 1).
		IConsoleVariable* DedupCVar = IConsoleManager::Get().FindConsoleVariable( TEXT("s.StreamableAssets.SimpleStreamableAssetManager.SortAssetsOnRegistration"));
		const int32 OriginalDedup = DedupCVar ? DedupCVar->GetInt() : 1;
		if (DedupCVar)
		{
			DedupCVar->Set(1, ECVF_SetByCode);
		}

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		Env.SpawnTestComponent({ { Asset, 128.f }, { Asset, 512.f } });
		Env.ProcessSSAM();

		TestTrue(TEXT("Asset has valid index"), FSimpleStreamableAssetManager::IsAssetRegistered(Asset->GetSSAMAssetHandle()));

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Deduplicated asset has exactly 1 reference bound"), Boxes.Num(), 1);

		if (DedupCVar)
		{
			DedupCVar->Set(OriginalDedup, ECVF_SetByCode);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Register a component with TexelFactor = 0. The FRegister constructor
	// filters assets via CanBeStreamedByDistance which requires
	// TexelFactor > UE_SMALL_NUMBER. An asset with zero texel factor
	// should NOT be registered (no bound elements, no index assigned).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMZeroTexelFactorFiltered, SSAM_TEST_NAME ".Registration.ZeroTexelFactorFiltered", TestFlags)

	bool FSSAMZeroTexelFactorFiltered::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		// Register with TexelFactor = 0 — should be filtered out
		Env.SpawnTestComponent({ { Asset, 0.f } });
		Env.ProcessSSAM();

		// The asset should NOT have been assigned an index (no streamable assets
		// passed the CanBeStreamedByDistance filter)
		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Zero texel factor asset has 0 bounds (filtered out)"), Boxes.Num(), 0);

		return true;
	}

	// ===================================================================
	// GROUP 2: Unregistration
	//
	// Tests all unregistration paths: object removal (DestroyComponent →
	// BatchRemovePrimitivesInternal → Unregister), same-frame
	// register+unregister, partial unregister (one of two objects), and
	// asset-level removal (UnregisterAsset, simulating texture unload).
	// ===================================================================

	// -----------------------------------------------------------------------
	// Register a component, process, then destroy it and process again.
	// Verify the component's shared index is reset and the asset's
	// reference bounds are cleaned up.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMUnregisterObject, SSAM_TEST_NAME ".Unregistration.Object", TestFlags)

	bool FSSAMUnregisterObject::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
		Env.ProcessSSAM();

		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(Asset->GetSSAMAssetHandle()));

		// Destroying the component triggers the full
		// BatchRemovePrimitivesInternal → Unregister flow
		Comp->DestroyComponent();
		Env.ProcessSSAM();

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has 0 bounds after unregister"), Boxes.Num(), 0);

		return true;
	}

	// -----------------------------------------------------------------------
	// Two components share one asset. Unregister one. Verify the asset
	// still has one reference bound from the remaining component.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMUnregisterOneOfTwoObjects, SSAM_TEST_NAME ".Unregistration.OneOfTwoObjects", TestFlags)

	bool FSSAMUnregisterOneOfTwoObjects::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* SharedAsset = Env.CreateTestAsset();

		USSAMTestComponent* Comp1 = Env.SpawnTestComponent({ { SharedAsset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		USSAMTestComponent* Comp2 = Env.SpawnTestComponent({ { SharedAsset, 256.f } }, FBoxSphereBounds(FVector(1000.f, 0.f, 0.f), FVector(100.f), 100.f));

		Env.ProcessSSAM();

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(SharedAsset, Boxes);
			TestEqual(TEXT("Asset has 2 bounds before partial unregister"), Boxes.Num(), 2);
		}

		Comp1->DestroyComponent();
		Env.ProcessSSAM();

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(SharedAsset, Boxes);
			TestEqual(TEXT("Asset has 1 bound after partial unregister"), Boxes.Num(), 1);
		}

		TestTrue(TEXT("SharedAsset still has valid SSAM index"), FSimpleStreamableAssetManager::IsAssetRegistered(SharedAsset->GetSSAMAssetHandle()));

		return true;
	}

	// -----------------------------------------------------------------------
	// Register a component, then immediately destroy it before calling
	// Process(). Both FRegister and FUnregister are processed in the same
	// UpdateTask_Async call. The processing order (assign index → unregister
	// → register) means the unregister resets the index to INDEX_NONE, then
	// RegisterRecord sees INDEX_NONE and early-returns. Net result: clean state.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMRegisterUnregisterSameFrame, SSAM_TEST_NAME ".Unregistration.SameFrame", TestFlags)

	bool FSSAMRegisterUnregisterSameFrame::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
		Comp->DestroyComponent();
		Env.ProcessSSAM();

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has 0 bounds after same-frame register+unregister"), Boxes.Num(), 0);

		return true;
	}

	// -----------------------------------------------------------------------
	// Register an object referencing an asset. Then call UnregisterAsset
	// on the asset itself (simulating texture unload). Verify the asset
	// index is freed and gets a fresh INDEX_NONE pointer, while the
	// object registration remains intact.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMUnregisterAsset, SSAM_TEST_NAME ".Unregistration.Asset", TestFlags)

	bool FSSAMUnregisterAsset::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
		Env.ProcessSSAM();

		TestTrue(TEXT("Asset has valid index"), FSimpleStreamableAssetManager::IsAssetRegistered(Asset->GetSSAMAssetHandle()));

		// Unregister the ASSET (not the object)
		FSimpleStreamableAssetManager::UnregisterAsset(Asset);
		Env.ProcessSSAM();

		// After UnregisterAsset, the asset's shared index should have been
		// replaced with a fresh INDEX_NONE pointer
		TestFalse(TEXT("Asset is unregistered after UnregisterAsset"), FSimpleStreamableAssetManager::IsAssetRegistered(Asset->GetSSAMAssetHandle()));

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has 0 bounds after UnregisterAsset"), Boxes.Num(), 0);

		return true;
	}

	// -----------------------------------------------------------------------
	// When all SSAM references to an asset are removed (component destroyed),
	// the streaming manager's InstanceRemovedTimestamp on the corresponding
	// FStreamingRenderAsset must be updated to the current time. Without this,
	// the unknown-ref heuristic incorrectly forces full mip loading for assets
	// that have no streaming references but are still rendered (e.g. cloud
	// textures used by volumetric rendering, which never go through component
	// registration).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMUnregisterInstanceRemovedTimestamp, SSAM_TEST_NAME ".Unregistration.InstanceRemovedTimestamp", TestFlags)

	bool FSSAMUnregisterInstanceRemovedTimestamp::RunTest(const FString& Parameters)
	{
		// This test requires the real FRenderAssetStreamingManager, which is only
		// created when r.TextureStreaming is enabled. Ensure it's on for this test.
		IConsoleVariable* TextureStreamingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TextureStreaming"));
		const int32 OriginalTextureStreaming = TextureStreamingCVar ? TextureStreamingCVar->GetInt() : 0;
		if (TextureStreamingCVar && !OriginalTextureStreaming)
		{
			TextureStreamingCVar->Set(1, ECVF_SetByCode);
			IStreamingManager::Get().Tick(0.f, true);
		}
		ON_SCOPE_EXIT
		{
			if (TextureStreamingCVar && !OriginalTextureStreaming)
			{
				TextureStreamingCVar->Set(0, ECVF_SetByCode);
				IStreamingManager::Get().Tick(0.f, true);
			}
		};

		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		// Set up fake streaming state so the streaming manager creates a
		// valid FStreamingRenderAsset (needs non-zero mip counts).
		Asset->SetupFakeStreamingState();
		Asset->SetStreamingIndexForTest(INDEX_NONE);

		FRenderAssetStreamingManager& StreamingMgr = static_cast<FRenderAssetStreamingManager&>(
			IStreamingManager::Get().GetRenderAssetStreamingManager());
		StreamingMgr.AddStreamingRenderAsset(Asset);

		// Register a component that references the asset through SSAM.
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
		Env.ProcessSSAM();

		// A single UpdateResourceStreaming flushes the pending asset into
		// StreamingRenderAssets (via ProcessAddedRenderAssets) and runs the
		// mip calc. At this point the asset has SSAM references so it's fine.
		StreamingMgr.UpdateResourceStreaming(0.f, true);

		FStreamingRenderAsset* StreamingAsset = StreamingMgr.GetStreamingRenderAsset(Asset);
		if (!TestNotNull(TEXT("FStreamingRenderAsset exists after flush"), StreamingAsset))
		{
			StreamingMgr.RemoveStreamingRenderAsset(Asset);
			return false;
		}

		// Record the timestamp before unregistration.
		const double TimestampBefore = StreamingAsset->InstanceRemovedTimestamp;

		// Advance FApp::GetCurrentTime() — it's a per-frame cached value that
		// doesn't advance with wall clock or Sleep. The mip calc uses it to
		// set InstanceRemovedTimestamp, so we need a visible delta.
		FAppTime::SetCurrentTime(FApp::GetCurrentTime() + 1.0);

		// Destroy the component, removing all SSAM references.
		Comp->DestroyComponent();
		Env.ProcessSSAM();

		// Run a second streaming update — the mip calc consumes the removed
		// asset indices and updates InstanceRemovedTimestamp.
		StreamingMgr.UpdateResourceStreaming(0.f, true);

		// Re-fetch: UpdateResourceStreaming may have reallocated the array.
		StreamingAsset = StreamingMgr.GetStreamingRenderAsset(Asset);
		if (!TestNotNull(TEXT("FStreamingRenderAsset still valid after streaming update"), StreamingAsset))
		{
			StreamingMgr.RemoveStreamingRenderAsset(Asset);
			return false;
		}

		// The core assertion: InstanceRemovedTimestamp must have been updated
		// so that the unknown-ref heuristic has a correct time baseline.
		TestTrue(TEXT("InstanceRemovedTimestamp updated after SSAM unregistration"),
			StreamingAsset->InstanceRemovedTimestamp > TimestampBefore);

		StreamingMgr.RemoveStreamingRenderAsset(Asset);
		return true;
	}

	// ===================================================================
	// GROUP 2b: AutoRTFM compatibility
	//
	// UPrimitiveComponent::BeginDestroy must be safe to run inside an AutoRTFM
	// transaction because Verse-driven NewObject<T>(SameOuter, "SameName") takes
	// the in-place reconstruction path in StaticConstructObject_Internal, which
	// synchronously calls ConditionalBeginDestroy on the existing instance.
	//
	// The fix wraps the SSAM cleanup in AutoRTFM::Open and defers the
	// RecoverMissedUnregister queue push via AutoRTFM::OnCommit so an aborting
	// transaction (which restores the component memory via RecordOpenWrite)
	// leaves the streaming arrays consistent with the restored handle.
	//
	// Two layers of regression coverage:
	//
	// 1. **Build-time** -- compiling SSAMCoreTests.cpp under the AutoRTFM clang
	//    fork (e.g. AutoRTFMEngineTests target) forces the compiler to emit a
	//    closed clone of every BeginDestroy reachable from the test code. If
	//    the SSAM atomics in BeginDestroy aren't wrapped in AutoRTFM::Open, the
	//    closed-clone emission asserts and the build fails.
	//
	// 2. **Runtime** (the tests below) -- gated on IsAutoRTFMRuntimeEnabled().
	//    Verifies the OnCommit-deferral contract our fix relies on: the queue
	//    push to RecoverMissedUnregister fires after a committed transaction
	//    and is skipped on abort. Driven via the component's actual SSAM
	//    handle so the test exercises real handle table state, but the
	//    component itself is left intact (its BeginDestroy runs at normal
	//    teardown time, outside any transaction) so the test world cleans
	//    up without tripping CheckAndHandleStaleWorldObjectReferences.
	// ===================================================================

	// Mirrors AUTORTFM_ACTOR_COMPONENT_TEST flags so these tests are picked up by
	// AutoRTFMEngineTests (which runs as a Program and gates on
	// ClientContext / ServerContext / CommandletContext), and adds EditorContext
	// so the regular UnrealEditor automation harness also discovers them
	// (where the IsAutoRTFMRuntimeEnabled() gate makes them skip cleanly).
	static const EAutomationTestFlags AutoRTFMTestFlags =
		EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ServerContext
		| EAutomationTestFlags::CommandletContext;

	// RAII enabler: AllocateAssetHandle / ReleaseAssetHandle check(IsEnabled())
	// and SSAM is off by default in AutoRTFMEngineTests. Set the CVar for the
	// scope of the test and restore on exit.
	struct FAutoRTFMTestSSAMEnabler
	{
		IConsoleVariable* CVar;
		int32 PreviousValue;

		FAutoRTFMTestSSAMEnabler()
			: CVar(IConsoleManager::Get().FindConsoleVariable(TEXT("s.StreamableAssets.UseSimpleStreamableAssetManager")))
			, PreviousValue(CVar ? CVar->GetInt() : 0)
		{
			if (CVar && PreviousValue == 0)
			{
				CVar->Set(1, ECVF_SetByCode);
			}
		}
		~FAutoRTFMTestSSAMEnabler()
		{
			if (CVar && PreviousValue == 0)
			{
				CVar->Set(0, ECVF_SetByCode);
			}
		}
	};

	// -----------------------------------------------------------------------
	// Committing transaction: the OnCommit-deferred ReleaseAssetHandle fires
	// after commit and the slot's generation advances, marking the captured
	// handle stale. Mirrors UStreamableRenderAsset::BeginDestroy's SSAM block.
	// Uses asset handles (process-lifetime, no world / Instance required)
	// because FSSAMTestEnvironment's FTestWorldWrapper teardown is not safe
	// in the AutoRTFMEngineTests Program target.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMReleaseAssetHandleCommitInvalidates, SSAM_TEST_NAME ".AutoRTFM.ReleaseAssetHandleCommitInvalidates", AutoRTFMTestFlags)

	bool FSSAMReleaseAssetHandleCommitInvalidates::RunTest(const FString& Parameters)
	{
		if (!AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
		{
			return true;
		}

		FAutoRTFMTestSSAMEnabler EnableSSAM;
		FSSAMAssetHandle Handle = FSimpleStreamableAssetManager::AllocateAssetHandle();
		TestTrue(TEXT("Handle is valid after Allocate"), Handle.IsValid());
		TestFalse(TEXT("Handle is not stale after Allocate"), Handle.IsStale());

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			// OnCommit must be registered from closed code -- from an open nest it runs
			// immediately, which would defeat the deferral.
			AutoRTFM::OnCommit([Handle]
			{
				FSimpleStreamableAssetManager::ReleaseAssetHandle(Handle);
			});
		});
		TestTrue(TEXT("Transaction committed"),
			Result == AutoRTFM::ETransactionResult::Committed);

		// Release fired -> the slot's generation advanced -> our captured handle is now stale.
		TestTrue(TEXT("Captured handle is stale after committed release"), Handle.IsStale());

		return true;
	}

	// -----------------------------------------------------------------------
	// Aborting transaction: the OnCommit-deferred ReleaseAssetHandle must NOT
	// fire. The handle stays live -- this is the property that prevents
	// BeginDestroy-in-aborting-transaction from leaving the SSAM handle
	// table inconsistent with the (restored) handle field.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMReleaseAssetHandleAbortPreserves, SSAM_TEST_NAME ".AutoRTFM.ReleaseAssetHandleAbortPreserves", AutoRTFMTestFlags)

	bool FSSAMReleaseAssetHandleAbortPreserves::RunTest(const FString& Parameters)
	{
		if (!AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
		{
			return true;
		}

		FAutoRTFMTestSSAMEnabler EnableSSAM;
		FSSAMAssetHandle Handle = FSimpleStreamableAssetManager::AllocateAssetHandle();
		TestTrue(TEXT("Handle is valid after Allocate"), Handle.IsValid());
		TestFalse(TEXT("Handle is not stale after Allocate"), Handle.IsStale());

		AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
		{
			// OnCommit must be registered from closed code -- from an open nest it runs
			// immediately, which would defeat the deferral.
			AutoRTFM::OnCommit([Handle]
			{
				FSimpleStreamableAssetManager::ReleaseAssetHandle(Handle);
			});
			AutoRTFM::AbortTransaction();
		});
		TestTrue(TEXT("Transaction aborted as requested"),
			Result == AutoRTFM::ETransactionResult::AbortedByRequest);

		// Release was deferred to commit -> abort skipped it -> handle still live.
		TestFalse(TEXT("Handle is not stale after aborted release"), Handle.IsStale());

		// Clean up to keep handle table tidy across runs.
		FSimpleStreamableAssetManager::ReleaseAssetHandle(Handle);

		return true;
	}

	// -----------------------------------------------------------------------
	// End-to-end coverage of UPrimitiveComponent::BeginDestroy's SSAM recovery
	// block. Spawns a registered component, fires ConditionalBeginDestroy with
	// the handle still live, and verifies the streaming arrays drop the
	// registration after ProcessSSAM. Exercises:
	//   1. AutoRTFM::Open captures the live handle (atomic Exchange)
	//   2. AutoRTFM::Open evaluates IsStale (atomic Generation load)
	//   3. ensureMsgf("missed Unregister") fires (silenced via AddExpectedError)
	//   4. AutoRTFM::OnCommit registered from closed code
	//   5. RecoverMissedUnregister queues an FUnregister
	//   6. ProcessSSAM drains -> streaming arrays drop the registration
	//
	// Wraps BeginDestroy in AutoRTFM::Transact when the AutoRTFM runtime is
	// enabled, so on AutoRTFM-compiled targets (Fortnite editor / server /
	// client) step 4 actually defers OnCommit until commit time -- exercising
	// the full deferred path. On non-AutoRTFM targets (UnrealEditor) the
	// Transact wrapper is effectively a no-op and OnCommit fires immediately.
	// -----------------------------------------------------------------------
	// Note on AutoRTFMEngineTests Program target: this test is intentionally NOT picked
	// up by that runner (no "AutoRTFM" in the path, no SupportsAutoRTFM flag). The
	// program target's lazy streaming-manager init combined with FSSAMTestEnvironment's
	// CheckAndHandleStaleWorldObjectReferences cleanup are incompatible. The deferred
	// OnCommit path is still covered there by FSSAMReleaseAssetHandle{Commit,Abort}*
	// using raw asset handles, and on Fortnite editor / server / client targets
	// (which compile with bUseAutoRTFMCompiler = true and have full world setup),
	// this test's Transact wrapping below exercises the full deferred path end-to-end.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMRecoverMissedUnregisterPath, SSAM_TEST_NAME ".Unregistration.RecoverMissedUnregisterPath", TestFlags)

	bool FSSAMRecoverMissedUnregisterPath::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;
		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
		Env.ProcessSSAM();

		const FSSAMPrimitiveHandle BeforeHandle = Comp->GetSSAMPrimitiveHandle();
		TestTrue(TEXT("Handle is live before BeginDestroy"),
			BeforeHandle.IsValid() && !BeforeHandle.IsStale());

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("Asset has 1 bound before BeginDestroy"), Boxes.Num(), 1);
		}

		// The "missed Unregister" ensureMsgf is the expected outcome of this path.
		// It emits multiple log lines across the LogOutputDevice and EnsureFailed
		// categories: the condition, the formatted message, "=== Handled ensure: ===",
		// "Stack:", blank lines, and a multi-line callstack. AddExpectedError with
		// Occurrences = -1 silently ignores any number of matches.
		AddExpectedError(TEXT("Ensure condition failed"),
			EAutomationExpectedErrorFlags::Contains, -1);
		AddExpectedError(TEXT("Component held a live SSAM handle"),
			EAutomationExpectedErrorFlags::Contains, -1);
		AddExpectedError(TEXT("Handled ensure"),
			EAutomationExpectedErrorFlags::Contains, -1);
		AddExpectedError(TEXT("Stack:"),
			EAutomationExpectedErrorFlags::Contains, -1);
		AddExpectedError(TEXT("\\[Callstack\\]"),
			EAutomationExpectedErrorFlags::Contains, -1);
		// ensureMsgf also emits two blank error lines as separators.
		AddExpectedError(TEXT("^$"),
			EAutomationExpectedErrorFlags::Contains, -1);

		// Drive BeginDestroy from inside a transaction when AutoRTFM is active so
		// the OnCommit handler in BeginDestroy actually defers to commit -- the
		// path the production fix is for. Outside AutoRTFM (or under it but
		// without the compiler), Transact runs the body directly and OnCommit
		// fires immediately. Either way, after the call returns the unregister
		// queue push has happened and ProcessSSAM will drain it.
		if (AutoRTFM::ForTheRuntime::IsAutoRTFMRuntimeEnabled())
		{
			AutoRTFM::ETransactionResult Result = AutoRTFM::Transact([&]
			{
				Comp->ConditionalBeginDestroy();
			});
			TestTrue(TEXT("Transaction committed"),
				Result == AutoRTFM::ETransactionResult::Committed);
		}
		else
		{
			Comp->ConditionalBeginDestroy();
		}

		Env.ProcessSSAM();

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Recovery path cleared registration"), Boxes.Num(), 0);

		return true;
	}

	// ===================================================================
	// GROUP 3: Fallback Path
	//
	// Tests the fallback registration path for proxies that do NOT
	// implement GetStreamableRenderAssetInfo (bImplementsStreamableAssetGathering
	// = false). Register pushes the component to
	// FallbackComponentRegistration, and ProcessFallbackRegistrations_GameThread
	// gathers assets via the component's GetStreamingRenderAssetInfo override.
	// ===================================================================

	// -----------------------------------------------------------------------
	// Use a proxy that does NOT implement GetStreamableRenderAssetInfo.
	// Register takes the fallback path, pushing the component to
	// FallbackComponentRegistration. ProcessFallbackRegistrations_GameThread
	// then gathers assets from the component (via GetStreamingRenderAssetInfo
	// override) and creates an FRegister record. Verify full end-to-end
	// registration including valid indices and asset bounds.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMFallbackRegistration, SSAM_TEST_NAME ".Fallback.Registration", TestFlags)

	bool FSSAMFallbackRegistration::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent({ { Asset, 512.f } });

		// Process the fallback queue (now a no-op) and the main queue
		Env.ProcessFallbackRegistrations();
		Env.ProcessSSAM();

		TestTrue(TEXT("Asset has valid index after registration"), FSimpleStreamableAssetManager::IsAssetRegistered(Asset->GetSSAMAssetHandle()));

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has 1 reference bound via fallback"), Boxes.Num(), 1);

		return true;
	}

	// -----------------------------------------------------------------------
	// Regression test for the fallback double-registration bug.
	//
	// RecreateRenderState_Concurrent unregisters the current proxy then creates a new one
	// synchronously. For a non-gathering (fallback) proxy, Register pushes an entry to
	// FallbackComponentRegistration on each call -- so two entries for the same component land
	// in the queue before ProcessFallbackRegistrations runs, each tagged with a different
	// handle (H1 from the first registration, H2 from the recreated one).
	//
	// Fix: each entry carries the handle it was pushed with, and the drain skips entries whose
	// handle no longer matches the component's current one. Only the latest entry (H2) produces
	// an FRegister record, so the asset ends up with exactly one bound.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMFallbackDoubleRegistrationBug, SSAM_TEST_NAME ".Fallback.DoubleRegistrationBug", TestFlags)

	bool FSSAMFallbackDoubleRegistrationBug::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent({ { Asset, 512.f } });

		// Re-create proxy before fallback queue is drained → two entries
		Comp->RecreateRenderState_Concurrent();

		// Now process both fallback entries + the main queue
		Env.ProcessFallbackRegistrations();
		Env.ProcessSSAM();

		// THE CRITICAL CHECK: the asset should have exactly 1 reference bound.
		// Without the fix this would be 2 (both stale entry and current entry registered).
		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has exactly 1 bound (no double registration)"), Boxes.Num(), 1);

		return true;
	}

	// -----------------------------------------------------------------------
	// Regression test for the double-register-without-unregister bug on
	// the primary (gathering) path.
	//
	// When Register is called twice on the same proxy without an
	// intervening Unregister, AssignPrimitiveHandle used to hit a
	// check(!Proxy->SSAMPrimitiveHandle.IsValid()) guard. The check compiled
	// out in shipping and the first registration leaked: its ObjectIndex
	// stayed in ObjectRegistrationIndexToAssetProperty, its asset-reference
	// bound stayed in the bounds array, and its handle slot stayed live in
	// the primitive handle table.
	//
	// Fix: AssignPrimitiveHandle now detects a live prior handle on the
	// proxy, logs a warning, unregisters the prior handle via
	// ClearPrimitiveHandle + Unregister(FUnregister), and then stamps the
	// new handle. After Process, exactly one bound remains and the prior
	// handle is stale.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMDoubleRegisterWithoutUnregister, SSAM_TEST_NAME ".Registration.DoubleRegisterWithoutUnregister", TestFlags)

	bool FSSAMDoubleRegisterWithoutUnregister::RunTest(const FString& Parameters)
	{
		// The self-heal path logs a warning for the contract violation. Expected, the automation harness doesn't count it as a test failure.
		AddExpectedError(TEXT("already has a live SSAMPrimitiveHandle"), EAutomationExpectedErrorFlags::Contains, 1);

		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
		Env.ProcessSSAM();

		FPrimitiveSceneProxy* Proxy = Comp->GetSceneProxy();
		TestNotNull(TEXT("Scene proxy exists after registration"), Proxy);
		if (!Proxy)
		{
			return false;
		}

		const FSSAMPrimitiveHandle FirstHandle = Proxy->GetSSAMPrimitiveHandle();
		TestTrue(TEXT("First registration produced a live handle"), FirstHandle.IsValid());

		// Second Register with no intervening Unregister. Both proxy- and
		// component-side handle fields advance in lockstep through the self-heal because
		// the same IPrimitiveComponent is passed.
		FSimpleStreamableAssetManager::Register(Comp->GetPrimitiveComponentInterface(), Proxy, Comp->GetRenderMatrix(), Comp->Bounds);
		Env.ProcessSSAM();

		const FSSAMPrimitiveHandle SecondHandle = Proxy->GetSSAMPrimitiveHandle();
		TestTrue(TEXT("Second registration produced a live handle"), SecondHandle.IsValid());
		TestTrue(TEXT("Second handle differs from first"), SecondHandle != FirstHandle);
		TestTrue(TEXT("First handle is stale after self-heal"), FirstHandle.IsStale());

		// The asset's bounds array must not grow: without the fix it contains
		// two entries (the leaked first registration and the new one).
		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has exactly 1 bound (no leaked prior registration)"), Boxes.Num(), 1);

		return true;
	}

	// ===================================================================
	// GROUP 4: Streaming Queries
	//
	// Tests the streaming query pipeline: UpdateBoundSizes computes
	// per-object screen sizes from view info, then GetRenderAssetScreenSize
	// multiplies by texel factor to produce the final streaming priority.
	// These tests verify the math is correct, inputs respond in the right
	// direction, and special modes (force mip, visibility) work.
	// ===================================================================

	// -----------------------------------------------------------------------
	// Validate exact screen size against the bUseNewMetrics formula.
	//
	// With bUseNewMetrics (default), distance is computed as distance-to-box:
	//   For each axis: dist = max(0, abs(ViewOrigin[i] - Origin[i]) - Extent[i])
	//   DistSq = sum(dist[i]²)
	//   ClampedDistSq = max(1, max(MinDistanceSq, DistSq))
	//   NormalizedSize = ScreenSize / sqrt(ClampedDistSq)
	//
	// Then GetRenderAssetScreenSize multiplies by TexelFactor * ComponentScale:
	//   MaxSize = TexelFactor * ComponentScale * NormalizedSize
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMScreenSizeMathCorrectness, SSAM_TEST_NAME ".StreamingQuery.MathCorrectness", TestFlags)

	bool FSSAMScreenSizeMathCorrectness::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		// Guard: this test's expected values assume bUseNewMetrics = true (default).
		// The old-metrics path uses a different distance formula.
		IConsoleVariable* MetricsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.UseNewMetrics"));
		const int32 OriginalMetrics = MetricsCVar ? MetricsCVar->GetInt() : 1;
		if (MetricsCVar)
		{
			MetricsCVar->Set(1, ECVF_SetByCode);
		}

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		const FVector BoundsExtent(100.f, 100.f, 100.f);
		const float TexelFactor = 512.f;
		const FBoxSphereBounds Bounds(FVector::ZeroVector, BoundsExtent, BoundsExtent.Size());

		Env.SpawnTestComponent({ { Asset, TexelFactor } }, Bounds);
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		// View at (1000, 0, 0), ScreenSize = 1920
		const float ScreenSizeParam = 1920.f;
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f), ScreenSizeParam);
		FSSAMTestEnvironment::FScreenSizeResult Result = Env.QueryScreenSize(AssetHandle);

		// Expected: distance-to-box along X = |1000 - 0| - min(|1000|, 100) = 900
		//           NormalizedSize = 1920 / 900 = 2.1333
		//           MaxSize = 512 * 1.0 * 2.1333 = 1092.27
		const float DistToBox = 1000.f - 100.f;
		const float ExpectedMaxSize = TexelFactor * 1.0f * (ScreenSizeParam / DistToBox);

		TestTrue(FString::Printf(TEXT("MaxSize (%.2f) matches expected (%.2f) within 1%%"), Result.MaxSize, ExpectedMaxSize),
			FMath::IsNearlyEqual(Result.MaxSize, ExpectedMaxSize, ExpectedMaxSize * 0.01f));

		if (MetricsCVar)
		{
			MetricsCVar->Set(OriginalMetrics, ECVF_SetByCode);
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Verify that screen size responds correctly to each input dimension:
	// - Nearer object → larger screen size than farther object (same texel)
	// - Higher texel factor → larger screen size (same position)
	// - 2x texel factor → exactly 2x screen size (proportionality)
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMScreenSizeRelativeCorrectness, SSAM_TEST_NAME ".StreamingQuery.RelativeCorrectness", TestFlags)

	bool FSSAMScreenSizeRelativeCorrectness::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* AssetNear = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetFar = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetHighTexel = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetLowTexel = Env.CreateTestAsset();

		const float R = 100.f;
		const FBoxSphereBounds NearBounds(FVector::ZeroVector, FVector(R), R);
		const FBoxSphereBounds FarBounds(FVector(2000.f, 0.f, 0.f), FVector(R), R);

		Env.SpawnTestComponent({ { AssetNear, 512.f } }, NearBounds);      // Object A: at origin
		Env.SpawnTestComponent({ { AssetFar, 512.f } }, FarBounds);        // Object B: at (2000, 0, 0)
		Env.SpawnTestComponent({ { AssetHighTexel, 1024.f } }, NearBounds); // Object C: at origin, 2x texel
		Env.SpawnTestComponent({ { AssetLowTexel, 256.f } }, NearBounds);  // Object D: at origin, 0.5x texel
		Env.ProcessSSAM();

		// View at (500, 0, 0) — closer to origin objects than to far object
		Env.UpdateBoundSizes(FVector(500.f, 0.f, 0.f));

		const float SizeNear = Env.QueryScreenSize(AssetNear->GetSSAMAssetHandle()).MaxSize;
		const float SizeFar = Env.QueryScreenSize(AssetFar->GetSSAMAssetHandle()).MaxSize;
		const float SizeHighTexel = Env.QueryScreenSize(AssetHighTexel->GetSSAMAssetHandle()).MaxSize;
		const float SizeLowTexel = Env.QueryScreenSize(AssetLowTexel->GetSSAMAssetHandle()).MaxSize;

		// Distance test: nearer object → larger screen size
		TestTrue(FString::Printf(TEXT("Nearer (%.2f) > Farther (%.2f)"), SizeNear, SizeFar), SizeNear > SizeFar);

		// Texel factor test: higher texel factor → larger screen size (same position)
		TestTrue(FString::Printf(TEXT("HighTexel (%.2f) > LowTexel (%.2f)"), SizeHighTexel, SizeLowTexel), SizeHighTexel > SizeLowTexel);

		// Proportionality: double the texel factor → double the screen size
		// AssetHighTexel (1024) vs AssetNear (512), both at origin
		const float Ratio = SizeHighTexel / SizeNear;
		TestTrue(FString::Printf(TEXT("Texel factor 2x ratio is ~2.0 (actual: %.3f)"), Ratio), FMath::IsNearlyEqual(Ratio, 2.0f, 0.01f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Register a component, compute screen size with a far view, then
	// recompute with a near view. Verify screen size increases when the
	// view moves closer. This tests the UpdateBoundSizes → query round-trip.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMScreenSizeUpdatesOnMove, SSAM_TEST_NAME ".StreamingQuery.UpdatesOnMove", TestFlags)

	bool FSSAMScreenSizeUpdatesOnMove::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		Env.UpdateBoundSizes(FVector(5000.f, 0.f, 0.f));
		const float SizeFar = Env.QueryScreenSize(AssetHandle).MaxSize;
		Env.UpdateBoundSizes(FVector(500.f, 0.f, 0.f));
		const float SizeNear = Env.QueryScreenSize(AssetHandle).MaxSize;

		TestTrue(TEXT("Far screen size is positive"), SizeFar > 0.f);
		TestTrue(TEXT("Near screen size is positive"), SizeNear > 0.f);
		TestTrue(TEXT("Closer view produces larger screen size"), SizeNear > SizeFar);

		return true;
	}

	// -----------------------------------------------------------------------
	// Register a component with bForceMipStreaming = true. Verify that
	// GetRenderAssetScreenSize returns MaxSize = FLT_MAX regardless of
	// distance. This exercises the force-load code path in ProcessElement
	// (line 487: if bForceLoad && NormalizedSize > 0 → MaxSize = FLT_MAX).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMForceMipStreaming, SSAM_TEST_NAME ".StreamingQuery.ForceMipStreaming", TestFlags)

	bool FSSAMForceMipStreaming::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } }, Bounds);

		// Set bForceMipStreaming and re-register so the proxy picks up the flag
		Comp->bForceMipStreaming = true;
		Comp->MarkRenderStateDirty();
		Comp->RecreateRenderState_Concurrent();
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		// Place view very far away — force mip should still return FLT_MAX
		Env.UpdateBoundSizes(FVector(100000.f, 0.f, 0.f));
		FSSAMTestEnvironment::FScreenSizeResult Result = Env.QueryScreenSize(AssetHandle);

		TestEqual(TEXT("ForceMipStreaming produces FLT_MAX screen size"), Result.MaxSize, FLT_MAX);

		return true;
	}

	// -----------------------------------------------------------------------
	// When GSimpleStreamableAssetManagerConsiderVisibility is enabled,
	// MaxNormalizedSize_VisibleOnly is only non-zero for objects with
	// a recent LastRenderTime (> LastUpdateTime). Verify that:
	// - An object that was never rendered has MaxSize_VisibleOnly = 0
	// - MaxSize (distance-based, not gated by visibility) is still positive
	//
	// The visibility check at UpdateBoundSizes line 428-431:
	//   if (bConsiderVisibility)
	//     ScreenSizeOverDistance = select(LastRenderTime > LastUpdateTime, ..., 0)
	// Our object's LastRenderTime defaults to -1000 (from FUpdate), which
	// is NOT greater than LastUpdateTime=0.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMVisibilityBasedStreaming, SSAM_TEST_NAME ".StreamingQuery.VisibilityBased", TestFlags)

	bool FSSAMVisibilityBasedStreaming::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		// Enable visibility consideration for this test
		IConsoleVariable* VisibilityCVar = IConsoleManager::Get().FindConsoleVariable( TEXT("s.StreamableAssets.SimpleStreamableAssetManager.ConsiderVisibility"));
		TestNotNull(TEXT("ConsiderVisibility CVar exists"), VisibilityCVar);
		const int32 OriginalValue = VisibilityCVar->GetInt();
		VisibilityCVar->Set(1, ECVF_SetByCode);

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		Env.UpdateBoundSizes(FVector(500.f, 0.f, 0.f));
		FSSAMTestEnvironment::FScreenSizeResult Result = Env.QueryScreenSize(AssetHandle);

		TestTrue(TEXT("MaxSize is positive for non-visible object"), Result.MaxSize > 0.f);
		TestEqual(TEXT("MaxSize_VisibleOnly is 0 for never-rendered object"), Result.MaxSizeVisibleOnly, 0.f);

		// Restore CVar
		VisibilityCVar->Set(OriginalValue, ECVF_SetByCode);
		return true;
	}

	// -----------------------------------------------------------------------
	// When the camera is INSIDE the object's bounding box, distance-to-box
	// is 0 for all axes. ClampedDistSq = max(1, 0) = 1, giving
	// NormalizedSize = ScreenSize / 1 = ScreenSize (near-maximum).
	// This is the first-person rendering edge case — objects the player is
	// standing inside must stream at highest quality.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMViewInsideBounds, SSAM_TEST_NAME ".StreamingQuery.ViewInsideBounds", TestFlags)

	bool FSSAMViewInsideBounds::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		const float TexelFactor = 512.f;
		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(500.f), 500.f);
		Env.SpawnTestComponent({ { Asset, TexelFactor } }, Bounds);
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		// View at origin — inside the 500-unit bounding box
		const float ScreenSizeParam = 1920.f;
		Env.UpdateBoundSizes(FVector::ZeroVector, ScreenSizeParam);
		FSSAMTestEnvironment::FScreenSizeResult Result = Env.QueryScreenSize(AssetHandle);

		// Distance-to-box = 0 on all axes → ClampedDistSq = max(1, 0) = 1
		// NormalizedSize = ScreenSize / sqrt(1) = 1920
		// MaxSize = TexelFactor * 1.0 * 1920 = 983040
		const float ExpectedMaxSize = TexelFactor * ScreenSizeParam;
		TestTrue(FString::Printf(TEXT("View inside bounds: MaxSize (%.2f) matches expected (%.2f) within 1%%"), Result.MaxSize, ExpectedMaxSize),
			FMath::IsNearlyEqual(Result.MaxSize, ExpectedMaxSize, ExpectedMaxSize * 0.01f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Multiple simultaneous views (split-screen scenario). The screen size
	// should be the MAX across all views. If view A is far and view B is
	// near, the result should match view B's (larger) screen size.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMultipleViews, SSAM_TEST_NAME ".StreamingQuery.MultipleViews", TestFlags)

	bool FSSAMMultipleViews::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		const float TexelFactor = 512.f;
		Env.SpawnTestComponent({ { Asset, TexelFactor } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		// Single near view for reference
		Env.UpdateBoundSizes(FVector(500.f, 0.f, 0.f));
		const float SizeNearOnly = Env.QueryScreenSize(AssetHandle).MaxSize;

		// Two views — one far (10000), one near (500)
		const FVector MultiViewOrigins[] = { FVector(10000.f, 0.f, 0.f), FVector(500.f, 0.f, 0.f) };
		Env.UpdateBoundSizes(MultiViewOrigins);
		const float MaxSizeMulti = Env.QueryScreenSize(AssetHandle).MaxSize;

		// Multi-view result should match the near view (which dominates)
		TestTrue(FString::Printf(TEXT("Multi-view (%.2f) matches near-only (%.2f) within 1%%"), MaxSizeMulti, SizeNearOnly),
			FMath::IsNearlyEqual(MaxSizeMulti, SizeNearOnly, SizeNearOnly * 0.01f));

		return true;
	}

	// -----------------------------------------------------------------------
	// MaxDrawDistance: when the view is beyond an object's max draw distance,
	// the InRangeMask check in UpdateBoundSizes zeros out
	// MaxNormalizedSize_VisibleOnly (the object is out of range).
	// Verify that MaxSize_VisibleOnly = 0 when the view is out of range.
	// Note: MaxNormalizedSize (non-visibility-gated) is NOT range-checked
	// in the current implementation — it always uses raw distance.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMaxDrawDistance, SSAM_TEST_NAME ".StreamingQuery.MaxDrawDistance", TestFlags)

	bool FSSAMMaxDrawDistance::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		// Set max draw distance = 1000 on the component
		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } }, Bounds);
		Comp->CachedMaxDrawDistance = 1000.f;

		// Re-register so the proxy picks up the draw distance
		Comp->MarkRenderStateDirty();
		Comp->RecreateRenderState_Concurrent();
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		// View at 500 — within max draw distance (1000)
		Env.UpdateBoundSizes(FVector(500.f, 0.f, 0.f));
		TestTrue(TEXT("Within range: MaxSize_VisibleOnly is positive"), Env.QueryScreenSize(AssetHandle).MaxSizeVisibleOnly > 0.f);

		// View at 5000 — beyond max draw distance (1000)
		Env.UpdateBoundSizes(FVector(5000.f, 0.f, 0.f));
		TestEqual(TEXT("Beyond range: MaxSize_VisibleOnly is 0"), Env.QueryScreenSize(AssetHandle).MaxSizeVisibleOnly, 0.f);

		return true;
	}

	// -----------------------------------------------------------------------
	// StreamingScaleFactor: when CanApplyStreamableRenderAssetScaleFactor
	// returns true, the render matrix's max axis scale is used as
	// ComponentScale. A 2x scaled object should produce 2x screen size
	// compared to an unscaled one (both at the same position with the
	// same texel factor).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMStreamingScaleFactor, SSAM_TEST_NAME ".StreamingQuery.ScaleFactor", TestFlags)

	bool FSSAMStreamingScaleFactor::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* AssetUnscaled = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetScaled = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);

		// Unscaled component — scale factor = 1.0
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent({ { AssetUnscaled, 512.f } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(1.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		// 2x scaled component — scale factor = 2.0
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent({ { AssetScaled, 512.f } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessSSAM();

		TestTrue(TEXT("Unscaled asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetUnscaled->GetSSAMAssetHandle()));
		TestTrue(TEXT("Scaled asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetScaled->GetSSAMAssetHandle()));

		// Same view for both
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeUnscaled = Env.QueryScreenSize(AssetUnscaled->GetSSAMAssetHandle()).MaxSize;
		const float SizeScaled = Env.QueryScreenSize(AssetScaled->GetSSAMAssetHandle()).MaxSize;

		TestTrue(TEXT("Unscaled screen size is positive"), SizeUnscaled > 0.f);
		TestTrue(TEXT("Scaled screen size is positive"), SizeScaled > 0.f);

		// 2x scale increases ComponentScale (2x) AND reduces distance-to-box
		// (larger extents), so the ratio should be > 2.0
		const float Ratio = SizeScaled / SizeUnscaled;
		TestTrue(FString::Printf(TEXT("2x scale produces ratio > 2.0 (actual: %.3f)"), Ratio), Ratio > 2.0f);

		return true;
	}

	// -----------------------------------------------------------------------
	// BoostFactor: FStreamingViewInfo::BoostFactor affects
	// ExtraBoostForVisiblePrimitive in ViewInfoExtras. Verify that a 2x
	// boost doubles MaxSize_VisibleOnly compared to 1x boost.
	// Note: BoostFactor affects the VisibleOnly path only (multiplied by
	// ExtraBoostForVisiblePrimitive at line 425).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMBoostFactor, SSAM_TEST_NAME ".StreamingQuery.BoostFactor", TestFlags)

	bool FSSAMBoostFactor::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f), 1920.f, 1.0f);
		const float VisibleOnly_1x = Env.QueryScreenSize(AssetHandle).MaxSizeVisibleOnly;

		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f), 1920.f, 2.0f);
		const float VisibleOnly_2x = Env.QueryScreenSize(AssetHandle).MaxSizeVisibleOnly;

		TestTrue(TEXT("VisibleOnly with 1x boost is positive"), VisibleOnly_1x > 0.f);
		TestTrue(TEXT("VisibleOnly with 2x boost is positive"), VisibleOnly_2x > 0.f);

		const float Ratio = VisibleOnly_2x / VisibleOnly_1x;
		TestTrue(FString::Printf(TEXT("2x boost ratio is ~2.0 (actual: %.3f)"), Ratio), FMath::IsNearlyEqual(Ratio, 2.0f, 0.1f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Object movement via FUpdate path: register an object, then move the
	// component (triggering a transform update → FUpdate record). Verify
	// that GetAssetReferenceBounds reflects the new position and screen
	// size changes accordingly.
	// In production, UpdatePrimitiveTransform_RenderThread pushes FUpdate
	// records when a component's transform changes.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMObjectMovementUpdate, SSAM_TEST_NAME ".StreamingQuery.ObjectMovement", TestFlags)

	bool FSSAMObjectMovementUpdate::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		// Object starts at origin
		USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f));
		Env.ProcessSSAM();

		const FSSAMAssetHandle AssetHandle = Asset->GetSSAMAssetHandle();
		TestTrue(TEXT("Asset registered"), FSimpleStreamableAssetManager::IsAssetRegistered(AssetHandle));

		// View at (1000, 0, 0)
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));
		const float SizeAtOrigin = Env.QueryScreenSize(AssetHandle).MaxSize;
		TestTrue(TEXT("Screen size at origin is positive"), SizeAtOrigin > 0.f);

		// Move the component closer to the view (to 800, 0, 0).
		// SetWorldLocation updates ComponentToWorld. SendRenderTransform_Concurrent
		// then recalculates bounds (CalcBounds with new transform) and calls
		// Scene->UpdatePrimitiveTransform, which ENQUEUES a render command
		// (RendererScene.cpp line 1687). The RT command pushes an FUpdate record
		// to the SSAM MPMC queue (line 1702). FlushRenderingCommands is REQUIRED
		// to ensure the RT executes that command before ProcessSSAM drains the queue.
		// Without the flush, the FUpdate would never reach SSAM.
		//
		// Note: SendRenderTransform_Concurrent is called directly here (not through
		// the engine's end-of-frame update path). This bypasses the
		// FPrimitiveTransformUpdater TLS accumulation (RendererScene.cpp line 1675)
		// which is only active during SendAllEndOfFrameUpdates.
		Comp->SetWorldLocation(FVector(800.f, 0.f, 0.f));
		Comp->SendRenderTransform_Concurrent();
		FlushRenderingCommands();
		Env.ProcessSSAM();

		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));
		const float SizeAfterMove = Env.QueryScreenSize(AssetHandle).MaxSize;
		TestTrue(TEXT("Screen size after move is positive"), SizeAfterMove > 0.f);

		// Object moved from origin to (800,0,0), view is at (1000,0,0)
		// Distance decreased from 900 (1000-100) to 100 (200-100)
		// Screen size should be much larger
		TestTrue(FString::Printf(TEXT("Screen size increased after moving closer: %.2f > %.2f"), SizeAfterMove, SizeAtOrigin), SizeAfterMove > SizeAtOrigin);

		return true;
	}

	// ===================================================================
	// GROUP 5: ComponentScale
	//
	// Tests for SSAM's component-scale handling at query time. Covers:
	// - The primary (scene-proxy) and fallback (UPrimitiveComponent) paths
	// - The per-entry FStreamingRenderAssetPrimitiveInfo::bAffectedByComponentScale
	//   opt-out flag (textures only)
	// - The AssetType-keyed gate that excludes meshes from the *ComponentScale
	//   multiply (their TexelFactor already encodes physical world size)
	// ===================================================================

	// -----------------------------------------------------------------------
	// FStreamingRenderAssetPrimitiveInfo::bAffectedByComponentScale=false on a
	// texture entry should opt OUT of the query-time component-scale multiply.
	// SSAM should NOT multiply TexelFactor by the component's transform scale
	// when the flag is false.
	//
	// Setup: two components both scaled 2x with bSupportScaleFactor=true.
	// AssetA registered with flag=true (default); AssetB registered with
	// flag=false. Identical TexelFactor and bounds.
	//
	// Expected: SizeFlagTrue ~ 2 * SizeFlagFalse (the only delta is the
	// extra ComponentScale multiply for the flag=true entry).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMComponentScaleFlagOptOut, SSAM_TEST_NAME ".ComponentScale.FlagOptOut", TestFlags)

	bool FSSAMComponentScaleFlagOptOut::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* AssetFlagTrue = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetFlagFalse = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		// 2x-scaled component, asset entry flag=true (should be scaled at query)
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent(
				{ FSSAMTestAssetEntry{ AssetFlagTrue, TexelFactor, /*bAffectedByComponentScale*/ true } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		// 2x-scaled component, asset entry flag=false (should NOT be scaled at query)
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent(
				{ FSSAMTestAssetEntry{ AssetFlagFalse, TexelFactor, /*bAffectedByComponentScale*/ false } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeFlagTrue = Env.QueryScreenSize(AssetFlagTrue->GetSSAMAssetHandle()).MaxSize;
		const float SizeFlagFalse = Env.QueryScreenSize(AssetFlagFalse->GetSSAMAssetHandle()).MaxSize;

		TestTrue(TEXT("flag=true MaxSize > 0"), SizeFlagTrue > 0.f);
		TestTrue(TEXT("flag=false MaxSize > 0"), SizeFlagFalse > 0.f);

		// Both share identical bounds, view, and TexelFactor. The only difference is whether
		// the per-entry flag opts the query-time *ComponentScale (=2) multiply in or out.
		// Verifies SSAM honors bAffectedByComponentScale: flag=true gets the multiply, flag=false
		// does not, so the ratio is 2.0.
		const float Ratio = SizeFlagTrue / SizeFlagFalse;
		TestTrue(FString::Printf(TEXT("flag=true vs flag=false ratio is ~2.0 (actual: %.3f)"), Ratio),
			FMath::IsNearlyEqual(Ratio, 2.0f, 0.05f));

		return true;
	}

	// -----------------------------------------------------------------------
	// SSAM fallback path (UPrimitiveComponent route via
	// ProcessFallbackRegistrations_GameThread) should apply component
	// transform scale to texture TexelFactor at query time, exactly like the
	// primary scene-proxy path does.
	//
	// Setup: two fallback components on the same view ray. One unscaled, one
	// scaled 2x. Identical TexelFactor and bounds (pre-scale).
	//
	// Expected: ratio (scaled / unscaled) > 2.0 (the component-scale multiply
	// is applied to the scaled component's TexelFactor; the larger bounds
	// also reduce distance-to-box, giving an additional bonus).
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMFallbackComponentScale, SSAM_TEST_NAME ".ComponentScale.FallbackComponentScale", TestFlags)

	bool FSSAMFallbackComponentScale::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* AssetUnscaled = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetScaled = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		// Unscaled fallback component (TS=1)
		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent({ FSSAMTestAssetEntry{ AssetUnscaled, TexelFactor } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(1.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		// 2x-scaled fallback component
		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent({ FSSAMTestAssetEntry{ AssetScaled, TexelFactor } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessFallbackRegistrations();
		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeUnscaled = Env.QueryScreenSize(AssetUnscaled->GetSSAMAssetHandle()).MaxSize;
		const float SizeScaled = Env.QueryScreenSize(AssetScaled->GetSSAMAssetHandle()).MaxSize;

		TestTrue(TEXT("unscaled MaxSize > 0"), SizeUnscaled > 0.f);
		TestTrue(TEXT("scaled MaxSize > 0"), SizeScaled > 0.f);

		// Mirrors the primary-path FSSAMStreamingScaleFactor expectation: the 2x scale + the
		// extents-driven distance-to-box bonus together produce ratio > 2.0. Verifies the
		// fallback path correctly plumbs GetStreamingScale via FRegister(Component, ...), so
		// the query-time ComponentScale multiply contributes the expected 2x.
		const float Ratio = SizeScaled / SizeUnscaled;
		TestTrue(FString::Printf(TEXT("fallback path: 2x scale produces ratio > 2.0 (actual: %.3f)"), Ratio), Ratio > 2.0f);

		return true;
	}

	// -----------------------------------------------------------------------
	// Mesh-typed assets (StaticMesh/SkeletalMesh) must NOT receive the
	// query-time ComponentScale multiply. Their TexelFactor encodes physical
	// world size (radius * 2) which already incorporates transform scale via
	// the world-space bounds, so an extra multiply would double-count.
	//
	// SSAM gates the query-time multiply on AssetType == Texture. This test
	// verifies that gate by registering one texture entry and one mesh entry
	// on the same scaled-2x component and querying both with their
	// respective AssetTypes.
	//
	// Expected: SizeTexture ~ 2 * SizeMesh — the only delta is the *2
	// ComponentScale multiply applied to the texture query.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMeshTransformScaleNotApplied, SSAM_TEST_NAME ".ComponentScale.MeshTransformScaleNotApplied", TestFlags)

	bool FSSAMMeshTransformScaleNotApplied::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* TextureAsset = Env.CreateTestAsset();
		USSAMTestStreamableAsset* MeshAsset = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		// One 2x-scaled component holding both entries, ensuring identical bounds + view geometry.
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent(
				{
					FSSAMTestAssetEntry{ TextureAsset, TexelFactor, /*bAffectedByComponentScale*/ true },
					FSSAMTestAssetEntry{ MeshAsset, TexelFactor, /*bAffectedByComponentScale*/ false },
				}, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeTexture = Env.QueryScreenSize(TextureAsset->GetSSAMAssetHandle(), EStreamableRenderAssetType::Texture).MaxSize;
		const float SizeMesh = Env.QueryScreenSize(MeshAsset->GetSSAMAssetHandle(), EStreamableRenderAssetType::StaticMesh).MaxSize;

		TestTrue(TEXT("texture MaxSize > 0"), SizeTexture > 0.f);
		TestTrue(TEXT("mesh MaxSize > 0"), SizeMesh > 0.f);

		// Both share the same component (same bounds, view, TexelFactor input). The only
		// difference is whether the query path multiplies by BoundsViewInfo.ComponentScale
		// (textures only). Expected ratio is exactly 2.0.
		const float Ratio = SizeTexture / SizeMesh;
		TestTrue(FString::Printf(TEXT("texture (with TS) vs mesh (no TS) ratio is ~2.0 (actual: %.3f)"), Ratio),
			FMath::IsNearlyEqual(Ratio, 2.0f, 0.01f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Fallback-path counterpart of ComponentScaleFlagOptOut: a texture entry
	// registered through the fallback (UPrimitiveComponent) path with
	// bAffectedByComponentScale=false on a 2x-scaled component should not
	// receive the query-time *ComponentScale multiply.
	//
	// Expected: flag=true and flag=false differ by the *ComponentScale (=2)
	// factor — ratio 2.0. Verifies the fallback path correctly plumbs
	// GetStreamingScale via FRegister(Component, ...) AND honors
	// bAffectedByComponentScale at query time.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMFallbackComponentScaleFlagOptOut, SSAM_TEST_NAME ".ComponentScale.FallbackFlagOptOut", TestFlags)

	bool FSSAMFallbackComponentScaleFlagOptOut::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* AssetFlagTrue = Env.CreateTestAsset();
		USSAMTestStreamableAsset* AssetFlagFalse = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		// 2x-scaled fallback component, flag=true (scaled, since fallback honors GetStreamingScale)
		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent(
				{ FSSAMTestAssetEntry{ AssetFlagTrue, TexelFactor, /*bAffectedByComponentScale*/ true } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		// 2x-scaled fallback component, flag=false (NOT scaled, since SSAM honors bAffectedByComponentScale)
		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent(
				{ FSSAMTestAssetEntry{ AssetFlagFalse, TexelFactor, /*bAffectedByComponentScale*/ false } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessFallbackRegistrations();
		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeFlagTrue = Env.QueryScreenSize(AssetFlagTrue->GetSSAMAssetHandle()).MaxSize;
		const float SizeFlagFalse = Env.QueryScreenSize(AssetFlagFalse->GetSSAMAssetHandle()).MaxSize;

		TestTrue(TEXT("fallback flag=true MaxSize > 0"), SizeFlagTrue > 0.f);
		TestTrue(TEXT("fallback flag=false MaxSize > 0"), SizeFlagFalse > 0.f);

		// Identical bounds and view; only the flag controls whether the ComponentScale (=2)
		// multiply is applied. Ratio ~ 2.0.
		const float Ratio = SizeFlagTrue / SizeFlagFalse;
		TestTrue(FString::Printf(TEXT("fallback flag=true vs flag=false ratio is ~2.0 (actual: %.3f)"), Ratio),
			FMath::IsNearlyEqual(Ratio, 2.0f, 0.05f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Fallback-path counterpart of MeshTransformScaleNotApplied: verify that
	// the AssetType-keyed query gate keeps the *ComponentScale multiply off
	// mesh entries even when registered through the fallback path.
	//
	// Setup: one 2x-scaled fallback component carrying both a texture entry
	// and a mesh entry.
	//
	// Expected: SizeTexture ~ 2 * SizeMesh — the only delta is the *2
	// ComponentScale multiply on the texture query. Verifies the fallback
	// path plumbs GetStreamingScale (so ComponentScale=2 is visible) AND
	// the AssetType-keyed gate keeps mesh entries correctly excluded.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMFallbackMeshTransformScaleNotApplied, SSAM_TEST_NAME ".ComponentScale.FallbackMeshTransformScaleNotApplied", TestFlags)

	bool FSSAMFallbackMeshTransformScaleNotApplied::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* TextureAsset = Env.CreateTestAsset();
		USSAMTestStreamableAsset* MeshAsset = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent(
				{
					FSSAMTestAssetEntry{ TextureAsset, TexelFactor, /*bAffectedByComponentScale*/ true },
					FSSAMTestAssetEntry{ MeshAsset, TexelFactor, /*bAffectedByComponentScale*/ false },
				}, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessFallbackRegistrations();
		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeTexture = Env.QueryScreenSize(TextureAsset->GetSSAMAssetHandle(), EStreamableRenderAssetType::Texture).MaxSize;
		const float SizeMesh = Env.QueryScreenSize(MeshAsset->GetSSAMAssetHandle(), EStreamableRenderAssetType::StaticMesh).MaxSize;

		TestTrue(TEXT("fallback texture MaxSize > 0"), SizeTexture > 0.f);
		TestTrue(TEXT("fallback mesh MaxSize > 0"), SizeMesh > 0.f);

		// Both entries share bounds, view, TexelFactor, and component scale. Only delta is the
		// AssetType-keyed multiply (textures only). Verifies the fallback path plumbs
		// GetStreamingScale (so ComponentScale=2 reaches the texture query) AND that the
		// AssetType gate keeps mesh entries at ComponentScale=1, producing ratio = 2.0.
		const float Ratio = SizeTexture / SizeMesh;
		TestTrue(FString::Printf(TEXT("fallback texture (with TS) vs fallback mesh (no TS) ratio is ~2.0 (actual: %.3f)"), Ratio),
			FMath::IsNearlyEqual(Ratio, 2.0f, 0.05f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Mesh-only regression guard for the primary (scene-proxy) path: scaling
	// a mesh-bearing component should change MaxSize ONLY through the
	// world-space bounds (larger extents -> shorter distance-to-box ->
	// larger NormalizedSize). No *ComponentScale multiply should be applied
	// at query time because AssetType != Texture gates the multiplier off.
	//
	// Two components with identical TexelFactor, one at scale=1 and one at
	// scale=2, queried as StaticMesh. Expected MaxSize ratio ~ 1.125 (the
	// distance-to-box bonus alone): with a view at (1000,0,0) and 100-unit
	// extents, scale=1 gives Distance=900 (NSize=1920/900) and scale=2 gives
	// Distance=800 (NSize=1920/800), so 2.4/2.133 = 1.125. A future change
	// that mistakenly applies *ComponentScale to mesh queries would push the
	// ratio toward 2.25 and fail this test.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMeshScaleViaBoundsOnly, SSAM_TEST_NAME ".ComponentScale.MeshScaleViaBoundsOnly", TestFlags)

	bool FSSAMMeshScaleViaBoundsOnly::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* MeshUnscaled = Env.CreateTestAsset();
		USSAMTestStreamableAsset* MeshScaled = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		// Unscaled component (TS=1)
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent(
				{ FSSAMTestAssetEntry{ MeshUnscaled, TexelFactor, /*bAffectedByComponentScale*/ false } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(1.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		// 2x-scaled component (TS=2)
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent(
				{ FSSAMTestAssetEntry{ MeshScaled, TexelFactor, /*bAffectedByComponentScale*/ false } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeUnscaled = Env.QueryScreenSize(MeshUnscaled->GetSSAMAssetHandle(), EStreamableRenderAssetType::StaticMesh).MaxSize;
		const float SizeScaled = Env.QueryScreenSize(MeshScaled->GetSSAMAssetHandle(), EStreamableRenderAssetType::StaticMesh).MaxSize;

		TestTrue(TEXT("unscaled mesh MaxSize > 0"), SizeUnscaled > 0.f);
		TestTrue(TEXT("scaled mesh MaxSize > 0"), SizeScaled > 0.f);

		const float Ratio = SizeScaled / SizeUnscaled;
		TestTrue(FString::Printf(TEXT("mesh: scale=2 vs scale=1 ratio is ~1.125 (distance bonus only) (actual: %.3f)"), Ratio),
			FMath::IsNearlyEqual(Ratio, 1.125f, 0.02f));

		return true;
	}

	// -----------------------------------------------------------------------
	// Fallback-path counterpart of MeshScaleViaBoundsOnly: same expectation
	// (mesh queries skip the *ComponentScale multiply, so scaling the
	// component changes MaxSize only through the bounds-driven distance
	// bonus). The fallback path's StreamingScaleFactor is now plumbed via
	// GetStreamingScale, but mesh queries don't consult ComponentScale
	// regardless. Acts as a "the asset-type gate still works through the
	// fallback path" guard.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMFallbackMeshScaleViaBoundsOnly, SSAM_TEST_NAME ".ComponentScale.FallbackMeshScaleViaBoundsOnly", TestFlags)

	bool FSSAMFallbackMeshScaleViaBoundsOnly::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* MeshUnscaled = Env.CreateTestAsset();
		USSAMTestStreamableAsset* MeshScaled = Env.CreateTestAsset();

		const FBoxSphereBounds Bounds(FVector::ZeroVector, FVector(100.f), 100.f);
		const float TexelFactor = 512.f;

		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent(
				{ FSSAMTestAssetEntry{ MeshUnscaled, TexelFactor, /*bAffectedByComponentScale*/ false } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(1.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		{
			USSAMTestComponent* Comp = Env.SpawnTestFallbackComponent(
				{ FSSAMTestAssetEntry{ MeshScaled, TexelFactor, /*bAffectedByComponentScale*/ false } }, Bounds);
			Comp->bSupportScaleFactor = true;
			Comp->SetWorldScale3D(FVector(2.f));
			Comp->MarkRenderStateDirty();
			Comp->RecreateRenderState_Concurrent();
		}

		Env.ProcessFallbackRegistrations();
		Env.ProcessSSAM();
		Env.UpdateBoundSizes(FVector(1000.f, 0.f, 0.f));

		const float SizeUnscaled = Env.QueryScreenSize(MeshUnscaled->GetSSAMAssetHandle(), EStreamableRenderAssetType::StaticMesh).MaxSize;
		const float SizeScaled = Env.QueryScreenSize(MeshScaled->GetSSAMAssetHandle(), EStreamableRenderAssetType::StaticMesh).MaxSize;

		TestTrue(TEXT("unscaled fallback mesh MaxSize > 0"), SizeUnscaled > 0.f);
		TestTrue(TEXT("scaled fallback mesh MaxSize > 0"), SizeScaled > 0.f);

		const float Ratio = SizeScaled / SizeUnscaled;
		TestTrue(FString::Printf(TEXT("fallback mesh: scale=2 vs scale=1 ratio is ~1.125 (distance bonus only) (actual: %.3f)"), Ratio),
			FMath::IsNearlyEqual(Ratio, 1.125f, 0.02f));

		return true;
	}

	// ===================================================================
	// GROUP 6: Lifecycle / Stress
	//
	// Tests that exercise SSAM's internal sparse array management under
	// repeated registration/unregistration cycles. Verify index recycling,
	// no memory leaks, and correct behavior under bulk operations.
	// ===================================================================

	// -----------------------------------------------------------------------
	// Call Process() when there are no pending records. Verify it's a safe
	// no-op that doesn't corrupt internal state. Tests that a subsequent
	// registration still works correctly after empty processing cycles.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMEmptyProcess, SSAM_TEST_NAME ".Lifecycle.EmptyProcess", TestFlags)

	bool FSSAMEmptyProcess::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		// Call Process() multiple times with nothing pending
		Env.ProcessSSAM();
		Env.ProcessSSAM();
		Env.ProcessSSAM();

		// Now register something and verify it still works
		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		Env.SpawnTestComponent({ { Asset, 512.f } });
		Env.ProcessSSAM();

		TestTrue(TEXT("Registration works after empty Process() calls"), FSimpleStreamableAssetManager::IsAssetRegistered(Asset->GetSSAMAssetHandle()));

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has 1 bound after empty Process() calls"), Boxes.Num(), 1);

		return true;
	}

	// -----------------------------------------------------------------------
	// Register N components, unregister all, then register N new ones.
	// Verify that freed indices are reused (allocated size doesn't grow
	// significantly) and that the new components are correctly registered.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMIndexRecyclingNoLeak, SSAM_TEST_NAME ".Lifecycle.IndexRecyclingNoLeak", TestFlags)

	bool FSSAMIndexRecyclingNoLeak::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();
		constexpr int32 N = 10;

		// Phase 1: Register N components
		TArray<USSAMTestComponent*> Components;
		for (int32 i = 0; i < N; ++i)
		{
			Components.Add(Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
				FVector(i * 500.f, 0.f, 0.f)));
		}
		Env.ProcessSSAM();
		const uint32 SizeAfterFirstBatch = FSimpleStreamableAssetManager::GetAllocatedSize();

		// Phase 2: Unregister all
		for (USSAMTestComponent* Comp : Components)
		{
			Comp->DestroyComponent();
		}
		Components.Empty();
		Env.ProcessSSAM();

		// Phase 3: Register N new components (should reuse freed indices)
		for (int32 i = 0; i < N; ++i)
		{
			Components.Add(Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
				FVector(i * 500.f, 0.f, 0.f)));
		}
		Env.ProcessSSAM();
		const uint32 SizeAfterSecondBatch = FSimpleStreamableAssetManager::GetAllocatedSize();

		TestTrue(TEXT("Allocated size did not grow significantly after index recycling"), SizeAfterSecondBatch <= SizeAfterFirstBatch * 2);

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("Asset has N bounds after re-registration"), Boxes.Num(), N);

		for (USSAMTestComponent* Comp : Components)
		{
			Comp->DestroyComponent();
		}
		Env.ProcessSSAM();
		return true;
	}

	// -----------------------------------------------------------------------
	// Rapidly create and destroy components in a loop (50 cycles). Verify
	// no crash, no memory leak (allocated size stays bounded), and no
	// lingering bounds after all cycles complete.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMRapidCreateDestroy, SSAM_TEST_NAME ".Lifecycle.RapidCreateDestroy", TestFlags)

	bool FSSAMRapidCreateDestroy::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		// Run one cycle first to establish a warm baseline (sparse arrays
		// allocate on first use, so comparing against a cold start is misleading)
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
			Env.ProcessSSAM();
			Comp->DestroyComponent();
			Env.ProcessSSAM();
		}
		const uint32 BaselineSize = FSimpleStreamableAssetManager::GetAllocatedSize();

		for (int32 i = 0; i < 50; ++i)
		{
			USSAMTestComponent* Comp = Env.SpawnTestComponent({ { Asset, 512.f } });
			Env.ProcessSSAM();
			Comp->DestroyComponent();
			Env.ProcessSSAM();
		}

		TestTrue(TEXT("Allocated size stays bounded after rapid create/destroy"), FSimpleStreamableAssetManager::GetAllocatedSize() <= BaselineSize * 2);

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("No lingering bounds"), Boxes.Num(), 0);

		return true;
	}

	// -----------------------------------------------------------------------
	// UE-363476: Simulates level transitions where each "level" introduces
	// unique assets with many objects. Objects are unregistered between
	// levels but assets stay registered (cached). Verifies that zombie
	// asset registrations are fully reclaimed when their element count
	// drops to zero, preventing unbounded growth across transitions.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMMemoryBloatOnLevelTransition, SSAM_TEST_NAME ".Lifecycle.MemoryBloatOnLevelTransition", TestFlags)

	bool FSSAMMemoryBloatOnLevelTransition::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		// Use many levels with many objects and unique assets per level.
		// If inner sparse arrays are not freed when empty, they retain
		// peak capacity after all objects are destroyed, causing linear
		// memory growth across levels.
		constexpr int32 NumLevels = 10;
		constexpr int32 AssetsPerLevel = 10;
		constexpr int32 ObjectsPerLevel = 50;

		// Pre-create all assets (they persist across "level transitions")
		TArray<USSAMTestStreamableAsset*> AllAssets;
		for (int32 i = 0; i < NumLevels * AssetsPerLevel; ++i)
		{
			AllAssets.Add(Env.CreateTestAsset());
		}

		// Lambda to simulate one level transition: spawn objects, process,
		// destroy objects, process. Assets stay alive.
		auto SimulateLevel = [&](int32 Level)
		{
			TArray<USSAMTestComponent*> LevelComponents;
			for (int32 i = 0; i < ObjectsPerLevel; ++i)
			{
				const int32 AssetBase = Level * AssetsPerLevel;
				LevelComponents.Add(Env.SpawnTestComponent( { { AllAssets[AssetBase + (i % AssetsPerLevel)], 256.f },
					  { AllAssets[AssetBase + ((i + 1) % AssetsPerLevel)], 128.f } },
					FBoxSphereBounds(FVector(i * 200.f, Level * 1000.f, 0.f), FVector(50.f), 50.f)));
			}
			Env.ProcessSSAM();

			for (USSAMTestComponent* Comp : LevelComponents)
			{
				Comp->DestroyComponent();
			}
			Env.ProcessSSAM();
		};

		// Run the first level to warm up all fixed-overhead structures
		// (ObjectBounds4, ObjectRegistrationIndexToAssetProperty, etc.)
		SimulateLevel(0);
		const uint32 SizeAfterLevel1 = FSimpleStreamableAssetManager::GetAllocatedSize();

		// Run remaining levels — each introduces new unique assets
		for (int32 Level = 1; Level < NumLevels; ++Level)
		{
			SimulateLevel(Level);
		}
		const uint32 SizeAfterAllLevels = FSimpleStreamableAssetManager::GetAllocatedSize();

		// With the fix, zombie asset slots are fully reclaimed when all objects
		// are removed: inner buffers freed, index returned to pool, shared_ptr
		// invalidated. Each subsequent level reuses the freed indices, so
		// growth should be near zero. Without the fix, each level's inner
		// sparse arrays retain peak capacity (~75KB+ total bloat).
		//
		// Threshold: 16KB catches unfixed bloat while allowing for minor
		// TArray slack and alignment overhead.
		const int64 MemoryGrowth = (int64)SizeAfterAllLevels - (int64)SizeAfterLevel1;

		UE_LOGF(LogTemp, Display, "UE-363476 test: SizeAfterLevel1=%u, SizeAfterAllLevels=%u, Growth=%lld bytes",
			SizeAfterLevel1, SizeAfterAllLevels, MemoryGrowth);

		TestTrue(TEXT("Memory growth across level transitions stays bounded (UE-363476)"), MemoryGrowth < 16384);

		// Verify all asset bounds are actually empty
		for (USSAMTestStreamableAsset* Asset : AllAssets)
		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("Asset has 0 bounds after all objects destroyed"), Boxes.Num(), 0);
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// Register 100 components, each referencing 1-3 of 3 shared assets.
	// Verify all components and assets get valid indices, correct bound
	// counts, and clean state after bulk unregister. Tests the MPMC queue
	// and sparse array capacity under load.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMBulkRegisterUnregister, SSAM_TEST_NAME ".Lifecycle.BulkRegisterUnregister", TestFlags)

	bool FSSAMBulkRegisterUnregister::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		constexpr int32 NumAssets = 3;
		constexpr int32 NumComponents = 100;

		TArray<USSAMTestStreamableAsset*> Assets;
		for (int32 i = 0; i < NumAssets; ++i)
		{
			Assets.Add(Env.CreateTestAsset());
		}

		// Register 100 components, each referencing (i % NumAssets + 1) assets
		TArray<USSAMTestComponent*> Components;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			TArray<FSSAMTestAssetEntry> Entries;
			for (int32 j = 0; j <= (i % NumAssets); ++j)
			{
				Entries.Add({ Assets[j], 256.f * (j + 1) });
			}
			Components.Add(Env.SpawnTestComponent(Entries, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
				FVector(i * 200.f, 0.f, 0.f)));
		}
		Env.ProcessSSAM();

		for (int32 i = 0; i < NumAssets; ++i)
		{
			TestTrue(FString::Printf(TEXT("Asset[%d] has valid index"), i), FSimpleStreamableAssetManager::IsAssetRegistered(Assets[i]->GetSSAMAssetHandle()));
		}

		// Asset[0] is referenced by all 100 components
		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Assets[0], Boxes);
			TestEqual(TEXT("Asset[0] referenced by all components"), Boxes.Num(), NumComponents);
		}

		// Bulk unregister
		for (USSAMTestComponent* Comp : Components)
		{
			Comp->DestroyComponent();
		}
		Env.ProcessSSAM();

		for (int32 i = 0; i < NumAssets; ++i)
		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Assets[i], Boxes);
			TestEqual(FString::Printf(TEXT("Asset[%d] has 0 bounds after bulk unregister"), i), Boxes.Num(), 0);
		}

		return true;
	}

	// ===================================================================
	// GROUP 7: Concurrency
	//
	// Tests concurrent access to SSAM's MPMC queues. Some tests use the
	// FPrimitiveSceneDesc path to add/remove primitives from parallel GT
	// workers (exercising the lockless queues under contention). Others
	// run Process() on a background thread while the GT produces records
	// (mirroring the cooked build's FDoWorkAsyncTask pattern).
	// ===================================================================

	// -----------------------------------------------------------------------
	// 50 primitives are added/removed from parallel game thread workers
	// via FPrimitiveSceneDesc + ParallelFor + EParallelGameThread tag.
	// This exercises TLocklessGrowingStorage / TConsumeAllMpmcQueue under
	// real thread contention from multiple concurrent producers.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMConcurrentRegistration, SSAM_TEST_NAME ".Concurrency.ParallelRegistration", TestFlags)

	bool FSSAMConcurrentRegistration::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		constexpr int32 NumPrimitives = 50;
		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		UWorld* World = Env.GetWorld();
		FSceneInterface* Scene = World->Scene;
		TestNotNull(TEXT("World has a scene"), Scene);

		// Pre-build all data structures on the game thread.
		// Heap-allocated because the render thread holds references that may
		// outlive the synchronous test scope (deferred cleanup).
		struct FTestPrimitive
		{
			FSSAMTestProxyDesc ProxyDesc;
			FPrimitiveSceneInfoData SceneData;
			FPrimitiveSceneDesc SceneDesc;
			FSSAMTestSceneProxy* Proxy = nullptr;
		};

		TArray<TUniquePtr<FTestPrimitive>> Primitives;
		Primitives.Reserve(NumPrimitives);

		for (int32 i = 0; i < NumPrimitives; ++i)
		{
			auto P = MakeUnique<FTestPrimitive>();
			const FBoxSphereBounds Bounds(FVector(i * 200.f, 0.f, 0.f), FVector(100.f), 100.f);

			TArray<FStreamingRenderAssetPrimitiveInfo> AssetInfos;
			AssetInfos.Emplace(Asset, Bounds, 256.f, PackedRelativeBox_Identity, false, true);

			P->ProxyDesc.Scene = Scene;
			P->Proxy = new FSSAMTestSceneProxy(&P->ProxyDesc, MoveTemp(AssetInfos));

			P->SceneData.SceneProxy = P->Proxy;
			P->SceneDesc.SceneProxy = P->Proxy;
			P->SceneDesc.ProxyDesc = &P->ProxyDesc;
			P->SceneDesc.PrimitiveSceneData = &P->SceneData;
			P->SceneDesc.Bounds = Bounds;
			P->SceneDesc.LocalBounds = Bounds;
			P->SceneDesc.RenderMatrix = FMatrix::Identity;
			P->SceneDesc.AttachmentRootPosition = Bounds.Origin;
			P->SceneDesc.Mobility = EComponentMobility::Movable;

			Primitives.Add(MoveTemp(P));
		}

		// Concurrent registration from parallel game thread workers
		ParallelFor(NumPrimitives, [&Primitives, Scene](int32 Index)
		{
			FTaskTagScope Scope(ETaskTag::EParallelGameThread);
			Scene->AddPrimitive(&Primitives[Index]->SceneDesc);
		});
		FlushRenderingCommands();
		Env.ProcessSSAM();

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("Asset has N bounds from concurrent registration"), Boxes.Num(), NumPrimitives);
		}

		// Concurrent unregistration from parallel game thread workers
		ParallelFor(NumPrimitives, [&Primitives, Scene](int32 Index)
		{
			FTaskTagScope Scope(ETaskTag::EParallelGameThread);
			Scene->RemovePrimitive(&Primitives[Index]->SceneDesc);
		});
		FlushRenderingCommands();
		Env.ProcessSSAM();

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("No bounds after concurrent cleanup"), Boxes.Num(), 0);
		}

		// Do NOT delete proxies — the scene takes ownership after AddPrimitive
		// and deletes them during RemovePrimitive's deferred cleanup or scene teardown.

		return true;
	}
	// -----------------------------------------------------------------------
	// Cooked build pattern: Process() loops continuously on a background
	// worker thread while the game thread registers and unregisters
	// components. This mirrors the real streaming system where
	// FDoWorkAsyncTask runs Process() every frame on the thread pool
	// while the game thread and parallel GT workers are simultaneously
	// pushing records to the MPMC queues.
	//
	// The consumer loop runs continuously while producers register objects.
	//
	// Note: FPlatformProcess::Yield() does not guarantee thread interleaving.
	// All registrations may accumulate and be consumed in the final
	// ProcessSSAM() call rather than incrementally by the background loop.
	// The test still has value as a stress test for the MPMC queue under
	// concurrent access, and the race detector (in instrumented builds)
	// will flag any data races regardless of scheduling order.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMConcurrentProcessAndRegister, SSAM_TEST_NAME ".Concurrency.ConcurrentProcessAndRegister", TestFlags)

	bool FSSAMConcurrentProcessAndRegister::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		std::atomic<bool> bStopProcessing{ false };

		// Launch a background worker that loops Process() continuously,
		// just like the cooked build's thread pool worker does every frame.
		UE::Tasks::FTask ProcessTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Env, &bStopProcessing]()
		{
			while (!bStopProcessing.load(std::memory_order_relaxed))
			{
				Env.ProcessSSAM();
				FPlatformProcess::Yield();
			}
		});

		// Game thread: register components in bursts while the consumer is running
		constexpr int32 NumBursts = 10;
		constexpr int32 NumPerBurst = 5;
		TArray<USSAMTestComponent*> AllComponents;
		for (int32 Burst = 0; Burst < NumBursts; ++Burst)
		{
			for (int32 i = 0; i < NumPerBurst; ++i)
			{
				AllComponents.Add(Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
					FVector(float(Burst * NumPerBurst + i) * 200.f, 0.f, 0.f)));
			}
			// Small yield to give the consumer a chance to run between bursts
			FPlatformProcess::Yield();
		}

		// Stop the consumer loop
		bStopProcessing.store(true, std::memory_order_relaxed);
		ProcessTask.Wait();

		// Final Process to drain anything remaining
		Env.ProcessSSAM();

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("All components registered after concurrent process+register"), Boxes.Num(), NumBursts * NumPerBurst);

		return true;
	}

	// -----------------------------------------------------------------------
	// Cooked build pattern: Process() loops on a background worker while
	// the game thread destroys components, pushing FUnregister records
	// concurrently with the consumer draining queues.
	//
	// Same Yield() scheduling caveat as ConcurrentProcessAndRegister.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMConcurrentProcessAndUnregister, SSAM_TEST_NAME ".Concurrency.ConcurrentProcessAndUnregister", TestFlags)

	bool FSSAMConcurrentProcessAndUnregister::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		// Register a batch of components first
		constexpr int32 NumComponents = 30;
		TArray<USSAMTestComponent*> Components;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			Components.Add(Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
				FVector(i * 200.f, 0.f, 0.f)));
		}
		Env.ProcessSSAM();

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("All registered before concurrent unregister"), Boxes.Num(), NumComponents);
		}

		std::atomic<bool> bStopProcessing{ false };

		// Background consumer loop
		UE::Tasks::FTask ProcessTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Env, &bStopProcessing]()
		{
			while (!bStopProcessing.load(std::memory_order_relaxed))
			{
				Env.ProcessSSAM();
				FPlatformProcess::Yield();
			}
		});

		// Game thread: destroy components one by one while consumer runs
		for (USSAMTestComponent* Comp : Components)
		{
			Comp->DestroyComponent();
			FPlatformProcess::Yield();
		}

		bStopProcessing.store(true, std::memory_order_relaxed);
		ProcessTask.Wait();

		Env.ProcessSSAM();

		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("All unregistered after concurrent process+unregister"), Boxes.Num(), 0);

		return true;
	}

	// -----------------------------------------------------------------------
	// Cooked build pattern: Process() loops on a background worker while
	// components move (pushing FUpdate records via the render thread).
	// The worker is consuming FUpdate records while the RT is producing them.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMConcurrentProcessAndUpdate, SSAM_TEST_NAME ".Concurrency.ConcurrentProcessAndUpdate", TestFlags)

	bool FSSAMConcurrentProcessAndUpdate::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		constexpr int32 NumComponents = 10;
		TArray<USSAMTestComponent*> Components;
		for (int32 i = 0; i < NumComponents; ++i)
		{
			Components.Add(Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
				FVector(i * 200.f, 0.f, 0.f)));
		}
		Env.ProcessSSAM();

		std::atomic<bool> bStopProcessing{ false };

		// Background consumer loop
		UE::Tasks::FTask ProcessTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Env, &bStopProcessing]()
		{
			while (!bStopProcessing.load(std::memory_order_relaxed))
			{
				Env.ProcessSSAM();
				FPlatformProcess::Yield();
			}
		});

		// Game thread: move components repeatedly while consumer runs.
		// SendRenderTransform_Concurrent enqueues a render command (RendererScene.cpp
		// line 1687). The actual FUpdate push to SSAM happens on the RENDER THREAD
		// when that command executes. FlushRenderingCommands between move batches
		// ensures the RT processes the commands, pushing FUpdate records into the
		// SSAM queue where the background Process() loop can consume them.
		constexpr int32 NumMoveIterations = 20;
		for (int32 Iter = 0; Iter < NumMoveIterations; ++Iter)
		{
			for (int32 i = 0; i < NumComponents; ++i)
			{
				Components[i]->SetWorldLocation(FVector(i * 200.f + Iter * 10.f, 0.f, 0.f));
				Components[i]->SendRenderTransform_Concurrent();
			}
			// Flush RT commands so FUpdate records are actually pushed to SSAM
			// queues, giving the background Process() loop a chance to consume them.
			FlushRenderingCommands();
		}

		bStopProcessing.store(true, std::memory_order_relaxed);
		ProcessTask.Wait();
		FlushRenderingCommands();
		Env.ProcessSSAM();

		// All components should still be registered (moves don't unregister)
		TArray<FBox> Boxes;
		FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
		TestEqual(TEXT("All components still registered after concurrent moves"), Boxes.Num(), NumComponents);

		return true;
	}

	// -----------------------------------------------------------------------
	// Worst-case cooked build scenario: Process() loops on a background
	// worker while three producer threads operate simultaneously:
	// (a) Parallel GT workers registering new primitives via FPrimitiveSceneDesc
	// (b) Game thread unregistering existing components
	// (c) Game thread registering new components
	// All producers push to SSAM's MPMC queues while the consumer drains them.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMTripleProducerConcurrency, SSAM_TEST_NAME ".Concurrency.TripleProducerConcurrency", TestFlags)

	bool FSSAMTripleProducerConcurrency::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		UWorld* World = Env.GetWorld();
		FSceneInterface* Scene = World->Scene;
		TestNotNull(TEXT("World has a scene"), Scene);

		// Pre-register components that we'll unregister during the test
		constexpr int32 NumToUnregister = 15;
		TArray<USSAMTestComponent*> ComponentsToUnregister;
		for (int32 i = 0; i < NumToUnregister; ++i)
		{
			ComponentsToUnregister.Add(Env.SpawnTestComponent({ { Asset, 512.f } }, FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f),
				FVector(i * 200.f, 0.f, 0.f)));
		}
		Env.ProcessSSAM();

		// Prepare FPrimitiveSceneDesc primitives for parallel registration
		constexpr int32 NumDescPrimitives = 15;
		struct FTestPrimitive
		{
			FSSAMTestProxyDesc ProxyDesc;
			FPrimitiveSceneInfoData SceneData;
			FPrimitiveSceneDesc SceneDesc;
		};

		TArray<TUniquePtr<FTestPrimitive>> Primitives;
		Primitives.Reserve(NumDescPrimitives);
		for (int32 i = 0; i < NumDescPrimitives; ++i)
		{
			auto P = MakeUnique<FTestPrimitive>();
			const FBoxSphereBounds Bounds(FVector((NumToUnregister + i) * 200.f, 0.f, 0.f), FVector(100.f), 100.f);

			TArray<FStreamingRenderAssetPrimitiveInfo> AssetInfos;
			AssetInfos.Emplace(Asset, Bounds, 256.f, PackedRelativeBox_Identity, false, true);

			P->ProxyDesc.Scene = Scene;
			FSSAMTestSceneProxy* Proxy = new FSSAMTestSceneProxy(&P->ProxyDesc, MoveTemp(AssetInfos));

			P->SceneData.SceneProxy = Proxy;
			P->SceneDesc.SceneProxy = Proxy;
			P->SceneDesc.ProxyDesc = &P->ProxyDesc;
			P->SceneDesc.PrimitiveSceneData = &P->SceneData;
			P->SceneDesc.Bounds = Bounds;
			P->SceneDesc.LocalBounds = Bounds;
			P->SceneDesc.RenderMatrix = FMatrix::Identity;
			P->SceneDesc.AttachmentRootPosition = Bounds.Origin;
			P->SceneDesc.Mobility = EComponentMobility::Movable;

			Primitives.Add(MoveTemp(P));
		}

		std::atomic<bool> bStopProcessing{ false };

		// (a) Background consumer loop — Process() running continuously
		UE::Tasks::FTask ProcessTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Env, &bStopProcessing]()
		{
			while (!bStopProcessing.load(std::memory_order_relaxed))
			{
				Env.ProcessSSAM();
				FPlatformProcess::Yield();
			}
		});

		// (b) Parallel GT workers registering new primitives via FPrimitiveSceneDesc
		UE::Tasks::FTask RegisterTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Primitives, Scene, NumDescPrimitives]()
		{
			ParallelFor(NumDescPrimitives, [&Primitives, Scene](int32 Index)
			{
				FTaskTagScope Scope(ETaskTag::EParallelGameThread);
				Scene->AddPrimitive(&Primitives[Index]->SceneDesc);
			});
		});

		// (c) Game thread unregistering existing components — concurrent with (a) and (b)
		for (USSAMTestComponent* Comp : ComponentsToUnregister)
		{
			Comp->DestroyComponent();
			FPlatformProcess::Yield();
		}

		// Wait for parallel registration to finish
		RegisterTask.Wait();

		// Stop the consumer and drain
		bStopProcessing.store(true, std::memory_order_relaxed);
		ProcessTask.Wait();
		FlushRenderingCommands();
		Env.ProcessSSAM();

		// Verify: unregistered components are gone, desc-based primitives are registered
		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("Only desc-based primitives remain after triple-producer"), Boxes.Num(), NumDescPrimitives);
		}

		// Cleanup desc-based primitives
		ParallelFor(NumDescPrimitives, [&Primitives, Scene](int32 Index)
		{
			FTaskTagScope Scope(ETaskTag::EParallelGameThread);
			Scene->RemovePrimitive(&Primitives[Index]->SceneDesc);
		});
		FlushRenderingCommands();
		Env.ProcessSSAM();

		{
			TArray<FBox> Boxes;
			FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
			TestEqual(TEXT("Clean state after triple-producer cleanup"), Boxes.Num(), 0);
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// Cooked build + GC scenario: Process() loops on a background worker
	// while the game thread garbage-collects UStreamableRenderAsset objects.
	// GC's BeginDestroy → UnlinkStreaming → UnregisterAsset pushes
	// FRemovedAssetRecord to the MPMC queue while the background worker
	// is draining it. Both paths acquire the same FCriticalSection.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMGarbageCollectionDuringProcess, SSAM_TEST_NAME ".Concurrency.GarbageCollectionDuringProcess", TestFlags)

	bool FSSAMGarbageCollectionDuringProcess::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		constexpr int32 NumAssets = 10;
		constexpr int32 NumComponentsPerAsset = 5;

		TArray<USSAMTestStreamableAsset*> Assets;
		for (int32 i = 0; i < NumAssets; ++i)
		{
			Assets.Add(Env.CreateTestAsset());
		}

		// Register components referencing each asset
		TArray<USSAMTestComponent*> Components;
		for (int32 i = 0; i < NumAssets; ++i)
		{
			for (int32 j = 0; j < NumComponentsPerAsset; ++j)
			{
				Components.Add(Env.SpawnTestComponent({ { Assets[i], 256.f } }, FBoxSphereBounds(FVector(i * 1000.f + j * 200.f, 0.f, 0.f), FVector(100.f), 100.f)));
			}
		}
		Env.ProcessSSAM();

		// Save each asset's SSAM handle. The handle is an 8-byte POD that survives GC regardless of
		// the UObject lifetime. After Process() drains the FRemovedAssetRecord and calls Release
		// on the handle, the slot's generation bumps and our saved handle becomes stale --
		// IsAssetRegistered() on it will return false.
		TArray<FSSAMAssetHandle> SavedHandles;
		for (USSAMTestStreamableAsset* Asset : Assets)
		{
			SavedHandles.Add(Asset->GetSSAMAssetHandle());
			TestTrue(TEXT("Asset has valid SSAM index before GC"), FSimpleStreamableAssetManager::IsAssetRegistered(SavedHandles.Last()));
		}

		// Unregister all objects (but keep assets registered in SSAM)
		for (USSAMTestComponent* Comp : Components)
		{
			Comp->DestroyComponent();
		}
		Env.ProcessSSAM();

		// Start background Process() loop — mirrors the cooked build async worker
		std::atomic<bool> bStopProcessing{ false };
		UE::Tasks::FTask ProcessTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Env, &bStopProcessing]()
		{
			while (!bStopProcessing.load(std::memory_order_relaxed))
			{
				Env.ProcessSSAM();
				FPlatformProcess::Yield();
			}
		});

		// Release assets for GC without setting StreamingIndex = INDEX_NONE.
		// This preserves the real BeginDestroy → UnlinkStreaming → UnregisterAsset path.
		Env.ReleaseAssetsForGC(Assets);

		// Force garbage collection — assets are now unreferenced and will be collected.
		// BeginDestroy → UnlinkStreaming → UnregisterAsset fires on the game thread
		// WHILE the background Process() loop is running.
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);

		// Stop background loop and drain any remaining records
		bStopProcessing.store(true, std::memory_order_relaxed);
		ProcessTask.Wait();
		Env.ProcessSSAM();

		// Verify: Process() consumed all FRemovedAssetRecords and released the handles; stale
		// handles now resolve to INDEX_NONE via the generation check.
		for (int32 i = 0; i < NumAssets; ++i)
		{
			TestFalse(FString::Printf(TEXT("Asset[%d] SSAM handle is stale after GC"), i), FSimpleStreamableAssetManager::IsAssetRegistered(SavedHandles[i]));
		}

		return true;
	}

	// -----------------------------------------------------------------------
	// Deliberately racy test to validate that the race detector catches
	// unsynchronized concurrent access. Two threads read/write a plain
	// int32 without any synchronization. Expected to FAIL on instrumented
	// builds. Remove or disable after validation.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMRaceDetectorCanary, SSAM_TEST_NAME ".Concurrency.RaceDetectorCanary", TestFlags)

	bool FSSAMRaceDetectorCanary::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		volatile int32 SharedCounter = 0;

		UE::Tasks::FTask WriterTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&SharedCounter]()
		{
			for (int32 i = 0; i < 1000; ++i)
			{
				++SharedCounter;
			}
		});

		// Read from the main thread while the writer is running
		for (int32 i = 0; i < 1000; ++i)
		{
			if (SharedCounter > 500)
			{
				break;
			}
		}

		WriterTask.Wait();
		TestTrue(TEXT("Counter was incremented"), SharedCounter > 0);
		return true;
	}

	// -----------------------------------------------------------------------
	// 30 primitives added/removed via FPrimitiveSceneDesc from parallel
	// game thread workers, repeated 5 times. Maximizes contention on
	// SSAM's MPMC queues through repeated concurrent cycles.
	// -----------------------------------------------------------------------
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSAMConcurrentOperationsStress, SSAM_TEST_NAME ".Concurrency.ConcurrentOperationsStress", TestFlags)

	bool FSSAMConcurrentOperationsStress::RunTest(const FString& Parameters)
	{
		FSSAMTestEnvironment Env;

		constexpr int32 NumPrimitives = 30;
		USSAMTestStreamableAsset* Asset = Env.CreateTestAsset();

		UWorld* World = Env.GetWorld();
		FSceneInterface* Scene = World->Scene;
		TestNotNull(TEXT("World has a scene"), Scene);

		struct FTestPrimitive
		{
			FSSAMTestProxyDesc ProxyDesc;
			FPrimitiveSceneInfoData SceneData;
			FPrimitiveSceneDesc SceneDesc;
			FSSAMTestSceneProxy* Proxy = nullptr;
		};

		TArray<TUniquePtr<FTestPrimitive>> Primitives;
		Primitives.Reserve(NumPrimitives);

		for (int32 i = 0; i < NumPrimitives; ++i)
		{
			auto P = MakeUnique<FTestPrimitive>();
			const FBoxSphereBounds Bounds(FVector(i * 200.f, 0.f, 0.f), FVector(100.f), 100.f);

			TArray<FStreamingRenderAssetPrimitiveInfo> AssetInfos;
			AssetInfos.Emplace(Asset, Bounds, 256.f, PackedRelativeBox_Identity, false, true);

			P->ProxyDesc.Scene = Scene;
			P->Proxy = new FSSAMTestSceneProxy(&P->ProxyDesc, MoveTemp(AssetInfos));

			P->SceneData.SceneProxy = P->Proxy;
			P->SceneDesc.SceneProxy = P->Proxy;
			P->SceneDesc.ProxyDesc = &P->ProxyDesc;
			P->SceneDesc.PrimitiveSceneData = &P->SceneData;
			P->SceneDesc.Bounds = Bounds;
			P->SceneDesc.LocalBounds = Bounds;
			P->SceneDesc.RenderMatrix = FMatrix::Identity;
			P->SceneDesc.AttachmentRootPosition = Bounds.Origin;
			P->SceneDesc.Mobility = EComponentMobility::Movable;

			Primitives.Add(MoveTemp(P));
		}

		// Run multiple iterations to increase chances of catching races
		constexpr int32 NumIterations = 5;
		for (int32 Iter = 0; Iter < NumIterations; ++Iter)
		{
			// Concurrent registration
			ParallelFor(NumPrimitives, [&Primitives, Scene](int32 Index)
			{
				FTaskTagScope Scope(ETaskTag::EParallelGameThread);
				Scene->AddPrimitive(&Primitives[Index]->SceneDesc);
			});
			FlushRenderingCommands();
			Env.ProcessSSAM();

			// Verify registration worked
			{
				TArray<FBox> Boxes;
				FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
				TestEqual(FString::Printf(TEXT("Iter %d: Asset has N bounds after registration"), Iter), Boxes.Num(), NumPrimitives);
			}

			// Concurrent unregistration
			ParallelFor(NumPrimitives, [&Primitives, Scene](int32 Index)
			{
				FTaskTagScope Scope(ETaskTag::EParallelGameThread);
				Scene->RemovePrimitive(&Primitives[Index]->SceneDesc);
			});
			FlushRenderingCommands();
			Env.ProcessSSAM();

			// Verify clean state
			{
				TArray<FBox> Boxes;
				FSimpleStreamableAssetManager::GetAssetReferenceBounds(Asset, Boxes);
				TestEqual(FString::Printf(TEXT("Iter %d: No bounds after unregistration"), Iter), Boxes.Num(), 0);
			}

			// Re-create proxies for next iteration (scene deleted them on remove)
			if (Iter < NumIterations - 1)
			{
				for (int32 i = 0; i < NumPrimitives; ++i)
				{
					FTestPrimitive& P = *Primitives[i];
					const FBoxSphereBounds Bounds(FVector(i * 200.f, 0.f, 0.f), FVector(100.f), 100.f);

					TArray<FStreamingRenderAssetPrimitiveInfo> AssetInfos;
					AssetInfos.Emplace(Asset, Bounds, 256.f, PackedRelativeBox_Identity, false, true);

					P.Proxy = new FSSAMTestSceneProxy(&P.ProxyDesc, MoveTemp(AssetInfos));
					P.SceneData.SceneProxy = P.Proxy;
					P.SceneDesc.SceneProxy = P.Proxy;
				}
			}
		}

		return true;
	}
}

#undef SSAM_TEST_NAME

#endif // WITH_DEV_AUTOMATION_TESTS
