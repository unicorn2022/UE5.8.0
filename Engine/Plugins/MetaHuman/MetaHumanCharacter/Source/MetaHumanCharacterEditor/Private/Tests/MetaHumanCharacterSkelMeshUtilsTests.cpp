// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Engine/SkeletalMesh.h"
#include "SkelMeshDNAUtils.h"
#include "DNA.h"
#include "DNAReader.h"
#include "DNAToSkelMeshMap.h"

#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanCharacterEditorSubsystem.h"

// -------------------------------------------------------------------------
// FMetaHumanUpdateSkelMeshFromDNATest
//
// Directly tests FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA
// by running it against the face archetype mesh and its embedded DNA, then
// verifying that every vertex position and normal in the LOD model matches
// the corresponding values returned by the DNA reader.
//
// This is a pure editor-only test that does not require the MetaHuman
// subsystem, an actor, or any network/cloud calls.
// -------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetaHumanUpdateSkelMeshFromDNATest,
	"MetaHumanCreator.SkelMeshUtils.UpdateSkelMeshFromDNA",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMetaHumanUpdateSkelMeshFromDNATest::RunTest(const FString& InParams)
{
	// -----------------------------------------------------------------------
	// 1. Load the face archetype skeletal mesh and its embedded DNA reader.
	// -----------------------------------------------------------------------
	USkeletalMesh* FaceArchetypeMesh = UMetaHumanCharacterEditorSubsystem::GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType::MetaHuman);
	UTEST_NOT_NULL("Face archetype mesh loaded", FaceArchetypeMesh);

	UDNA* DNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(FaceArchetypeMesh);
	UTEST_NOT_NULL("Face archetype DNA asset", DNAAsset);

	TSharedPtr<IDNAReader> DNAReader = DNAAsset->GetDNAReader();
	UTEST_VALID("Face archetype DNA reader valid", DNAReader);

	// -----------------------------------------------------------------------
	// 2. Build the vertex map and call UpdateSkelMeshFromDNA with BaseMesh only.
	//    This exercises UpdateBaseMesh — the path that writes positions and
	//    normals — without touching joints or skin weights.
	// -----------------------------------------------------------------------
	TSharedRef<FDNAToSkelMeshMap> DNAToSkelMeshMap =
		MakeShared<FDNAToSkelMeshMap>(*USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(FaceArchetypeMesh));

	FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA(
		DNAReader.ToSharedRef(),
		FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::BaseMesh,
		DNAToSkelMeshMap,
		EMetaHumanCharacterOrientation::Y_UP,
		FaceArchetypeMesh);

	// -----------------------------------------------------------------------
	// 3. Build a position-to-normal index map for each DNA mesh (same logic as
	//    UpdateBaseMesh itself) so we can look up the expected normal per vertex.
	// -----------------------------------------------------------------------
	FSkeletalMeshModel* ImportedModel = FaceArchetypeMesh->GetImportedModel();
	UTEST_NOT_NULL("Imported model valid", ImportedModel);

	const int32 MeshCount = DNAReader->GetMeshCount();

	for (int32 LODIndex = 0; LODIndex < ImportedModel->LODModels.Num(); ++LODIndex)
	{
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		int32 SectionIndex = 0;

		for (const FSkelMeshSection& Section : LODModel.Sections)
		{
			const int32 DNAMeshIndex = DNAToSkelMeshMap->ImportVtxToDNAMeshIndex[LODIndex][Section.GetVertexBufferIndex()];
			if (DNAMeshIndex < 0 || DNAMeshIndex >= MeshCount)
			{
				++SectionIndex;
				continue;
			}

			// Build position -> normal index map for this DNA mesh.
			const int32 NumPositions = static_cast<int32>(DNAReader->GetVertexPositionCount(DNAMeshIndex));
			TArray<int32> DNAPositionToNormalIndex;
			DNAPositionToNormalIndex.Init(INDEX_NONE, NumPositions);

			const int32 NumLayouts = static_cast<int32>(DNAReader->GetVertexLayoutCount(DNAMeshIndex));
			for (int32 LayoutIndex = 0; LayoutIndex < NumLayouts; ++LayoutIndex)
			{
				const FVertexLayout Layout = DNAReader->GetVertexLayout(DNAMeshIndex, LayoutIndex);
				if (Layout.Position >= 0 && Layout.Position < NumPositions && Layout.Normal >= 0)
				{
					DNAPositionToNormalIndex[Layout.Position] = Layout.Normal;
				}
			}

			// Verify each vertex in this section.
			const int32 NumSoftVertices = Section.GetNumVertices();
			int32 VertexBufferIndex = Section.GetVertexBufferIndex();

			for (int32 VertexIndex = 0; VertexIndex < NumSoftVertices; ++VertexIndex)
			{
				const int32 DNAVertexIndex = DNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][VertexBufferIndex];

				if (DNAVertexIndex >= 0)
				{
					const FSoftSkinVertex& Vertex = Section.SoftVertices[VertexIndex];

					// --- Position check ---
					const FVector ExpectedPosition = DNAReader->GetVertexPosition(DNAMeshIndex, DNAVertexIndex);
					UTEST_EQUAL_TOLERANCE(
						FString::Printf(TEXT("LOD %d mesh %d vertex %d position"), LODIndex, DNAMeshIndex, DNAVertexIndex),
						FVector{ Vertex.Position },
						ExpectedPosition,
						UE_KINDA_SMALL_NUMBER);

					// --- Normal check ---
					if (DNAPositionToNormalIndex.IsValidIndex(DNAVertexIndex) && DNAPositionToNormalIndex[DNAVertexIndex] != INDEX_NONE)
					{
						const int32 NormalIndex = DNAPositionToNormalIndex[DNAVertexIndex];
						const FVector ExpectedNormal = DNAReader->GetVertexNormal(DNAMeshIndex, NormalIndex);
						UTEST_EQUAL_TOLERANCE(
							FString::Printf(TEXT("LOD %d mesh %d vertex %d normal"), LODIndex, DNAMeshIndex, DNAVertexIndex),
							FVector{ Vertex.TangentZ },
							ExpectedNormal,
							UE_KINDA_SMALL_NUMBER);
					}
				}

				++VertexBufferIndex;
			}

			++SectionIndex;
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
