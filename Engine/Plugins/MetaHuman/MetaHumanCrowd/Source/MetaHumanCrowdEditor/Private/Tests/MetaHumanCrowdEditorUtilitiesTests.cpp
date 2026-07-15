// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "BoneWeights.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "MetaHumanCrowdEditorUtilities.h"
#include "MetaHumanCrowdTypes.h"

//----------------------------------------------------------------------//
// End-to-end tests for ConstructMeshFromBundle's section-to-material resolution.
//
// What these tests pin (the observable contract of the build path):
//
//   For every (LOD, Section) on the built USkeletalMesh, applying the engine's
//   render-time material resolution rule
//
//       Resolved = LODMaterialMap[SectionIdx] != INDEX_NONE
//                      ? LODMaterialMap[SectionIdx]
//                      : Section.MaterialIndex
//
//   yields the index in Mesh->GetMaterials() whose MaterialSlotName matches the
//   PG slot name we set in the source bundle.
//
// Why end-to-end: ConstructMeshFromBundle's Step 5b-engine-namematch contains a
// branch that depends on engine-internal behaviour (name-based remapping in the
// LOD 0 build path). If the engine ever stops doing that name-match, replaces
// it with something else, or moves the rewrite somewhere new, the test must
// still catch it. So the assertions check the final material resolution on the
// built mesh, not the helper that does the remapping.
//
// Cases:
//   A. CanonicalNamesMatch          - exercises the "engine remapped for us" branch.
//   B. SlotRenamedAfterImport       - exercises the "engine bailed, we filled the
//                                     LODMaterialMap" branch (the one the body-merge
//                                     bug uncovered).
//   C. CrossLODDedup                - LOD 1 sections (engine never remaps LOD 1+).
//   D. MultipleSectionsSameMaterial - two PGs deduping to one CompactedMaterials entry.
//   E. EmptyPolygonGroup            - empty PG whose canonical slot name does not
//                                     appear elsewhere in CompactedMaterials. This is
//                                     the shape that makes the engine name-match bail
//                                     in real Production-quality body-merge bundles.
//----------------------------------------------------------------------//

namespace UE::MetaHuman::CrowdEditorUtilities::Tests
{

namespace
{

constexpr const TCHAR* RootBoneName = TEXT("Root");

/**
 * Append a single triangle to MD assigned to the given polygon group.
 * The triangle is a simple xy-plane triangle, offset by Offset along x to avoid
 * coincident vertices across multiple triangles in the same MeshDescription.
 */
void AppendTriangle(FMeshDescription& MD, FPolygonGroupID PGID, float Offset)
{
	FStaticMeshAttributes Attrs(MD);
	TVertexAttributesRef<FVector3f> VertexPositions = Attrs.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attrs.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attrs.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceTangents = Attrs.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float>     VertexInstanceBinormalSigns = Attrs.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attrs.GetVertexInstanceColors();

	const FVertexID V0 = MD.CreateVertex();
	const FVertexID V1 = MD.CreateVertex();
	const FVertexID V2 = MD.CreateVertex();
	VertexPositions[V0] = FVector3f(Offset + 0.0f, 0.0f, 0.0f);
	VertexPositions[V1] = FVector3f(Offset + 1.0f, 0.0f, 0.0f);
	VertexPositions[V2] = FVector3f(Offset + 0.0f, 1.0f, 0.0f);

	const FVertexInstanceID VI0 = MD.CreateVertexInstance(V0);
	const FVertexInstanceID VI1 = MD.CreateVertexInstance(V1);
	const FVertexInstanceID VI2 = MD.CreateVertexInstance(V2);
	for (const FVertexInstanceID VI : {VI0, VI1, VI2})
	{
		VertexInstanceUVs.Set(VI, 0, FVector2f(0.0f, 0.0f));
		VertexInstanceNormals[VI] = FVector3f(0.0f, 0.0f, 1.0f);
		VertexInstanceTangents[VI] = FVector3f(1.0f, 0.0f, 0.0f);
		VertexInstanceBinormalSigns[VI] = 1.0f;
		VertexInstanceColors[VI] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	}

	MD.CreateTriangle(PGID, {VI0, VI1, VI2});
}

/**
 * Initialise a MeshDescription with the skeletal mesh attribute schema, register
 * the named polygon groups (in order), and weight all subsequent vertices to a
 * single root bone. Returns the polygon group IDs in the same order as InSlotNames.
 */
TArray<FPolygonGroupID> InitMeshDescription(FMeshDescription& MD, TConstArrayView<FName> InSlotNames)
{
	FSkeletalMeshAttributes SkelAttrs(MD);
	SkelAttrs.Register();

	// Register one bone matching our root-only skeleton.
	const FBoneID BoneID = SkelAttrs.CreateBone();
	SkelAttrs.GetBoneNames().Set(BoneID, FName(RootBoneName));
	SkelAttrs.GetBoneParentIndices().Set(BoneID, INDEX_NONE);
	SkelAttrs.GetBonePoses().Set(BoneID, FTransform::Identity);

	TArray<FPolygonGroupID> PGIDs;
	PGIDs.Reserve(InSlotNames.Num());
	for (const FName SlotName : InSlotNames)
	{
		const FPolygonGroupID PGID = MD.CreatePolygonGroup();
		SkelAttrs.GetPolygonGroupMaterialSlotNames().Set(PGID, SlotName);
		PGIDs.Add(PGID);
	}

	return PGIDs;
}

/**
 * Set skin weights on every vertex in MD: 100% to the single root bone (BoneIndex=0).
 * Must be called after all triangles have been appended.
 */
void WeightAllVerticesToRoot(FMeshDescription& MD)
{
	FSkeletalMeshAttributes SkelAttrs(MD);
	FSkinWeightsVertexAttributesRef SkinWeights = SkelAttrs.GetVertexSkinWeights();

	const UE::AnimationCore::FBoneWeight RootWeight(/*BoneIndex=*/0, 1.0f);
	for (const FVertexID VertexID : MD.Vertices().GetElementIDs())
	{
		SkinWeights.Set(VertexID, MakeArrayView(&RootWeight, 1));
	}
}

/** Build a one-bone reference skeleton for Bundle.RefSkeleton (used to look up
 *  bone names during pruning in ConstructMeshFromBundle). */
FReferenceSkeleton MakeRootOnlyRefSkeleton(USkeleton* TargetSkeleton)
{
	FReferenceSkeleton RefSkel;
	{
		FReferenceSkeletonModifier Modifier(RefSkel, TargetSkeleton);
		Modifier.Add(FMeshBoneInfo(FName(RootBoneName), FString(RootBoneName), INDEX_NONE), FTransform::Identity);
	}
	return RefSkel;
}

/** Build a fresh transient USkeleton with a single root bone. */
USkeleton* MakeRootOnlySkeleton(UPackage* Outer)
{
	USkeleton* Skeleton = NewObject<USkeleton>(Outer, NAME_None, RF_Transient);
	{
		// FReferenceSkeletonModifier(USkeleton*) writes directly into the skeleton's own ref skeleton.
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(FName(RootBoneName), FString(RootBoneName), INDEX_NONE), FTransform::Identity);
	}
	return Skeleton;
}

FSkeletalMaterial MakeMaterial(FName MaterialSlotName, FName ImportedMaterialSlotName)
{
	FSkeletalMaterial M;
	M.MaterialSlotName = MaterialSlotName;
	M.ImportedMaterialSlotName = ImportedMaterialSlotName;
	M.MaterialInterface = nullptr;
	return M;
}

/**
 * Apply the engine's render-time material resolution rule to a built section:
 *   LODMaterialMap[SectionIdx] if present and != INDEX_NONE, else Section.MaterialIndex.
 */
int32 ResolveBuiltSectionMaterial(const FSkeletalMeshLODInfo& LODInfo, const FSkelMeshSection& Section, int32 SectionIdx)
{
	if (LODInfo.LODMaterialMap.IsValidIndex(SectionIdx))
	{
		const int32 Mapped = LODInfo.LODMaterialMap[SectionIdx];
		if (Mapped != INDEX_NONE)
		{
			return Mapped;
		}
	}
	return Section.MaterialIndex;
}

/** A built section's expected polygon-group slot name, indexed by built-section order. */
struct FSectionExpectation
{
	int32 LODIndex = 0;
	int32 SectionIdx = 0;
	FName ExpectedSlotName;
};

/**
 * For every (LOD, Section) on Mesh, resolve its material via the renderer rule and
 * assert that the resolved material's MaterialSlotName matches the expectation for
 * that section.
 *
 * Expectations are matched by (LODIndex, SectionIdx). The test expectations only
 * need to specify each section that is expected to exist; any section in the built
 * mesh without a matching expectation is reported as a failure.
 */
void VerifyBuiltMeshMaterials(
	FAutomationTestBase& Test,
	const TCHAR* CaseLabel,
	USkeletalMesh* Mesh,
	TConstArrayView<FSectionExpectation> Expectations)
{
	if (!Test.TestNotNull(*FString::Printf(TEXT("%s: ConstructMeshFromBundle returned non-null mesh"), CaseLabel), Mesh))
	{
		return;
	}

	const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
	if (!Test.TestNotNull(*FString::Printf(TEXT("%s: built mesh has imported model"), CaseLabel), ImportedModel))
	{
		return;
	}

	const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();

	int32 ExpectationsConsumed = 0;
	for (int32 LODIndex = 0; LODIndex < ImportedModel->LODModels.Num(); ++LODIndex)
	{
		const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
		Test.TestNotNull(*FString::Printf(TEXT("%s: LOD %d has LODInfo"), CaseLabel, LODIndex), LODInfo);
		if (!LODInfo)
		{
			continue;
		}

		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		for (int32 SectionIdx = 0; SectionIdx < LODModel.Sections.Num(); ++SectionIdx)
		{
			const FSkelMeshSection& Section = LODModel.Sections[SectionIdx];
			const int32 Resolved = ResolveBuiltSectionMaterial(*LODInfo, Section, SectionIdx);

			// Locate matching expectation by (LOD, SectionIdx).
			const FSectionExpectation* Expected = Expectations.FindByPredicate(
				[LODIndex, SectionIdx](const FSectionExpectation& E)
				{
					return E.LODIndex == LODIndex && E.SectionIdx == SectionIdx;
				});

			if (!Test.TestNotNull(
				*FString::Printf(TEXT("%s: LOD %d section %d has an expectation"), CaseLabel, LODIndex, SectionIdx),
				Expected))
			{
				continue;
			}
			++ExpectationsConsumed;

			Test.TestTrue(
				*FString::Printf(
					TEXT("%s: LOD %d section %d resolved material index (%d) is in range (Materials.Num()=%d)"),
					CaseLabel, LODIndex, SectionIdx, Resolved, Materials.Num()),
				Materials.IsValidIndex(Resolved));
			if (!Materials.IsValidIndex(Resolved))
			{
				continue;
			}

			Test.TestEqual(
				*FString::Printf(
					TEXT("%s: LOD %d section %d resolved MaterialSlotName"),
					CaseLabel, LODIndex, SectionIdx),
				Materials[Resolved].MaterialSlotName,
				Expected->ExpectedSlotName);
		}
	}

	Test.TestEqual(
		*FString::Printf(TEXT("%s: number of built sections matches expectation count"), CaseLabel),
		ExpectationsConsumed, Expectations.Num());
}

/**
 * Scoped owner: a transient package that owns the test mesh + skeleton so they get
 * GC'd cleanly when the test ends. Use TStrongObjectPtr to keep them alive across
 * the inevitable PostEditChange / build pump that happens inside ConstructMeshFromBundle.
 */
struct FTestOwner
{
	TStrongObjectPtr<UPackage> Package;
	TStrongObjectPtr<USkeleton> Skeleton;

	FTestOwner()
	{
		Package = TStrongObjectPtr<UPackage>(NewObject<UPackage>(GetTransientPackage(), NAME_None, RF_Transient));
		Skeleton = TStrongObjectPtr<USkeleton>(MakeRootOnlySkeleton(Package.Get()));
	}
};

} // namespace


//----------------------------------------------------------------------//
// Test A: CanonicalNamesMatch
//
// Every PG slot name == Bundle.Materials[k].MaterialSlotName == ImportedMaterialSlotName.
// In this configuration the engine's LOD 0 name match succeeds; built LOD 0 sections
// carry CompactedMaterials indices directly and Step 11 leaves LODMaterialMap empty.
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowdConstructMeshFromBundleCanonicalNamesMatch,
	"MetaHuman.Crowd.ConstructMeshFromBundle.SectionMaterialResolution.CanonicalNamesMatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCrowdConstructMeshFromBundleCanonicalNamesMatch::RunTest(const FString& Parameters)
{
	using namespace UE::MetaHuman::CrowdEditorUtilities::Tests;

	FTestOwner Owner;

	const FName SlotA(TEXT("SlotA"));
	const FName SlotB(TEXT("SlotB"));

	FMetaHumanCrowdMeshGeometryBundle Bundle;
	Bundle.RefSkeleton = MakeRootOnlyRefSkeleton(Owner.Skeleton.Get());
	Bundle.Materials = {
		MakeMaterial(/*MaterialSlotName=*/SlotA, /*ImportedMaterialSlotName=*/SlotA),
		MakeMaterial(/*MaterialSlotName=*/SlotB, /*ImportedMaterialSlotName=*/SlotB),
	};
	Bundle.MeshDescriptions.SetNum(1);
	{
		FMeshDescription& MD = Bundle.MeshDescriptions[0];
		const TArray<FPolygonGroupID> PGIDs = InitMeshDescription(MD, {SlotA, SlotB});
		AppendTriangle(MD, PGIDs[0], /*Offset=*/0.0f);
		AppendTriangle(MD, PGIDs[1], /*Offset=*/2.0f);
		WeightAllVerticesToRoot(MD);
	}
	Bundle.LODMaterialMaps.SetNum(1);
	Bundle.LODMaterialMaps[0] = {0, 1};

	UE::MetaHuman::CrowdEditorUtilities::FMeshConstructionParams Params;
	Params.LODsToKeep = {0};
	Params.TargetSkeleton = Owner.Skeleton.Get();

	USkeletalMesh* Mesh = UE::MetaHuman::CrowdEditorUtilities::ConstructMeshFromBundle(
		Bundle, Params, Owner.Package.Get(), TEXT("TestMesh_CanonicalNamesMatch"));

	const FSectionExpectation Expectations[] = {
		{0, 0, SlotA},
		{0, 1, SlotB},
	};
	VerifyBuiltMeshMaterials(*this, TEXT("CanonicalNamesMatch"), Mesh, Expectations);

	return true;
}


//----------------------------------------------------------------------//
// Test B: SlotRenamedAfterImport
//
// Bundle.Materials[k].MaterialSlotName != ImportedMaterialSlotName. After
// canonicalisation, PG slot names equal MaterialSlotName, which does not match any
// CompactedMaterials[i].ImportedMaterialSlotName, so the engine name-match bails.
// Step 11 then has to populate LODMaterialMap correctly.
//
// This is the configuration that uncovered the original body-merge bug: an extra
// LODMaterialMap remap after the engine had already rewritten faces would corrupt
// resolution, and the inverse case (no remap when the engine bailed) would corrupt
// resolution too. The fix predicts which branch the engine takes; this test pins
// the resulting render-time resolution in the bail-out branch.
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowdConstructMeshFromBundleSlotRenamedAfterImport,
	"MetaHuman.Crowd.ConstructMeshFromBundle.SectionMaterialResolution.SlotRenamedAfterImport",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCrowdConstructMeshFromBundleSlotRenamedAfterImport::RunTest(const FString& Parameters)
{
	using namespace UE::MetaHuman::CrowdEditorUtilities::Tests;

	FTestOwner Owner;

	const FName CanonicalSlotA(TEXT("RenamedSlotA"));
	const FName ImportedSlotA(TEXT("OriginalImportNameA"));
	const FName CanonicalSlotB(TEXT("RenamedSlotB"));
	const FName ImportedSlotB(TEXT("OriginalImportNameB"));

	FMetaHumanCrowdMeshGeometryBundle Bundle;
	Bundle.RefSkeleton = MakeRootOnlyRefSkeleton(Owner.Skeleton.Get());
	Bundle.Materials = {
		MakeMaterial(/*MaterialSlotName=*/CanonicalSlotA, /*ImportedMaterialSlotName=*/ImportedSlotA),
		MakeMaterial(/*MaterialSlotName=*/CanonicalSlotB, /*ImportedMaterialSlotName=*/ImportedSlotB),
	};
	Bundle.MeshDescriptions.SetNum(1);
	{
		FMeshDescription& MD = Bundle.MeshDescriptions[0];
		// PG slot names use MaterialSlotName (post-rename). Step 5a in
		// ConstructMeshFromBundle would rewrite them to MaterialSlotName regardless,
		// using LODMaterialMaps to resolve which Bundle.Materials entry applies.
		const TArray<FPolygonGroupID> PGIDs = InitMeshDescription(MD, {CanonicalSlotA, CanonicalSlotB});
		AppendTriangle(MD, PGIDs[0], /*Offset=*/0.0f);
		AppendTriangle(MD, PGIDs[1], /*Offset=*/2.0f);
		WeightAllVerticesToRoot(MD);
	}
	Bundle.LODMaterialMaps.SetNum(1);
	Bundle.LODMaterialMaps[0] = {0, 1};

	UE::MetaHuman::CrowdEditorUtilities::FMeshConstructionParams Params;
	Params.LODsToKeep = {0};
	Params.TargetSkeleton = Owner.Skeleton.Get();

	USkeletalMesh* Mesh = UE::MetaHuman::CrowdEditorUtilities::ConstructMeshFromBundle(
		Bundle, Params, Owner.Package.Get(), TEXT("TestMesh_SlotRenamed"));

	const FSectionExpectation Expectations[] = {
		{0, 0, CanonicalSlotA},
		{0, 1, CanonicalSlotB},
	};
	VerifyBuiltMeshMaterials(*this, TEXT("SlotRenamedAfterImport"), Mesh, Expectations);

	return true;
}


//----------------------------------------------------------------------//
// Test C: CrossLODDedup
//
// Two LODs: LOD 0 has {SlotA, SlotB}, LOD 1 has {SlotB, SlotC}. The engine's name
// match runs only on LOD 0 (and only on Materials.Num() > 1). LOD 1 sections always
// keep PG IDs; Step 11's PerLODMaterialRemap must produce the right LODMaterialMap
// for LOD 1 even when SlotB is shared across LODs and SlotC is unique to LOD 1.
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowdConstructMeshFromBundleCrossLODDedup,
	"MetaHuman.Crowd.ConstructMeshFromBundle.SectionMaterialResolution.CrossLODDedup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCrowdConstructMeshFromBundleCrossLODDedup::RunTest(const FString& Parameters)
{
	using namespace UE::MetaHuman::CrowdEditorUtilities::Tests;

	FTestOwner Owner;

	const FName SlotA(TEXT("SlotA"));
	const FName SlotB(TEXT("SlotB"));
	const FName SlotC(TEXT("SlotC"));

	FMetaHumanCrowdMeshGeometryBundle Bundle;
	Bundle.RefSkeleton = MakeRootOnlyRefSkeleton(Owner.Skeleton.Get());
	Bundle.Materials = {
		MakeMaterial(SlotA, SlotA),
		MakeMaterial(SlotB, SlotB),
		MakeMaterial(SlotC, SlotC),
	};

	Bundle.MeshDescriptions.SetNum(2);
	{
		FMeshDescription& MD0 = Bundle.MeshDescriptions[0];
		const TArray<FPolygonGroupID> PG0 = InitMeshDescription(MD0, {SlotA, SlotB});
		AppendTriangle(MD0, PG0[0], 0.0f);
		AppendTriangle(MD0, PG0[1], 2.0f);
		WeightAllVerticesToRoot(MD0);

		FMeshDescription& MD1 = Bundle.MeshDescriptions[1];
		const TArray<FPolygonGroupID> PG1 = InitMeshDescription(MD1, {SlotB, SlotC});
		AppendTriangle(MD1, PG1[0], 0.0f);
		AppendTriangle(MD1, PG1[1], 2.0f);
		WeightAllVerticesToRoot(MD1);
	}
	Bundle.LODMaterialMaps.SetNum(2);
	Bundle.LODMaterialMaps[0] = {0, 1}; // LOD 0: PG 0 -> SlotA, PG 1 -> SlotB
	Bundle.LODMaterialMaps[1] = {1, 2}; // LOD 1: PG 0 -> SlotB, PG 1 -> SlotC

	UE::MetaHuman::CrowdEditorUtilities::FMeshConstructionParams Params;
	Params.LODsToKeep = {0, 1};
	Params.TargetSkeleton = Owner.Skeleton.Get();

	USkeletalMesh* Mesh = UE::MetaHuman::CrowdEditorUtilities::ConstructMeshFromBundle(
		Bundle, Params, Owner.Package.Get(), TEXT("TestMesh_CrossLODDedup"));

	const FSectionExpectation Expectations[] = {
		{0, 0, SlotA},
		{0, 1, SlotB},
		{1, 0, SlotB},
		{1, 1, SlotC},
	};
	VerifyBuiltMeshMaterials(*this, TEXT("CrossLODDedup"), Mesh, Expectations);

	return true;
}


//----------------------------------------------------------------------//
// Test D: MultipleSectionsSameMaterial
//
// Two PGs in LOD 0 share the same canonical slot name. Step 5b-remap dedupes
// them to a single CompactedMaterials entry, and the engine build also merges
// same-material sections into one. Real example: fitted outfits where Chaos
// export emits one PG per source LOD per material with the same slot name across
// PGs. The contract is that the merged section resolves to the shared material.
//
// NOTE: the engine's section-merge in BuildSkeletalMesh combines the two
// SlotShared PGs into a single built section, so the source has 3 PGs but the
// built mesh has 2 sections. This is engine-level behaviour, not Step 11.
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowdConstructMeshFromBundleMultipleSectionsSameMaterial,
	"MetaHuman.Crowd.ConstructMeshFromBundle.SectionMaterialResolution.MultipleSectionsSameMaterial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCrowdConstructMeshFromBundleMultipleSectionsSameMaterial::RunTest(const FString& Parameters)
{
	using namespace UE::MetaHuman::CrowdEditorUtilities::Tests;

	FTestOwner Owner;

	const FName SlotShared(TEXT("SharedSlot"));
	const FName SlotOther(TEXT("OtherSlot"));

	FMetaHumanCrowdMeshGeometryBundle Bundle;
	Bundle.RefSkeleton = MakeRootOnlyRefSkeleton(Owner.Skeleton.Get());
	Bundle.Materials = {
		MakeMaterial(SlotShared, SlotShared),
		MakeMaterial(SlotOther, SlotOther),
	};
	Bundle.MeshDescriptions.SetNum(1);
	{
		FMeshDescription& MD = Bundle.MeshDescriptions[0];
		// Two PGs with SlotShared (split sections sharing one material), one with SlotOther.
		const TArray<FPolygonGroupID> PGIDs = InitMeshDescription(MD, {SlotShared, SlotShared, SlotOther});
		AppendTriangle(MD, PGIDs[0], 0.0f);
		AppendTriangle(MD, PGIDs[1], 2.0f);
		AppendTriangle(MD, PGIDs[2], 4.0f);
		WeightAllVerticesToRoot(MD);
	}
	Bundle.LODMaterialMaps.SetNum(1);
	Bundle.LODMaterialMaps[0] = {0, 0, 1}; // both shared PGs -> Materials[0], other -> Materials[1]

	UE::MetaHuman::CrowdEditorUtilities::FMeshConstructionParams Params;
	Params.LODsToKeep = {0};
	Params.TargetSkeleton = Owner.Skeleton.Get();

	USkeletalMesh* Mesh = UE::MetaHuman::CrowdEditorUtilities::ConstructMeshFromBundle(
		Bundle, Params, Owner.Package.Get(), TEXT("TestMesh_MultipleSectionsSameMaterial"));

	const FSectionExpectation Expectations[] = {
		{0, 0, SlotShared},
		{0, 1, SlotOther},
	};
	VerifyBuiltMeshMaterials(*this, TEXT("MultipleSectionsSameMaterial"), Mesh, Expectations);

	return true;
}


//----------------------------------------------------------------------//
// Test E: EmptyPolygonGroup
//
// LOD 0 has an empty polygon group whose canonical slot name does not appear in
// any other PG (so it does not get represented in CompactedMaterials by Step 5b-remap).
// FSkeletalMeshImportData::CreateFromMeshDescription emits one entry per PG including
// empty ones, so the engine's LOD 0 name match sees the empty PG's slot name and bails
// (the slot isn't in CompactedMaterials). This is the realistic body-merge bundle
// shape -- a section gets removed earlier in Step 5b but its PG remains.
//
// In this configuration Step 11 must populate LODMaterialMap for LOD 0, which is the
// branch that the engine-namematch predictor labels as "false". The test pins that
// the non-empty sections still resolve correctly.
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowdConstructMeshFromBundleEmptyPolygonGroup,
	"MetaHuman.Crowd.ConstructMeshFromBundle.SectionMaterialResolution.EmptyPolygonGroup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCrowdConstructMeshFromBundleEmptyPolygonGroup::RunTest(const FString& Parameters)
{
	using namespace UE::MetaHuman::CrowdEditorUtilities::Tests;

	FTestOwner Owner;

	const FName SlotA(TEXT("SlotA"));
	const FName SlotB(TEXT("SlotB"));
	const FName SlotEmpty(TEXT("SlotEmptyOnly")); // Not present in any non-empty PG.

	FMetaHumanCrowdMeshGeometryBundle Bundle;
	Bundle.RefSkeleton = MakeRootOnlyRefSkeleton(Owner.Skeleton.Get());
	Bundle.Materials = {
		MakeMaterial(SlotA, SlotA),
		MakeMaterial(SlotB, SlotB),
		MakeMaterial(SlotEmpty, SlotEmpty), // Material entry exists; no triangles reference it.
	};
	Bundle.MeshDescriptions.SetNum(1);
	{
		FMeshDescription& MD = Bundle.MeshDescriptions[0];
		// Three PGs, but only the first two get triangles. The third is empty.
		const TArray<FPolygonGroupID> PGIDs = InitMeshDescription(MD, {SlotA, SlotB, SlotEmpty});
		AppendTriangle(MD, PGIDs[0], 0.0f);
		AppendTriangle(MD, PGIDs[1], 2.0f);
		// PGIDs[2] intentionally has no triangles.
		WeightAllVerticesToRoot(MD);
	}
	Bundle.LODMaterialMaps.SetNum(1);
	Bundle.LODMaterialMaps[0] = {0, 1, 2};

	UE::MetaHuman::CrowdEditorUtilities::FMeshConstructionParams Params;
	Params.LODsToKeep = {0};
	Params.TargetSkeleton = Owner.Skeleton.Get();

	USkeletalMesh* Mesh = UE::MetaHuman::CrowdEditorUtilities::ConstructMeshFromBundle(
		Bundle, Params, Owner.Package.Get(), TEXT("TestMesh_EmptyPolygonGroup"));

	// The empty PG produces no built section. Only SlotA / SlotB sections remain.
	const FSectionExpectation Expectations[] = {
		{0, 0, SlotA},
		{0, 1, SlotB},
	};
	VerifyBuiltMeshMaterials(*this, TEXT("EmptyPolygonGroup"), Mesh, Expectations);

	return true;
}

//----------------------------------------------------------------------//
// Test F: SectionRemovedAtRuntime
//
// Realistic body-merge / outfit pipeline shape: Params.SectionsToRemove deletes
// polygons from a PG at runtime in Step 5b. The empty PG stays in the
// MeshDescription, so FSkeletalMeshImportData::CreateFromMeshDescription emits
// an entry for it and the engine's LOD 0 name match has to find it (or bail).
// This is the path that produced the body-merge actor-mesh material bug.
//
// Configuration: three PGs (SlotKept, SlotRemoved, SlotOther). SectionsToRemove
// strips SlotRemoved's geometry. The remaining sections must still resolve to
// SlotKept and SlotOther correctly.
//----------------------------------------------------------------------//

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrowdConstructMeshFromBundleSectionRemovedAtRuntime,
	"MetaHuman.Crowd.ConstructMeshFromBundle.SectionMaterialResolution.SectionRemovedAtRuntime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCrowdConstructMeshFromBundleSectionRemovedAtRuntime::RunTest(const FString& Parameters)
{
	using namespace UE::MetaHuman::CrowdEditorUtilities::Tests;

	FTestOwner Owner;

	const FName SlotKept(TEXT("SlotKept"));
	const FName SlotRemoved(TEXT("SlotRemoved"));
	const FName SlotOther(TEXT("SlotOther"));

	FMetaHumanCrowdMeshGeometryBundle Bundle;
	Bundle.RefSkeleton = MakeRootOnlyRefSkeleton(Owner.Skeleton.Get());
	Bundle.Materials = {
		MakeMaterial(SlotKept, SlotKept),
		MakeMaterial(SlotRemoved, SlotRemoved),
		MakeMaterial(SlotOther, SlotOther),
	};
	Bundle.MeshDescriptions.SetNum(1);
	{
		FMeshDescription& MD = Bundle.MeshDescriptions[0];
		const TArray<FPolygonGroupID> PGIDs = InitMeshDescription(MD, {SlotKept, SlotRemoved, SlotOther});
		AppendTriangle(MD, PGIDs[0], 0.0f);
		AppendTriangle(MD, PGIDs[1], 2.0f);
		AppendTriangle(MD, PGIDs[2], 4.0f);
		WeightAllVerticesToRoot(MD);
	}
	Bundle.LODMaterialMaps.SetNum(1);
	Bundle.LODMaterialMaps[0] = {0, 1, 2};

	::UE::MetaHuman::CrowdEditorUtilities::FMeshConstructionParams Params;
	Params.LODsToKeep = {0};
	Params.TargetSkeleton = Owner.Skeleton.Get();
	// Step 5b removes polygons of this PG at runtime. The empty PG remains in the
	// MeshDescription, so the engine's LOD 0 name match will see its slot name --
	// exactly the shape that motivates the engine-namematch predictor.
	Params.SectionsToRemove = {SlotRemoved};

	USkeletalMesh* Mesh = ::UE::MetaHuman::CrowdEditorUtilities::ConstructMeshFromBundle(
		Bundle, Params, Owner.Package.Get(), TEXT("TestMesh_SectionRemoved"));

	const FSectionExpectation Expectations[] = {
		{0, 0, SlotKept},
		{0, 1, SlotOther},
	};
	VerifyBuiltMeshMaterials(*this, TEXT("SectionRemovedAtRuntime"), Mesh, Expectations);

	return true;
}

} // namespace UE::MetaHuman::CrowdEditorUtilities::Tests

#endif // WITH_AUTOMATION_TESTS
