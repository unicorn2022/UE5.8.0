// Copyright Epic Games, Inc. All Rights Reserved.

#include "Item/MetaHumanCrowdCharacterEditorPipeline.h"

#include "ChaosOutfitAsset/BodyUserData.h"
#include "DNAAssetUserData.h"
#include "DNA.h"
#include "Item/MetaHumanCrowdCharacterPipeline.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterGeneratedAssets.h"
#include "MetaHumanCrowdEditorLog.h"
#include "MetaHumanWardrobeItem.h"

#include "Engine/SkeletalMesh.h"
#include "Logging/StructuredLog.h"

UMetaHumanCrowdCharacterEditorPipeline::UMetaHumanCrowdCharacterEditorPipeline()
{
	Specification = CreateDefaultSubobject<UMetaHumanCharacterEditorPipelineSpecification>("Specification");
	Specification->BuildInputStruct = FMetaHumanCrowdCharacterBuildInput::StaticStruct();
}

namespace UE::MetaHuman::CrowdCharacterEditorPipelinePrivate
{

/**
 * Returns the mesh to use for the build output given the requirement level and an optional
 * pre-built mesh property. If a pre-built mesh is provided and the requirement is Modifiable,
 * the mesh is duplicated. If the requirement is ReadOnly, the mesh is passed through as-is.
 * Returns nullptr if no pre-built mesh is available (caller should use generated assets).
 */
static USkeletalMesh* ResolveMesh(
	USkeletalMesh* InPreBuiltMesh,
	EMetaHumanCrowdCharacterMeshRequirement InRequirement,
	UObject* InOuterForDuplicate)
{
	if (!InPreBuiltMesh)
	{
		return nullptr;
	}

	if (InRequirement == EMetaHumanCrowdCharacterMeshRequirement::Modifiable)
	{
		USkeletalMesh* DuplicatedMesh = DuplicateObject<USkeletalMesh>(InPreBuiltMesh, InOuterForDuplicate);

		// The DDC key should be preserved on duplication, as the source data is identical
		{
			const FString SourceDDCKey = InPreBuiltMesh->GetDerivedDataKey();
			const FString DuplicatedDDCKey = DuplicatedMesh->GetDerivedDataKey();
			ensure(SourceDDCKey == DuplicatedDDCKey);
		}

		// The DNA needs to be kept in sync with the mesh, so if the mesh needs to be modifiable, 
		// we need a modifiable copy of the DNA as well.
		if (UDNAAssetUserData* AssetUserData = DuplicatedMesh->GetAssetUserData<UDNAAssetUserData>())
		{
			if (AssetUserData->DNAAsset)
			{
				AssetUserData->DNAAsset = DuplicateObject(AssetUserData->DNAAsset, InOuterForDuplicate);
				AssetUserData->DNAAsset->SetFlags(RF_Public);
			}
		}

		return DuplicatedMesh;
	}

	// ReadOnly -- pass through without duplication
	return InPreBuiltMesh;
}

} // namespace UE::MetaHuman::CrowdCharacterEditorPipelinePrivate

UE::Tasks::TTask<FMetaHumanPaletteBuiltData> UMetaHumanCrowdCharacterEditorPipeline::BuildItem(const FBuildItemParams& Params) const
{
	using namespace UE::MetaHuman::CrowdCharacterEditorPipelinePrivate;
	using EMeshReq = EMetaHumanCrowdCharacterMeshRequirement;

	UObject* LoadedAsset = Params.WardrobeItem->PrincipalAsset.LoadSynchronous();
	if (!LoadedAsset)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd character pipeline failed to load principal asset {Asset} during build", Params.WardrobeItem->PrincipalAsset.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(LoadedAsset);
	if (!Character)
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Principal asset {Asset} is not a UMetaHumanCharacter and is not compatible with the Crowd Character Pipeline", Params.WardrobeItem->PrincipalAsset.ToString());

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	if (!Params.BuildInput.GetPtr<FMetaHumanCrowdCharacterBuildInput>())
	{
		UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd character pipeline didn't receive required build input struct");

		return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
	}

	const FMetaHumanCrowdCharacterBuildInput& BuildInput = Params.BuildInput.Get<FMetaHumanCrowdCharacterBuildInput>();

	const bool bNeedFace = BuildInput.FaceMeshRequirement != EMeshReq::NotNeeded;
	const bool bNeedBody = BuildInput.BodyMeshRequirement != EMeshReq::NotNeeded;
	const bool bNeedMerged = BuildInput.MergedHeadAndBodyMeshRequirement != EMeshReq::NotNeeded;

	// Determine which assets need to be generated (requested but not provided via pre-built
	// pipeline properties).
	const bool bNeedGenerateFace = bNeedFace && !FaceMesh;
	const bool bNeedGenerateBody = bNeedBody && !BodyMesh;
	const bool bNeedGenerateMerged = bNeedMerged && !MergedHeadAndBodyMesh;
	const bool bNeedGenerateMeasurements = BuildInput.bGenerateBodyMeasurements && !MergedHeadAndBodyMesh;

	FMetaHumanCharacterGeneratedAssets GeneratedAssets;

	if (bNeedGenerateFace || bNeedGenerateBody || bNeedGenerateMerged || bNeedGenerateMeasurements)
	{
		const FMetaHumanCharacterGeneratedAssetOptions Options
		{
			.bGenerateFaceMesh = bNeedGenerateFace,
			.bGenerateBodyMesh = bNeedGenerateBody,
			.bGenerateMergedHeadAndBodyMesh = bNeedGenerateMerged,
			.bGenerateBodyMeasurements = bNeedGenerateMeasurements
		};

		UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterEditorSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
		if (!MetaHumanCharacterEditorSubsystem->TryGenerateCharacterAssets(Character, Params.OuterForGeneratedObjects, Options, GeneratedAssets))
		{
			UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Crowd character pipeline failed to generate assets from Character {Character}", Params.WardrobeItem->PrincipalAsset.ToString());

			return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
		}
	}

	// Build the output, preferring pre-built properties over generated assets.
	FMetaHumanPaletteBuiltData BuiltDataResult;
	FMetaHumanPipelineBuiltData& CharacterBuiltData = BuiltDataResult.ItemBuiltData.Edit().Add(Params.ItemPath);
	CharacterBuiltData.DefaultUnpackSubfolder = FString::Format(TEXT("Characters/{0}"), { LoadedAsset->GetName() });

	FMetaHumanCrowdCharacterBuildOutput& CharacterBuildOutput = CharacterBuiltData.BuildOutput.InitializeAs<FMetaHumanCrowdCharacterBuildOutput>();
	CharacterBuildOutput.CompatibleBody = CompatibleBody;

	if (bNeedFace)
	{
		USkeletalMesh* ResolvedFace = ResolveMesh(FaceMesh, BuildInput.FaceMeshRequirement, Params.OuterForGeneratedObjects);
		CharacterBuildOutput.FaceMesh = ResolvedFace ? ResolvedFace : GeneratedAssets.FaceMesh.Get();
		if (CharacterBuildOutput.FaceMesh)
		{
			CharacterBuiltData.Metadata.Emplace(CharacterBuildOutput.FaceMesh, CharacterBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_Face"), { LoadedAsset->GetName() }));
		}
	}

	if (bNeedBody)
	{
		USkeletalMesh* ResolvedBody = ResolveMesh(BodyMesh, BuildInput.BodyMeshRequirement, Params.OuterForGeneratedObjects);
		CharacterBuildOutput.BodyMesh = ResolvedBody ? ResolvedBody : GeneratedAssets.BodyMesh.Get();
		if (CharacterBuildOutput.BodyMesh)
		{
			CharacterBuiltData.Metadata.Emplace(CharacterBuildOutput.BodyMesh, CharacterBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_Body"), { LoadedAsset->GetName() }));
		}
	}

	if (bNeedMerged)
	{
		USkeletalMesh* ResolvedMerged = ResolveMesh(MergedHeadAndBodyMesh, BuildInput.MergedHeadAndBodyMeshRequirement, Params.OuterForGeneratedObjects);
		CharacterBuildOutput.MergedHeadAndBodyMesh = ResolvedMerged ? ResolvedMerged : GeneratedAssets.MergedHeadAndBodyMesh.Get();
		if (CharacterBuildOutput.MergedHeadAndBodyMesh)
		{
			CharacterBuiltData.Metadata.Emplace(CharacterBuildOutput.MergedHeadAndBodyMesh, CharacterBuiltData.DefaultUnpackSubfolder, FString::Format(TEXT("SKM_{0}_MergedHeadAndBody"), { LoadedAsset->GetName() }));
		}
	}

	if (BuildInput.bGenerateBodyMeasurements)
	{
		if (MergedHeadAndBodyMesh)
		{
			// When using a pre-built merged mesh, extract body measurements from the
			// UChaosOutfitAssetBodyUserData attached to it by the character editor.
			if (const UChaosOutfitAssetBodyUserData* UserData = MergedHeadAndBodyMesh->GetAssetUserData<UChaosOutfitAssetBodyUserData>())
			{
				CharacterBuildOutput.BodyMeasurements = UserData->Measurements;
			}
			else
			{
				UE_LOGFMT(LogMetaHumanCrowdEditor, Error, "Merged head and body mesh {Mesh} provided to Crowd Character pipeline doesn't contain body measurements", MergedHeadAndBodyMesh->GetPathName());

				return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>();
			}
		}
		else
		{
			CharacterBuildOutput.BodyMeasurements = GeneratedAssets.BodyMeasurements;
		}
	}

	return UE::Tasks::MakeCompletedTask<FMetaHumanPaletteBuiltData>(MoveTemp(BuiltDataResult));
}

TNotNull<const UMetaHumanCharacterEditorPipelineSpecification*> UMetaHumanCrowdCharacterEditorPipeline::GetSpecification() const
{
	return Specification;
}
