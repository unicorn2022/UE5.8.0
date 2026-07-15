// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "MetaHumanTypes.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanDefaultEditorPipelineBase.h"
#include "MetaHumanCharacterPaletteProjectSettings.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanCollection.h"
#include "MetaHumanInstance.h"
#include "MetaHumanDefaultPipelineBase.h"
#include "MetaHumanCommonDataUtils.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "DNAUtils.h"
#include "UObject/GCObjectScopeGuard.h"
#include "ImageCore.h"
#include "DNA.h"

#if WITH_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FMetaHumanDefaultEditorPipelineBaseTest, TEXT("MetaHumanCreator.DefaultEditorPipelineBase"), EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

	void ValidateCharacterPartOutput(const FMetaHumanCharacterPartOutput& InCharacterPartOutput, TNotNull<const UMetaHumanDefaultEditorPipelineBase*> InPipeline)
	{
		USkeletalMesh* FaceMesh = InCharacterPartOutput.GeneratedAssets.FaceMesh;
		USkeletalMesh* BodyMesh = InCharacterPartOutput.GeneratedAssets.BodyMesh;

		TestNotNull(TEXT("Face Mesh"), FaceMesh);
		TestNotNull(TEXT("Body Mesh"), InCharacterPartOutput.GeneratedAssets.BodyMesh.Get());

		if (!InPipeline->FaceSkeleton.IsNull())
		{
			TestEqual(TEXT("Face Skeleton"), FaceMesh->GetSkeleton(), InPipeline->FaceSkeleton.LoadSynchronous());
		}
		if (!InPipeline->BodySkeleton.IsNull())
		{
			TestEqual(TEXT("Body Skeleton"), BodyMesh->GetSkeleton(), InPipeline->BodySkeleton.LoadSynchronous());
		}

		ValidateMaterials(FaceMesh->GetMaterials());
		ValidateMaterials(BodyMesh->GetMaterials());
	}

	void ValidateMaterials(const TArray<FSkeletalMaterial>& InMaterials)
	{
		TestFalse(TEXT("Skeletal Materials is not Empty"), InMaterials.IsEmpty());
		for (const FSkeletalMaterial& SkeletalMaterial : InMaterials)
		{
			TestNotNull(FString::Printf(TEXT("Material Interface for slot %s is not null"), *SkeletalMaterial.MaterialSlotName.ToString()), SkeletalMaterial.MaterialInterface.Get());
		}
	}

	TStrongObjectPtr<UMetaHumanCharacter> Character;
	TObjectPtr<UObject> GeneratedOuter;

END_DEFINE_SPEC(FMetaHumanDefaultEditorPipelineBaseTest)

void FMetaHumanDefaultEditorPipelineBaseTest::Define()
{
	BeforeEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("MetaHumanCharacterSubsystem"), MetaHumanCharacterSubsystem);

			if (!Character)
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				TestFalse(TEXT("Character is not valid"), Character->IsCharacterValid());
				TestTrue(TEXT("Initial FaceStateData is empty"), Character->GetFaceStateData().GetSize() == 0);

				MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character.Get());
				TestTrue(TEXT("Add Character to Editor Subsystem"), MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character.Get()));

				UDNA* FaceArchetypeDNA = ReadDNAAssetFromFile(FMetaHumanCommonDataUtils::GetFaceDNAFilesystemPath(), GetTransientPackage());
				TSharedPtr<IDNAReader> ArchetypeDNAReader = FaceArchetypeDNA->GetDNAReader();
				FImportFromDNAParams ImportDNAParams
				{
					.bImportWholeRig = true,
					.AlignmentOptions = EAlignmentOptions::None
				};
				MetaHumanCharacterSubsystem->ImportFromFaceDna(Character.Get(), ArchetypeDNAReader.ToSharedRef(), ImportDNAParams);

				FImage BlankImage;
				BlankImage.Init(2048, 2048, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				for (EFaceTextureType TextureType : TEnumRange<EFaceTextureType>())
				{
					Character->StoreSynthesizedFaceTexture(TextureType, BlankImage);
				}
				for (EBodyTextureType TextureType : TEnumRange<EBodyTextureType>())
				{
					Character->StoreHighResBodyTexture(TextureType, BlankImage);
				}
				Character->SetHasHighResolutionTextures(true);
			}
			TestNotNull(TEXT("Character is not null"), Character.Get());
			if (!MetaHumanCharacterSubsystem->IsObjectAddedForEditing(Character.Get()))
			{
				TestTrue(TEXT("Add Character to Editor Subsystem"), MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character.Get()));
			}
			FText UnusedMessage;
			TestTrue(TEXT("Character can be built"), MetaHumanCharacterSubsystem->CanBuildMetaHuman(Character.Get(), UnusedMessage));

			if (!GeneratedOuter)
			{
				// Prepare an Outer package for generated assets
				GeneratedOuter = CreatePackage(TEXT("/Engine/Transient/MH_GeneratedAssets_Test"));
			}
			TestNotNull(TEXT("Generated Outer Package"), GeneratedOuter.Get());
		});

	AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("MetaHumanCharacterSubsystem"), MetaHumanCharacterSubsystem);

			if (MetaHumanCharacterSubsystem->IsObjectAddedForEditing(Character.Get()))
			{
				MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character.Get());
			}
		});

	It("TestBuildCollection", [this]()
		{
			// Ensure test data are setup and created
			TestNotNull(TEXT("Character is not null"), Character.Get());
			TestTrue(TEXT("Character is valid"), Character->IsCharacterValid());
			TestNotNull(TEXT("Generated Outer Package"), GeneratedOuter.Get());

			const EMetaHumanQualityLevel QualityLevel = EMetaHumanQualityLevel::Cinematic;

			const UMetaHumanCharacterPaletteProjectSettings* Settings = GetDefault<UMetaHumanCharacterPaletteProjectSettings>();
			TestNotNull(TEXT("MetaHuman Character Editor Settings"), Settings);
			
			const TSoftClassPtr<UMetaHumanCollectionPipeline>* FoundPipeline = Settings->DefaultCharacterLegacyPipelines.Find(QualityLevel);
			TestNotNull(TEXT("Default MetaHuman pipeline class"), FoundPipeline);

			TObjectPtr<UMetaHumanCollectionPipeline> CollectionPipeline = NewObject<UMetaHumanCollectionPipeline>(GeneratedOuter, FoundPipeline->LoadSynchronous());
			TestNotNull(TEXT("MetaHuman collection pipeline"), CollectionPipeline.Get());
			TestTrue(TEXT("Collection pipeline is a subclass of the default base pipeline"), CollectionPipeline->IsA(UMetaHumanDefaultPipelineBase::StaticClass()));

			UMetaHumanCollection* Collection = DuplicateObject<UMetaHumanCollection>(Character->GetInternalCollection(), GeneratedOuter);
			Collection->SetPipeline(CollectionPipeline);

			// Guard transient objects from GC since texture baking triggers GC
			TGCObjectsScopeGuard<UObject> GCGuard({ Collection, GeneratedOuter });

			const TArray<FMetaHumanPinnedSlotSelection> SortedPinnedSlotSelections = Collection->GetDefaultInstance()->ToPinnedSlotSelections(EMetaHumanUnusedSlotBehavior::PinnedToEmpty);
			const UMetaHumanCollectionEditorPipeline::FBuildCollectionParams Params
			{
				.Collection = Collection,
				.OuterForGeneratedAssets = GeneratedOuter,
				.SortedPinnedSlotSelections = SortedPinnedSlotSelections
			};

			const UMetaHumanCollectionEditorPipeline::FOnBuildComplete OnBuildCollectionComplete = UMetaHumanDefaultEditorPipelineBase::FOnBuildComplete::CreateLambda(
				[this, CollectionPipeline](EMetaHumanBuildStatus BuildStatus, TSharedPtr<FMetaHumanCollectionBuiltData> BuiltData)
				{
					TestEqual(TEXT("Character built successful"), BuildStatus, EMetaHumanBuildStatus::Succeeded);

					bool bCharacterPartOutputFound = false;
					for (const FMetaHumanPipelineBuiltDataCollectionPair& ItemBuildDataPair : BuiltData->PaletteBuiltData.ItemBuiltData.View().SortedElements)
					{
						if (const FMetaHumanCharacterPartOutput* CharacterPartOutput = ItemBuildDataPair.Value.BuildOutput.GetPtr<FMetaHumanCharacterPartOutput>())
						{
							bCharacterPartOutputFound = true;
							ValidateCharacterPartOutput(*CharacterPartOutput, CastChecked<UMetaHumanDefaultEditorPipelineBase>(CollectionPipeline->GetEditorPipeline()));
						}

					}

					TestTrue(TEXT("Build output contains Character Part"), bCharacterPartOutputFound);
				});

			CollectionPipeline->GetEditorPipeline()->BuildCollection(Params, OnBuildCollectionComplete);
		});
}


#endif