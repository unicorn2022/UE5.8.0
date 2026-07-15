// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "MassSkinnedMeshRepresentationTypes.h"
#include "Mass/MetaHumanMassFragments.h"
#include "Mass/MetaHumanMassRepresentationSubsystem.h"

#if WITH_AUTOMATION_TESTS

//----------------------------------------------------------------------//
// FMassSkinnedMeshInstanceVisualizationMeshDesc Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshDescDefaultConstructor,
	"MetaHumanMassCrowd.RepresentationTypes.MeshDesc.DefaultConstructor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMeshDescDefaultConstructor::RunTest(const FString& Parameters)
{
	FMassSkinnedMeshInstanceVisualizationMeshDesc Desc;

	TestNull(TEXT("Asset should be null by default"), Desc.Asset.Get());
	TestNull(TEXT("TransformProvider should be null by default"), Desc.TransformProvider.Get());
	TestEqual(TEXT("MaterialOverrides should be empty"), Desc.MaterialOverrides.Num(), 0);
	TestTrue(TEXT("MinLODSignificance should default to High"), Desc.MinLODSignificance == float(EMassLOD::High));
	TestTrue(TEXT("MaxLODSignificance should default to Max"), Desc.MaxLODSignificance == float(EMassLOD::Max));
	TestFalse(TEXT("bCastShadows should be false by default"), Desc.bCastShadows);
	TestFalse(TEXT("bRequiresExternalInstanceIDTracking should be false by default"), Desc.bRequiresExternalInstanceIDTracking);
	TestEqual(TEXT("Mobility should be Movable by default"), Desc.Mobility, EComponentMobility::Movable);
	TestTrue(TEXT("ISKMComponentClass should default to UInstancedSkinnedMeshComponent"), Desc.InstancedSkinnedMeshComponentClass == UInstancedSkinnedMeshComponent::StaticClass());
	TestTrue(TEXT("LocalTransform should be identity"), Desc.LocalTransform.Equals(FTransform::Identity));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshDescEquality,
	"MetaHumanMassCrowd.RepresentationTypes.MeshDesc.Equality",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMeshDescEquality::RunTest(const FString& Parameters)
{
	FMassSkinnedMeshInstanceVisualizationMeshDesc DescA;
	FMassSkinnedMeshInstanceVisualizationMeshDesc DescB;

	TestTrue(TEXT("Two default-constructed descs should be equal"), DescA == DescB);

	DescA.bCastShadows = true;
	TestFalse(TEXT("Descs with different bCastShadows should not be equal"), DescA == DescB);

	DescB.bCastShadows = true;
	TestTrue(TEXT("Descs with matching bCastShadows should be equal"), DescA == DescB);

	DescA.MinLODSignificance = 2.0f;
	TestFalse(TEXT("Descs with different MinLODSignificance should not be equal"), DescA == DescB);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshDescHash,
	"MetaHumanMassCrowd.RepresentationTypes.MeshDesc.Hash",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMeshDescHash::RunTest(const FString& Parameters)
{
	FMassSkinnedMeshInstanceVisualizationMeshDesc DescA;
	FMassSkinnedMeshInstanceVisualizationMeshDesc DescB;

	TestEqual(TEXT("Two default-constructed descs should have the same hash"), GetTypeHash(DescA), GetTypeHash(DescB));

	DescA.bCastShadows = true;
	TestNotEqual(TEXT("Descs with different bCastShadows should have different hashes"), GetTypeHash(DescA), GetTypeHash(DescB));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshDescSetSignificanceRange,
	"MetaHumanMassCrowd.RepresentationTypes.MeshDesc.SetSignificanceRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMeshDescSetSignificanceRange::RunTest(const FString& Parameters)
{
	FMassSkinnedMeshInstanceVisualizationMeshDesc Desc;

	Desc.SetSignificanceRange(EMassLOD::Medium, EMassLOD::Off);
	TestTrue(TEXT("MinLODSignificance should match Medium"), Desc.MinLODSignificance == float(EMassLOD::Medium));
	TestTrue(TEXT("MaxLODSignificance should match Off"), Desc.MaxLODSignificance == float(EMassLOD::Off));

	return true;
}


//----------------------------------------------------------------------//
// FSkinnedMeshInstanceVisualizationDesc Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizationDescIsValid_Empty,
	"MetaHumanMassCrowd.RepresentationTypes.VisualizationDesc.IsValid.EmptyIsInvalid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisualizationDescIsValid_Empty::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;
	TestFalse(TEXT("Empty desc with no meshes should be invalid"), Desc.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizationDescIsValid_NullMesh,
	"MetaHumanMassCrowd.RepresentationTypes.VisualizationDesc.IsValid.NullMeshIsInvalid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisualizationDescIsValid_NullMesh::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;
	FMassSkinnedMeshInstanceVisualizationMeshDesc MeshDesc;
	// SkeletalMesh is null, ISKMComponentClass is valid (default)
	Desc.Meshes.Add(MeshDesc);

	TestFalse(TEXT("Desc with null SkeletalMesh should be invalid"), Desc.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizationDescEquality,
	"MetaHumanMassCrowd.RepresentationTypes.VisualizationDesc.Equality",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisualizationDescEquality::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc DescA;
	FSkinnedMeshInstanceVisualizationDesc DescB;

	TestTrue(TEXT("Two empty descs should be equal"), DescA == DescB);

	DescA.bUseTransformOffset = true;
	TestFalse(TEXT("Descs with different bUseTransformOffset should not be equal"), DescA == DescB);

	DescB.bUseTransformOffset = true;
	TestTrue(TEXT("Descs with matching bUseTransformOffset should be equal"), DescA == DescB);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizationDescReset,
	"MetaHumanMassCrowd.RepresentationTypes.VisualizationDesc.Reset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisualizationDescReset::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;
	Desc.bUseTransformOffset = true;
	Desc.TransformOffset = FTransform(FRotator(0, -90, 0));
	Desc.CustomDataFloats.Add(1.0f);
	FMassSkinnedMeshInstanceVisualizationMeshDesc MeshDesc;
	Desc.Meshes.Add(MeshDesc);

	Desc.Reset();

	TestFalse(TEXT("bUseTransformOffset should be false after reset"), Desc.bUseTransformOffset);
	TestTrue(TEXT("TransformOffset should be identity after reset"), Desc.TransformOffset.Equals(FTransform::Identity));
	TestEqual(TEXT("CustomDataFloats should be empty after reset"), Desc.CustomDataFloats.Num(), 0);
	TestEqual(TEXT("Meshes should be empty after reset"), Desc.Meshes.Num(), 0);

	return true;
}


//----------------------------------------------------------------------//
// FMetaHumanMassInstanceRepresentation Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceRepresentationDefault,
	"MetaHumanMassCrowd.RepresentationTypes.InstanceRepresentation.Default",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstanceRepresentationDefault::RunTest(const FString& Parameters)
{
	FMetaHumanMassInstanceRepresentation Rep;

	TestNull(TEXT("SourceInstance should be null by default"), Rep.SourceInstance.Get());
	TestFalse(TEXT("DescHandle should be invalid by default"), Rep.DescHandle.IsValid());

	return true;
}


//----------------------------------------------------------------------//
// FMetaHumanMassInstanceRegistryData Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceRegistryDataDefault,
	"MetaHumanMassCrowd.RepresentationTypes.InstanceRegistryData.Default",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstanceRegistryDataDefault::RunTest(const FString& Parameters)
{
	FMetaHumanMassInstanceRegistryData Data;
	TestEqual(TEXT("InstanceRepresentations should be empty by default"), Data.InstanceRepresentations.Num(), 0);
	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshComponentSharedData Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FISKMCSharedDataDefault,
	"MetaHumanMassCrowd.RepresentationTypes.ISKMCSharedData.DefaultConstruction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FISKMCSharedDataDefault::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshComponentSharedData Data;

	TestNull(TEXT("ISKMC should be null by default"), Data.GetMutableInstancedSkinnedMeshComponent());
	TestFalse(TEXT("Should not have updates to apply by default"), Data.HasUpdatesToApply());
	TestFalse(TEXT("RequiresExternalInstanceIDTracking should be false by default"), Data.RequiresExternalInstanceIDTracking());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FISKMCSharedDataReset,
	"MetaHumanMassCrowd.RepresentationTypes.ISKMCSharedData.Reset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FISKMCSharedDataReset::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshComponentSharedData Data;

	// Modify some state
	Data.ResetAccumulatedData(); // just to make sure it doesn't crash on empty data

	Data.Reset();

	TestNull(TEXT("ISKMC should be null after reset"), Data.GetMutableInstancedSkinnedMeshComponent());
	TestFalse(TEXT("Should not have updates after reset"), Data.HasUpdatesToApply());

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshComponentSharedDataMap Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FISKMCSharedDataMapEmpty,
	"MetaHumanMassCrowd.RepresentationTypes.ISKMCSharedDataMap.EmptyState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FISKMCSharedDataMapEmpty::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshComponentSharedDataMap Map;

	TestEqual(TEXT("Num should be 0"), Map.Num(), 0);
	TestEqual(TEXT("NumValid should be 0"), Map.NumValid(), 0);
	TestTrue(TEXT("IsEmpty should be true"), Map.IsEmpty());

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshInfo Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstancedSkinnedMeshInfoDefault,
	"MetaHumanMassCrowd.RepresentationTypes.InstancedSkinnedMeshInfo.DefaultConstruction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstancedSkinnedMeshInfoDefault::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshInfo Info;

	TestEqual(TEXT("Desc Meshes should be empty"), Info.GetDesc().Meshes.Num(), 0);
	TestFalse(TEXT("Should be invalid with no meshes"), Info.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstancedSkinnedMeshInfoReset,
	"MetaHumanMassCrowd.RepresentationTypes.InstancedSkinnedMeshInfo.Reset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstancedSkinnedMeshInfoReset::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;
	Desc.bUseTransformOffset = true;
	FMassInstancedSkinnedMeshInfo Info(Desc);

	TestTrue(TEXT("ShouldUseTransformOffset should be true"), Info.ShouldUseTransformOffset());

	Info.Reset();

	TestFalse(TEXT("ShouldUseTransformOffset should be false after reset"), Info.ShouldUseTransformOffset());
	TestEqual(TEXT("LODSignificanceRanges num should be 0 after reset"), Info.GetLODSignificanceRangesNum(), 0);

	return true;
}


//----------------------------------------------------------------------//
// FMetaHumanAppearanceSharedFragment Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAppearanceSharedFragmentDefault,
	"MetaHumanMassCrowd.Fragments.AppearanceSharedFragment.Default",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAppearanceSharedFragmentDefault::RunTest(const FString& Parameters)
{
	FMetaHumanAppearanceSharedFragment Fragment;

	TestEqual(TEXT("AssignedAppearanceIndices should be empty"), Fragment.AssignedAppearanceIndices.Num(), 0);
	TestEqual(TEXT("CurrentIndex should be 0"), Fragment.CurrentIndex, 0u);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAppearanceSharedFragmentRoundRobin,
	"MetaHumanMassCrowd.Fragments.AppearanceSharedFragment.RoundRobinCycling",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAppearanceSharedFragmentRoundRobin::RunTest(const FString& Parameters)
{
	FMetaHumanAppearanceSharedFragment Fragment;
	Fragment.AssignedAppearanceIndices = { 10, 20, 30 };
	Fragment.CurrentIndex = 0;

	// Simulate the round-robin logic from IdentityInitializer
	TArray<uint32> AssignedValues;
	for (int32 i = 0; i < 6; ++i)
	{
		Fragment.CurrentIndex = (Fragment.CurrentIndex + 1) % Fragment.AssignedAppearanceIndices.Num();
		AssignedValues.Add(Fragment.AssignedAppearanceIndices[Fragment.CurrentIndex]);
	}

	// Should cycle: 20, 30, 10, 20, 30, 10
	TestEqual(TEXT("First assignment"), AssignedValues[0], 20u);
	TestEqual(TEXT("Second assignment"), AssignedValues[1], 30u);
	TestEqual(TEXT("Third assignment wraps"), AssignedValues[2], 10u);
	TestEqual(TEXT("Fourth assignment cycles"), AssignedValues[3], 20u);
	TestEqual(TEXT("Fifth assignment"), AssignedValues[4], 30u);
	TestEqual(TEXT("Sixth assignment wraps again"), AssignedValues[5], 10u);

	return true;
}




//----------------------------------------------------------------------//
// FMassLODInstancedSkinnedMeshSignificanceRange Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLODSignificanceRangeDefault,
	"MetaHumanMassCrowd.RepresentationTypes.LODSignificanceRange.Default",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLODSignificanceRangeDefault::RunTest(const FString& Parameters)
{
	FMassLODInstancedSkinnedMeshSignificanceRange Range;

	TestEqual(TEXT("SkinnedMeshRefs should be empty"), Range.SkinnedMeshComponentRefs.Num(), 0);
	TestNull(TEXT("ISKMCSharedDataPtr should be null"), Range.InstancedSkinnedMeshSharedDataPtr);

	return true;
}


//----------------------------------------------------------------------//
// FSkinnedMeshInstanceVisualizationDesc CustomDataFloats Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizationDescCustomDataFloats,
	"MetaHumanMassCrowd.RepresentationTypes.VisualizationDesc.CustomDataFloats",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisualizationDescCustomDataFloats::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;

	TestEqual(TEXT("CustomDataFloats should be empty by default"), Desc.CustomDataFloats.Num(), 0);

	Desc.CustomDataFloats.Add(1.0f);
	Desc.CustomDataFloats.Add(2.0f);
	Desc.CustomDataFloats.Add(3.0f);

	TestEqual(TEXT("CustomDataFloats should have 3 entries"), Desc.CustomDataFloats.Num(), 3);
	TestEqual(TEXT("First custom float should be 1.0"), Desc.CustomDataFloats[0], 1.0f);
	TestEqual(TEXT("Second custom float should be 2.0"), Desc.CustomDataFloats[1], 2.0f);
	TestEqual(TEXT("Third custom float should be 3.0"), Desc.CustomDataFloats[2], 3.0f);

	return true;
}


//----------------------------------------------------------------------//
// FSkinnedMeshInstanceVisualizationDesc TransformOffset Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualizationDescTransformOffset,
	"MetaHumanMassCrowd.RepresentationTypes.VisualizationDesc.TransformOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVisualizationDescTransformOffset::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;

	TestFalse(TEXT("bUseTransformOffset should be false by default"), Desc.bUseTransformOffset);
	TestTrue(TEXT("TransformOffset should be identity by default"), Desc.TransformOffset.Equals(FTransform::Identity));

	const FTransform TestTransform(FRotator(0, -90, 0));
	Desc.bUseTransformOffset = true;
	Desc.TransformOffset = TestTransform;

	TestTrue(TEXT("bUseTransformOffset should be true"), Desc.bUseTransformOffset);
	TestTrue(TEXT("TransformOffset should match set value"), Desc.TransformOffset.Equals(TestTransform));

	return true;
}


//----------------------------------------------------------------------//
// FMassSkinnedMeshInstanceVisualizationMeshDesc MaterialOverrides Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeshDescMaterialOverrides,
	"MetaHumanMassCrowd.RepresentationTypes.MeshDesc.MaterialOverrides",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMeshDescMaterialOverrides::RunTest(const FString& Parameters)
{
	FMassSkinnedMeshInstanceVisualizationMeshDesc DescA;
	FMassSkinnedMeshInstanceVisualizationMeshDesc DescB;

	// Both empty, should be equal
	TestTrue(TEXT("Descs with empty MaterialOverrides should be equal"), DescA == DescB);

	// Add null material to A only
	DescA.MaterialOverrides.Add(nullptr);
	TestFalse(TEXT("Descs with different MaterialOverrides count should not be equal"), DescA == DescB);

	// Match the count on B
	DescB.MaterialOverrides.Add(nullptr);
	TestTrue(TEXT("Descs with matching null MaterialOverrides should be equal"), DescA == DescB);

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshComponentSharedData Accumulated Data Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FISKMCSharedDataAccumulation,
	"MetaHumanMassCrowd.RepresentationTypes.ISKMCSharedData.AccumulatedData",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FISKMCSharedDataAccumulation::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshComponentSharedData Data;

	// Initially no updates
	TestFalse(TEXT("Should not have updates to apply initially"), Data.HasUpdatesToApply());
	TestEqual(TEXT("Update instance IDs should be empty"), Data.GetEntitiesRequiringUpdate().Num(), 0);
	TestEqual(TEXT("Remove instance IDs should be empty"), Data.GetEntitiesRequiringRemoval().Num(), 0);
	TestEqual(TEXT("Transforms should be empty"), Data.GetMeshInstanceTransforms().Num(), 0);
	TestEqual(TEXT("PrevTransforms should be empty"), Data.GetMeshInstancePrevTransforms().Num(), 0);
	TestEqual(TEXT("CustomFloats should be empty"), Data.GetMeshInstanceCustomFloats().Num(), 0);

	// ResetAccumulatedData should be safe on empty data
	Data.ResetAccumulatedData();
	TestFalse(TEXT("Should still have no updates after reset of empty data"), Data.HasUpdatesToApply());

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshComponentSharedData Touch Counter Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FISKMCSharedDataTouchCounter,
	"MetaHumanMassCrowd.RepresentationTypes.ISKMCSharedData.TouchCounter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FISKMCSharedDataTouchCounter::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshComponentSharedData Data;

	const int16 InitialCounter = Data.GetComponentInstanceIdTouchCounter();
	TestEqual(TEXT("Touch counter should start at 0"), InitialCounter, static_cast<int16>(0));

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshComponentSharedDataMap Add/Remove Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FISKMCSharedDataMapAddRemove,
	"MetaHumanMassCrowd.RepresentationTypes.ISKMCSharedDataMap.AddRemove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FISKMCSharedDataMapAddRemove::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshComponentSharedDataMap Map;

	// Create a test key - use nullptr-based key
	FInstancedSkinnedMeshComponentSharedDataKey TestKey(nullptr);

	// Add an entry
	FMassInstancedSkinnedMeshComponentSharedData& AddedData = Map.Add(TestKey);
	TestEqual(TEXT("Num should be 1 after add"), Map.Num(), 1);
	TestEqual(TEXT("NumValid should be 1 after add"), Map.NumValid(), 1);
	TestFalse(TEXT("IsEmpty should be false after add"), Map.IsEmpty());

	// Find the entry
	FMassInstancedSkinnedMeshComponentSharedData* Found = Map.Find(TestKey);
	TestNotNull(TEXT("Should find the added entry"), Found);

	// Remove the entry
	Map.Remove(TestKey);
	TestEqual(TEXT("NumValid should be 0 after remove"), Map.NumValid(), 0);

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshInfo TransformOffset Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstancedSkinnedMeshInfoTransformOffset,
	"MetaHumanMassCrowd.RepresentationTypes.InstancedSkinnedMeshInfo.TransformOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstancedSkinnedMeshInfoTransformOffset::RunTest(const FString& Parameters)
{
	FSkinnedMeshInstanceVisualizationDesc Desc;
	const FTransform TestTransform(FRotator(0, -90, 0));
	Desc.bUseTransformOffset = true;
	Desc.TransformOffset = TestTransform;

	FMassInstancedSkinnedMeshInfo Info(Desc);

	TestTrue(TEXT("ShouldUseTransformOffset should be true"), Info.ShouldUseTransformOffset());
	TestTrue(TEXT("TransformOffset should match"), Info.GetTransformOffset().Equals(TestTransform));

	return true;
}


//----------------------------------------------------------------------//
// FMassInstancedSkinnedMeshInfo LOD Significance Range Lookup Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstancedSkinnedMeshInfoLODRangeLookup,
	"MetaHumanMassCrowd.RepresentationTypes.InstancedSkinnedMeshInfo.LODRangeLookupOnEmpty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstancedSkinnedMeshInfoLODRangeLookup::RunTest(const FString& Parameters)
{
	FMassInstancedSkinnedMeshInfo Info;

	// With no ranges, lookup should return null
	TestNull(TEXT("LOD range lookup on empty info should return null"), Info.GetLODSignificanceRange(0.0f));
	TestNull(TEXT("LOD range lookup on empty info should return null for high value"), Info.GetLODSignificanceRange(3.0f));
	TestEqual(TEXT("LODSignificanceRangesNum should be 0"), Info.GetLODSignificanceRangesNum(), 0);

	return true;
}


//----------------------------------------------------------------------//
// FMetaHumanAppearanceSharedFragment Single Element Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAppearanceSharedFragmentSingleElement,
	"MetaHumanMassCrowd.Fragments.AppearanceSharedFragment.SingleElement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAppearanceSharedFragmentSingleElement::RunTest(const FString& Parameters)
{
	FMetaHumanAppearanceSharedFragment Fragment;
	Fragment.AssignedAppearanceIndices = { 5 };
	Fragment.CurrentIndex = 0;

	// With a single element, round-robin should always return the same index
	TArray<uint32> AssignedValues;
	for (int32 i = 0; i < 3; ++i)
	{
		Fragment.CurrentIndex = (Fragment.CurrentIndex + 1) % Fragment.AssignedAppearanceIndices.Num();
		AssignedValues.Add(Fragment.AssignedAppearanceIndices[Fragment.CurrentIndex]);
	}

	TestEqual(TEXT("First assignment with single element"), AssignedValues[0], 5u);
	TestEqual(TEXT("Second assignment with single element"), AssignedValues[1], 5u);
	TestEqual(TEXT("Third assignment with single element"), AssignedValues[2], 5u);

	return true;
}


//----------------------------------------------------------------------//
// FMetaHumanMassInstanceRepresentation DescHandle Tests
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInstanceRepresentationDescHandle,
	"MetaHumanMassCrowd.RepresentationTypes.InstanceRepresentation.DescHandleValidity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FInstanceRepresentationDescHandle::RunTest(const FString& Parameters)
{
	FMetaHumanMassInstanceRepresentation Rep;

	// Default handle should be invalid
	TestFalse(TEXT("Default DescHandle should be invalid"), Rep.DescHandle.IsValid());

	// Construct a handle with a valid index
	FSkinnedMeshInstanceVisualizationDescHandle ValidHandle(0);
	Rep.DescHandle = ValidHandle;
	TestTrue(TEXT("DescHandle with index 0 should be valid"), Rep.DescHandle.IsValid());

	// Check ToIndex works
	TestEqual(TEXT("DescHandle ToIndex should return 0"), Rep.DescHandle.ToIndex(), 0);

	return true;
}


#endif // WITH_AUTOMATION_TESTS
