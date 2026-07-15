// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterGeneratedAssets.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanTypes.h"
#include "Animation/AnimInstance.h"
#include "Engine/Texture2D.h"
#include "Algo/Contains.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageCore.h"
#include "UObject/GCObjectScopeGuard.h"
#include "DNA.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanCommonDataUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectArray.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCollection.h"
#include "MetaHumanInvisibleDrivingActor.h"
#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"


#if WITH_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FMetaHumanCharacterEditorSubsystemTest, "MetaHumanCreator.EditorSubsystem", EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

	void ValidateGeneratedAssets(const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets)
	{
		ValidateFaceSKM(InGeneratedAssets);
		ValidateBodySKM(InGeneratedAssets);
		ValidateFaceTextures(InGeneratedAssets);
		ValidateBodyTextures(InGeneratedAssets);
	}

	void ValidateFaceSKM(const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets)
	{
		USkeletalMesh* FaceMesh = InGeneratedAssets.FaceMesh;
		TestNotNull(TEXT("Face Mesh"), FaceMesh);

		if (FaceMesh)
		{
			TestNotNull(TEXT("Face SKM Skeleton"), FaceMesh->GetSkeleton());
			TestNotNull(TEXT("Face SKM Post Process ABP"), FaceMesh->GetPostProcessAnimBlueprint().Get());
			TestNotNull(TEXT("Face SKM Physics Asset"), FaceMesh->GetPhysicsAsset());

			UDNA* DNAUserData = USkelMeshDNAUtils::GetMeshDNAAsset(FaceMesh);
			TestNotNull(TEXT("Face SKM DNA"), DNAUserData);
			if (DNAUserData)
			{
				TestTrue(TEXT("Face SKM DNA Behavior Reader"), DNAUserData->GetDNAReader().IsValid());
			}

			TestTrue(TEXT("Face SKM in metadata"), Algo::ContainsBy(InGeneratedAssets.Metadata, FaceMesh, &FMetaHumanGeneratedAssetMetadata::Object));
		}
	}

	void ValidateBodySKM(const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets)
	{
		TestNotNull(TEXT("Body Mesh"), InGeneratedAssets.BodyMesh.Get());
		TestNotNull(TEXT("Body Physics Asset"), InGeneratedAssets.PhysicsAsset.Get());

		UDNA* DNAUserData = USkelMeshDNAUtils::GetMeshDNAAsset(InGeneratedAssets.BodyMesh);
		TestNotNull(TEXT("Body SKM DNA"), DNAUserData);
		if (DNAUserData)
		{
			TestTrue(TEXT("Body SKM DNA Behavior Reader"), DNAUserData->GetDNAReader().IsValid());
		}

		TestTrue(TEXT("Body SKM in metadata"), Algo::ContainsBy(InGeneratedAssets.Metadata, InGeneratedAssets.BodyMesh, &FMetaHumanGeneratedAssetMetadata::Object));
	}

	void ValidateFaceTextures(const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets)
	{
		for (EFaceTextureType TextureType : TEnumRange<EFaceTextureType>())
		{
			TestTrue(TEXT("Contains Face Texture Type"), InGeneratedAssets.SynthesizedFaceTextures.Contains(TextureType));
			if (const TObjectPtr<UTexture2D>* FaceTexturePtr = InGeneratedAssets.SynthesizedFaceTextures.Find(TextureType))
			{
				const UTexture2D* FaceTexture = *FaceTexturePtr;
				TestNotNull(TEXT("Face Texture is not null"), FaceTexture);
				TestTrue(TEXT("Face Texture Source"), FaceTexture && FaceTexture->Source.IsValid());
				TestTrue(TEXT("Face Texture in metadata"), Algo::ContainsBy(InGeneratedAssets.Metadata, FaceTexture, &FMetaHumanGeneratedAssetMetadata::Object));
			}
		}
	}

	void ValidateBodyTextures(const FMetaHumanCharacterGeneratedAssets& InGeneratedAssets)
	{
		for (EBodyTextureType TextureType : TEnumRange<EBodyTextureType>())
		{
			TestTrue(TEXT("Contains Body Texture Type"), InGeneratedAssets.BodyTextures.Contains(TextureType));
			if (const TObjectPtr<UTexture2D>* BodyTexturePtr = InGeneratedAssets.BodyTextures.Find(TextureType))
			{
				const UTexture2D* BodyTexture = *BodyTexturePtr;
				TestNotNull(TEXT("Body Texture is not null"), BodyTexture);
				TestTrue(TEXT("Body Texture Source"), BodyTexture && BodyTexture->Source.IsValid());
				// TODO: Body textures are not currently tracked in GeneratedAssets.Metadata
				//TestTrue(TEXT("Body Texture in metadata"), Algo::ContainsBy(InGeneratedAssets.Metadata, BodyTexture, &FMetaHumanGeneratedAssetMetadata::Object));
			}
		}
	}

	void ValidateFaceSKMMaterials(TNotNull<USkeletalMesh*> InFaceMesh)
	{
		const TArray<FName> SkinSlotNames =
		{
			FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot::LOD0),
			FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot::LOD1),
			FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot::LOD2),
			FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot::LOD3),
			FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot::LOD4),
			FMetaHumanCharacterSkinMaterials::GetSkinMaterialSlotName(EMetaHumanCharacterSkinMaterialSlot::LOD5to7),
			FMetaHumanCharacterSkinMaterials::EyeLeftSlotName,
			FMetaHumanCharacterSkinMaterials::EyeRightSlotName,
			FMetaHumanCharacterSkinMaterials::SalivaSlotName,
			FMetaHumanCharacterSkinMaterials::EyeShellSlotName,
			FMetaHumanCharacterSkinMaterials::EyeEdgeSlotName,
			FMetaHumanCharacterSkinMaterials::TeethSlotName,
			FMetaHumanCharacterSkinMaterials::EyelashesSlotName,
			FMetaHumanCharacterSkinMaterials::EyelashesHiLodSlotName,
		};

		for (const FName& SlotName : SkinSlotNames)
		{
			FSkeletalMaterial* MaterialSlot = Algo::FindBy(InFaceMesh->GetMaterials(), SlotName, &FSkeletalMaterial::MaterialSlotName);
			TestNotNull(TEXT("Face Material Slot"), MaterialSlot);
			if (MaterialSlot)
			{
				TestNotNull(TEXT("Face Material Interface for Slot"), MaterialSlot->MaterialInterface.Get());
			}
		}
	}

	void ValidateFaceSKMMaterials(TNotNull<USkeletalMesh*> InFaceMesh, const FMetaHumanCharacterFaceMaterialSet& InExpectedMaterialSet)
	{
		auto ValidateSkinMaterial = [this, InFaceMesh](FName InSlotName, const UMaterialInterface* InExpectedMaterialInterface)
			{
				FSkeletalMaterial* MaterialSlot = Algo::FindBy(InFaceMesh->GetMaterials(), InSlotName, &FSkeletalMaterial::MaterialSlotName);
				TestNotNull(TEXT("Face Material Slot"), MaterialSlot);

				if (MaterialSlot)
				{
					TestNotNull(TEXT("Face Material Interface for Slot"), MaterialSlot->MaterialInterface.Get());
					TestEqual<const UMaterialInterface*>(TEXT("Face Material Interface for Slot"), MaterialSlot->MaterialInterface.Get(), InExpectedMaterialInterface);
				}
			};

		FMetaHumanCharacterSkinMaterials::ForEachFaceMaterialSlot(InExpectedMaterialSet, ValidateSkinMaterial);
	}

	/** Compare two FSharedBuffers for byte equality */
	bool AreBuffersEqual(const FSharedBuffer& InBufferA, const FSharedBuffer& InBufferB)
	{
		if (InBufferA.GetSize() != InBufferB.GetSize())
		{
			return false;
		}
		if (InBufferA.GetSize() == 0)
		{
			return true;
		}
		return FMemory::Memcmp(InBufferA.GetData(), InBufferB.GetData(), InBufferA.GetSize()) == 0;
	}

	/** Check if vertex arrays differ beyond tolerance */
	bool DoVerticesDiffer(const TArray<FVector3f>& InVerticesA, const TArray<FVector3f>& InVerticesB, float InTolerance = 0.001f)
	{
		if (InVerticesA.Num() != InVerticesB.Num())
		{
			return true;
		}
		for (int32 Index = 0; Index < InVerticesA.Num(); ++Index)
		{
			if (!InVerticesA[Index].Equals(InVerticesB[Index], InTolerance))
			{
				return true;
			}
		}
		return false;
	}

	/** Snapshot LOD0 vertex positions from a skeletal mesh */
	TArray<FVector3f> SnapshotLOD0Positions(const USkeletalMesh* InMesh)
	{
		TArray<FVector3f> Result;
		const FSkeletalMeshModel* Model = InMesh->GetImportedModel();
		if (!Model || Model->LODModels.Num() == 0)
		{
			return Result;
		}
		TArray<FSoftSkinVertex> Vertices;
		Model->LODModels[0].GetVertices(Vertices);
		Result.Reserve(Vertices.Num());
		for (const FSoftSkinVertex& Vertex : Vertices)
		{
			Result.Add(Vertex.Position);
		}
		return Result;
	}

	/** Common setup: create a character, initialize it, and add it for editing */
	bool SetupCharacterForEditing()
	{
		Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
		UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		TestNotNull(TEXT("Subsystem"), Subsystem);
		if (!Subsystem)
		{
			return false;
		}
		
		Subsystem->InitializeMetaHumanCharacter(Character.Get());
		
		return TestTrue(TEXT("Add Character for editing"), Subsystem->TryAddObjectToEdit(Character.Get()));
	}

	/** Common setup: create a character with blank textures for build pipeline tests */
	bool SetupCharacterWithTextures()
	{
		if (!SetupCharacterForEditing())
		{
			return false;
		}
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
		return true;
	}

	/** RAII world for actor-based tests */
	struct FScopedTestWorld
	{
		FScopedTestWorld()
		{
			const FName UniqueWorldName = MakeUniqueObjectName(
				GetTransientPackage(), UWorld::StaticClass(), TEXT("MH_SpecTestWorld"));
			World = NewObject<UWorld>(GetTransientPackage(), UniqueWorldName);
			World->AddToRoot();
			World->WorldType = EWorldType::EditorPreview;

			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(World->WorldType);
			WorldContext.SetCurrentWorld(World);

			World->InitializeNewWorld(UWorld::InitializationValues()
				.RequiresHitProxies(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.AllowAudioPlayback(false)
				.SetTransactional(false)
				.CreatePhysicsScene(true));

			FURL URL;
			World->InitializeActorsForPlay(URL);
		}

		~FScopedTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
			const bool bInformEngineOfWorld = false;
			World->DestroyWorld(bInformEngineOfWorld);
		}

		UWorld* World = nullptr;
	};

	TStrongObjectPtr<UMetaHumanCharacter> Character;
	TStrongObjectPtr<UObject> GeneratedOuter;
	TUniquePtr<FScopedTestWorld> TestWorld;

END_DEFINE_SPEC(FMetaHumanCharacterEditorSubsystemTest)


void FMetaHumanCharacterEditorSubsystemTest::Define()
{
	// =========================================================================
	// Asset Generation Tests
	// =========================================================================
	Describe("AssetGeneration", [this]()
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

				// Create test textures so that TryGenerateCharacterAssets() generates Source for them
				TestTrue(TEXT("Add Character to Editor Subsystem"), MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character.Get()));
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
				MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character.Get());
			}
			TestNotNull(TEXT("Character is not null"), Character.Get());
			TestTrue(TEXT("Character is valid"), Character->IsCharacterValid());

			if (!GeneratedOuter)
			{
				GeneratedOuter.Reset(CreatePackage(TEXT("/Engine/Transient/MH_GeneratedAssets_Test")));
			}
			TestNotNull(TEXT("Generated Outer Package"), GeneratedOuter.Get());
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("MetaHumanCharacterSubsystem"), MetaHumanCharacterSubsystem);

			if (Character.IsValid() && MetaHumanCharacterSubsystem->IsObjectAddedForEditing(Character.Get()))
			{
				MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character.Get());
			}
		});

		It("should create editor data with valid meshes and materials", [this]()
		{
			// Ensure test data are setup and created
			TestNotNull(TEXT("Character is not null"), Character.Get());
			TestTrue(TEXT("Character is valid"), Character->IsCharacterValid());

			// TStrongObjectPtr prevents GC — no FGCObjectScopeGuard needed

			UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("MetaHumanCharacterSubsystem"), MetaHumanCharacterSubsystem);
			TestTrue(TEXT("Add Character to Editor Subsystem"), MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character.Get()));

			const TSharedRef<FMetaHumanCharacterEditorData>* CharacterEditorDataPtr = MetaHumanCharacterSubsystem->GetMetaHumanCharacterEditorData(Character.Get());
			TestNotNull(TEXT("Character Editor Data is not null"), CharacterEditorDataPtr);

			// Test the editor data contains SKMs and any materials
			if (CharacterEditorDataPtr)
			{
				const FMetaHumanCharacterEditorData& CharacterEditorData = CharacterEditorDataPtr->Get();
				TestNotNull(TEXT("Character Editor Data contains Face Mesh"), CharacterEditorData.FaceMesh.Get());
				TestNotNull(TEXT("Character Editor Data contains Body Mesh"), CharacterEditorData.BodyMesh.Get());

				TestFalse(TEXT("Character Editor Data has Face materials assigned"), CharacterEditorData.HeadMaterials.Skin.IsEmpty());
				TestNotNull(TEXT("Character Editor Data has EyeLeft material assigned"), CharacterEditorData.HeadMaterials.EyeLeft.Get());
				TestNotNull(TEXT("Character Editor Data has EyeRight material assigned"), CharacterEditorData.HeadMaterials.EyeRight.Get());
				TestNotNull(TEXT("Character Editor Data has EyeShell material assigned"), CharacterEditorData.HeadMaterials.EyeShell.Get());
				TestNotNull(TEXT("Character Editor Data has LacrimalFluid material assigned"), CharacterEditorData.HeadMaterials.LacrimalFluid.Get());
				TestNotNull(TEXT("Character Editor Data has Teeth material assigned"), CharacterEditorData.HeadMaterials.Teeth.Get());
				TestNotNull(TEXT("Character Editor Data has Eyelashes material assigned"), CharacterEditorData.HeadMaterials.Eyelashes.Get());
				TestNotNull(TEXT("Character Editor Data has EyelashesHiLods material assigned"), CharacterEditorData.HeadMaterials.EyelashesHiLods.Get());

				ValidateFaceSKMMaterials(CharacterEditorData.FaceMesh);
				ValidateFaceSKMMaterials(CharacterEditorData.FaceMesh, CharacterEditorData.HeadMaterials);

				if (FSkeletalMaterial* MaterialSlot = Algo::FindBy(
					CharacterEditorData.BodyMesh->GetMaterials(), FMetaHumanCharacterSkinMaterials::BodySlotName, &FSkeletalMaterial::MaterialSlotName))
				{
					TestEqual<const UMaterialInterface*>(TEXT("Body Material Interface for Slot"),
						MaterialSlot->MaterialInterface, Cast<const UMaterialInterface>(CharacterEditorData.BodyMaterial));
				}
			}

		});

	It("should generate character assets with valid SKMs and textures", [this]()
		{
			// Ensure test data are setup and created
			TestNotNull(TEXT("Character is not null"), Character.Get());
			TestTrue(TEXT("Character is valid"), Character->IsCharacterValid());
			TestNotNull(TEXT("Generated Outer Package"), GeneratedOuter.Get());

			// TStrongObjectPtr prevents GC — no TGCObjectsScopeGuard needed

			UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("MetaHumanCharacterSubsystem"), MetaHumanCharacterSubsystem);
			TestTrue(TEXT("Add Character to Editor Subsystem"), MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character.Get()));

			FMetaHumanCharacterGeneratedAssets GeneratedAssets;
			TestTrue(TEXT("Generate Character Assets"), MetaHumanCharacterSubsystem->TryGenerateCharacterAssets(Character.Get(), GeneratedOuter.Get(), GeneratedAssets));
			ValidateGeneratedAssets(GeneratedAssets);
		});
	}); // end AssetGeneration

	// =========================================================================
	// Lifecycle Tests
	// =========================================================================
	Describe("Lifecycle", [this]()
	{
		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		Describe("InitializeMetaHumanCharacter", [this]()
		{
			It("should populate face and body state data", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				TestTrue(TEXT("Face state empty before init"), Character->GetFaceStateData().GetSize() == 0);
				TestTrue(TEXT("Body state empty before init"), Character->GetBodyStateData().GetSize() == 0);

				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());

				TestTrue(TEXT("Face state populated"), Character->GetFaceStateData().GetSize() > 0);
				TestTrue(TEXT("Body state populated"), Character->GetBodyStateData().GetSize() > 0);
			});
		});

		Describe("TryAddObjectToEdit", [this]()
		{
			It("should succeed for a valid initialized character", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());

				TestTrue(TEXT("Add succeeds"), Subsystem->TryAddObjectToEdit(Character.Get()));
				TestTrue(TEXT("Is added"), Subsystem->IsObjectAddedForEditing(Character.Get()));
			});

			It("should create editor data with valid meshes", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				TestTrue(TEXT("Add Character to Editor Subsystem"), Subsystem->TryAddObjectToEdit(Character.Get()));

				const TSharedRef<FMetaHumanCharacterEditorData>* CharacterEditorDataPtr = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				TestNotNull(TEXT("Editor data"), CharacterEditorDataPtr);
				if (CharacterEditorDataPtr)
				{
					TestNotNull(TEXT("Face mesh"), CharacterEditorDataPtr->Get().FaceMesh.Get());
					TestNotNull(TEXT("Body mesh"), CharacterEditorDataPtr->Get().BodyMesh.Get());
				}
			});

			It("should reject adding the same character twice", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				TestTrue(TEXT("First add succeeds"), Subsystem->TryAddObjectToEdit(Character.Get()));

				AddExpectedError(TEXT("already added for editing"), EAutomationExpectedErrorFlags::Contains);
				TestFalse(TEXT("Second add fails"), Subsystem->TryAddObjectToEdit(Character.Get()));
				TestTrue(TEXT("Still added"), Subsystem->IsObjectAddedForEditing(Character.Get()));
			});

			It("should support multiple different characters", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				TStrongObjectPtr<UMetaHumanCharacter> Character2(NewObject<UMetaHumanCharacter>(GetTransientPackage()));

				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				Subsystem->InitializeMetaHumanCharacter(Character2.Get());

				TestTrue(TEXT("Add first"), Subsystem->TryAddObjectToEdit(Character.Get()));
				TestTrue(TEXT("Add second"), Subsystem->TryAddObjectToEdit(Character2.Get()));
				TestTrue(TEXT("First is editing"), Subsystem->IsObjectAddedForEditing(Character.Get()));
				TestTrue(TEXT("Second is editing"), Subsystem->IsObjectAddedForEditing(Character2.Get()));

				Subsystem->RemoveObjectToEdit(Character2.Get());
			});

			It("should give each character its own face and body DNA", [this]()
			{
				// Regression test: the face/body skeletal meshes are duplicated from a shared
				// archetype, and each character must end up with its own UDNA instance.
				// Previously the default editor path only duplicated the mesh -- the
				// UDNAAssetUserData->DNAAsset pointer was copied verbatim, so every character
				// opened from the same archetype shared a single UDNA. Mutations to one
				// character's DNA (RestoreLegacyUEMHCCompatibility, SetDNAReader, etc.) then
				// leaked into every other character.

				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				TStrongObjectPtr<UMetaHumanCharacter> Character2(NewObject<UMetaHumanCharacter>(GetTransientPackage()));

				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				Subsystem->InitializeMetaHumanCharacter(Character2.Get());

				TestTrue(TEXT("Add first"), Subsystem->TryAddObjectToEdit(Character.Get()));
				TestTrue(TEXT("Add second"), Subsystem->TryAddObjectToEdit(Character2.Get()));

				const TSharedRef<FMetaHumanCharacterEditorData>* Data1 = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				const TSharedRef<FMetaHumanCharacterEditorData>* Data2 = Subsystem->GetMetaHumanCharacterEditorData(Character2.Get());
				TestNotNull(TEXT("Character editor data"), Data1);
				TestNotNull(TEXT("Character2 editor data"), Data2);

				if (Data1 && Data2)
				{
					USkeletalMesh* FaceMesh1 = Data1->Get().FaceMesh;
					USkeletalMesh* FaceMesh2 = Data2->Get().FaceMesh;
					USkeletalMesh* BodyMesh1 = Data1->Get().BodyMesh;
					USkeletalMesh* BodyMesh2 = Data2->Get().BodyMesh;

					TestNotNull(TEXT("Face mesh on Character"), FaceMesh1);
					TestNotNull(TEXT("Face mesh on Character2"), FaceMesh2);
					TestNotNull(TEXT("Body mesh on Character"), BodyMesh1);
					TestNotNull(TEXT("Body mesh on Character2"), BodyMesh2);

					// Sanity: meshes themselves are distinct instances per character.
					TestNotEqual<const USkeletalMesh*>(TEXT("Face meshes are distinct instances"), FaceMesh1, FaceMesh2);
					TestNotEqual<const USkeletalMesh*>(TEXT("Body meshes are distinct instances"), BodyMesh1, BodyMesh2);

					if (FaceMesh1 && FaceMesh2)
					{
						const UDNA* FaceDNA1 = USkelMeshDNAUtils::GetMeshDNAAsset(FaceMesh1);
						const UDNA* FaceDNA2 = USkelMeshDNAUtils::GetMeshDNAAsset(FaceMesh2);
						TestNotNull(TEXT("Face DNA on Character"), FaceDNA1);
						TestNotNull(TEXT("Face DNA on Character2"), FaceDNA2);
						TestNotEqual<const UDNA*>(TEXT("Face DNAs are distinct instances per character"), FaceDNA1, FaceDNA2);
					}

					if (BodyMesh1 && BodyMesh2)
					{
						const UDNA* BodyDNA1 = USkelMeshDNAUtils::GetMeshDNAAsset(BodyMesh1);
						const UDNA* BodyDNA2 = USkelMeshDNAUtils::GetMeshDNAAsset(BodyMesh2);
						TestNotNull(TEXT("Body DNA on Character"), BodyDNA1);
						TestNotNull(TEXT("Body DNA on Character2"), BodyDNA2);
						TestNotEqual<const UDNA*>(TEXT("Body DNAs are distinct instances per character"), BodyDNA1, BodyDNA2);
					}
				}

				Subsystem->RemoveObjectToEdit(Character.Get());
				Subsystem->RemoveObjectToEdit(Character2.Get());
			});

			It("should give a character initialized from a preset its own face and body DNA", [this]()
			{
				// Regression test: applying a preset must not leave the destination character
				// sharing a UDNA with a sibling character. InitializeFromPreset mutates the
				// destination character's face/body DNA in place (via SetDNAReader and
				// RestoreLegacyUEMHCCompatibility) -- if the DNA were shared, applying a
				// preset to one character would clobber the other.

				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				UMetaHumanCharacter* PresetCharacter = LoadObject<UMetaHumanCharacter>(nullptr, TEXT("/" UE_PLUGIN_NAME "/Optional/Presets/Ada.Ada"));
				if (!PresetCharacter)
				{
					// The preset lives in an Optional plugin folder that may not be installed in
					// every test environment -- skip rather than fail if it's missing.
					AddInfo(TEXT("Optional preset asset not present -- skipping preset DNA test"));
					return;
				}

				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				TStrongObjectPtr<UMetaHumanCharacter> Character2(NewObject<UMetaHumanCharacter>(GetTransientPackage()));

				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				Subsystem->InitializeMetaHumanCharacter(Character2.Get());

				TestTrue(TEXT("Add first"), Subsystem->TryAddObjectToEdit(Character.Get()));
				TestTrue(TEXT("Add second"), Subsystem->TryAddObjectToEdit(Character2.Get()));

				// Apply the preset to Character only. Character2 stays untouched.
				Subsystem->InitializeFromPreset(Character.Get(), PresetCharacter);

				const TSharedRef<FMetaHumanCharacterEditorData>* Data1 = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				const TSharedRef<FMetaHumanCharacterEditorData>* Data2 = Subsystem->GetMetaHumanCharacterEditorData(Character2.Get());
				TestNotNull(TEXT("Preset character editor data"), Data1);
				TestNotNull(TEXT("Sibling character editor data"), Data2);

				if (Data1 && Data2)
				{
					USkeletalMesh* FaceMesh1 = Data1->Get().FaceMesh;
					USkeletalMesh* FaceMesh2 = Data2->Get().FaceMesh;
					USkeletalMesh* BodyMesh1 = Data1->Get().BodyMesh;
					USkeletalMesh* BodyMesh2 = Data2->Get().BodyMesh;

					if (FaceMesh1 && FaceMesh2)
					{
						const UDNA* FaceDNA1 = USkelMeshDNAUtils::GetMeshDNAAsset(FaceMesh1);
						const UDNA* FaceDNA2 = USkelMeshDNAUtils::GetMeshDNAAsset(FaceMesh2);
						TestNotNull(TEXT("Preset character face DNA"), FaceDNA1);
						TestNotNull(TEXT("Sibling character face DNA"), FaceDNA2);
						TestNotEqual<const UDNA*>(TEXT("Preset face DNA is a distinct instance from the sibling character"), FaceDNA1, FaceDNA2);
					}

					if (BodyMesh1 && BodyMesh2)
					{
						const UDNA* BodyDNA1 = USkelMeshDNAUtils::GetMeshDNAAsset(BodyMesh1);
						const UDNA* BodyDNA2 = USkelMeshDNAUtils::GetMeshDNAAsset(BodyMesh2);
						TestNotNull(TEXT("Preset character body DNA"), BodyDNA1);
						TestNotNull(TEXT("Sibling character body DNA"), BodyDNA2);
						TestNotEqual<const UDNA*>(TEXT("Preset body DNA is a distinct instance from the sibling character"), BodyDNA1, BodyDNA2);
					}
				}

				Subsystem->RemoveObjectToEdit(Character.Get());
				Subsystem->RemoveObjectToEdit(Character2.Get());
			});
		});

		Describe("IsObjectAddedForEditing", [this]()
		{
			It("should return true for added character", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				TestTrue(TEXT("Add succeeds"), Subsystem->TryAddObjectToEdit(Character.Get()));
				TestTrue(TEXT("Is added"), Subsystem->IsObjectAddedForEditing(Character.Get()));
			});

			It("should return false for non-added character", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				TestFalse(TEXT("Not added"), Subsystem->IsObjectAddedForEditing(Character.Get()));
			});

			It("should return false after RemoveObjectToEdit", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				TestTrue(TEXT("Add for edit"), Subsystem->TryAddObjectToEdit(Character.Get()));
				Subsystem->RemoveObjectToEdit(Character.Get());
				TestFalse(TEXT("Not added after remove"), Subsystem->IsObjectAddedForEditing(Character.Get()));
			});
		});

		Describe("RemoveObjectToEdit", [this]()
		{
			It("should clean up editor data", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				TestTrue(TEXT("Add for edit"), Subsystem->TryAddObjectToEdit(Character.Get()));

				TestNotNull(TEXT("Data exists before"), Subsystem->GetMetaHumanCharacterEditorData(Character.Get()));
				Subsystem->RemoveObjectToEdit(Character.Get());
				TestNull(TEXT("Data gone after"), Subsystem->GetMetaHumanCharacterEditorData(Character.Get()));
			});

			It("should be safe for non-added character", [this]()
			{
				Character = TStrongObjectPtr<UMetaHumanCharacter>(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				Subsystem->InitializeMetaHumanCharacter(Character.Get());
				AddExpectedError(TEXT("was not previously opened"), EAutomationExpectedErrorFlags::Contains);
				Subsystem->RemoveObjectToEdit(Character.Get());
			});
		});
	});

	// =========================================================================
	// Face State Tests
	// =========================================================================
	Describe("FaceState", [this]()
	{
		BeforeEach([this]()
		{
			SetupCharacterForEditing();
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		Describe("GetFaceState and CopyFaceState", [this]()
		{
			It("should return valid face state for editing character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TSharedRef<const FMetaHumanCharacterIdentity::FState> State = Subsystem->GetFaceState(Character.Get());
				FMetaHumanRigEvaluatedState EvaluatedState = State->Evaluate();
				TestTrue(TEXT("State has vertices"), EvaluatedState.Vertices.Num() > 0);
			});

			It("should return independent mutable copy", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TSharedRef<FMetaHumanCharacterIdentity::FState> Copy = Subsystem->CopyFaceState(Character.Get());
				FMetaHumanRigEvaluatedState EvaluatedStateBefore = Copy->Evaluate();

				Copy->Randomize(1.0f);
				FMetaHumanRigEvaluatedState SubsystemEval = Subsystem->GetFaceState(Character.Get())->Evaluate();

				TestFalse(TEXT("Copy Randomize did not affect subsystem state"), DoVerticesDiffer(EvaluatedStateBefore.Vertices, SubsystemEval.Vertices));
			});
		});

		Describe("ApplyFaceState", [this]()
		{
			It("should update evaluated vertices and LOD model", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				const TSharedRef<FMetaHumanCharacterEditorData>* CharacterEditorDataPtr = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				TestNotNull(TEXT("Editor data"), CharacterEditorDataPtr);
				if (CharacterEditorDataPtr)
				{
					TArray<FVector3f> LODBefore = SnapshotLOD0Positions(CharacterEditorDataPtr->Get().FaceMesh);

					TSharedRef<FMetaHumanCharacterIdentity::FState> Modified = Subsystem->CopyFaceState(Character.Get());
					Modified->Randomize(1.0f);
					Subsystem->ApplyFaceState(Character.Get(), Modified);

					TArray<FVector3f> LODAfter = SnapshotLOD0Positions(CharacterEditorDataPtr->Get().FaceMesh);
					TestTrue(TEXT("LOD0 has vertices after apply"), LODAfter.Num() > 0);
					TestTrue(TEXT("Face LOD0 vertices changed after apply"), DoVerticesDiffer(LODBefore, LODAfter));
				}
			});

			It("should fire OnFaceStateChanged delegate", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				bool bDelegateFired = false;
				FDelegateHandle Handle = Subsystem->OnFaceStateChanged(Character.Get()).AddLambda([&bDelegateFired]() { bDelegateFired = true; });

				TSharedRef<FMetaHumanCharacterIdentity::FState> Copy = Subsystem->CopyFaceState(Character.Get());
				Subsystem->ApplyFaceState(Character.Get(), Copy);

				TestTrue(TEXT("Delegate fired"), bDelegateFired);
				Subsystem->OnFaceStateChanged(Character.Get()).Remove(Handle);
			});
		});

		Describe("CommitFaceState", [this]()
		{
			It("should update serialized buffer on character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TSharedRef<FMetaHumanCharacterIdentity::FState> Modified = Subsystem->CopyFaceState(Character.Get());
				Modified->Randomize(1.0f);

				FSharedBuffer BufferBefore = Character->GetFaceStateData();
				Subsystem->CommitFaceState(Character.Get(), Modified);
				FSharedBuffer BufferAfter = Character->GetFaceStateData();

				TestTrue(TEXT("Buffer size > 0"), BufferAfter.GetSize() > 0);
				TestFalse(TEXT("Face state buffer changed after commit"), AreBuffersEqual(BufferBefore, BufferAfter));
			});
		});

		Describe("Landmarks", [this]()
		{
			It("should return landmark positions", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TArray<FVector3f> Landmarks = Subsystem->GetFaceLandmarks(Character.Get());
				TestTrue(TEXT("Has landmarks"), Landmarks.Num() > 0);
			});

			It("should translate a landmark and change vertices", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TArray<FVector3f> LandmarksBefore = Subsystem->GetFaceLandmarks(Character.Get());
				if (LandmarksBefore.Num() > 0)
				{
					FMetaHumanRigEvaluatedState EvaluatedStateBefore = Subsystem->GetFaceState(Character.Get())->Evaluate();

					TSharedRef<const FMetaHumanCharacterIdentity::FState> StateCopy = Subsystem->GetFaceState(Character.Get());
					const FVector3f Delta(1.0f, 0.0f, 0.0f);
					TArray<FVector3f> UpdatedLandmarks = Subsystem->TranslateFaceLandmark(Character.Get(), StateCopy, /*InLandmarkIndex*/ 0, Delta, /*bSymmetric*/ false);

					FMetaHumanRigEvaluatedState EvaluatedStateAfter = Subsystem->GetFaceState(Character.Get())->Evaluate();
					TestTrue(TEXT("Vertices changed after landmark translate"), DoVerticesDiffer(EvaluatedStateBefore.Vertices, EvaluatedStateAfter.Vertices));
				}
			});
		});

		Describe("ModelCoefficients", [this]()
		{
			It("should get face model coefficients", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TArray<float> Coefficients;
				Subsystem->GetFaceModelCoefficients(Character.Get(), Coefficients);
				TestTrue(TEXT("Has coefficients"), Coefficients.Num() > 0);
			});
		});
	});

	// =========================================================================
	// Body State Tests
	// =========================================================================
	Describe("BodyState", [this]()
	{
		BeforeEach([this]()
		{
			if (!SetupCharacterForEditing())
			{
				return;
			}
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		Describe("GetBodyState and CopyBodyState", [this]()
		{
			It("should return valid body state with vertices", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> State = Subsystem->GetBodyState(Character.Get());
				FMetaHumanRigEvaluatedState EvaluatedState = State->GetVerticesAndVertexNormals();
				TestTrue(TEXT("Has body vertices"), EvaluatedState.Vertices.Num() > 0);
			});

			It("should return independent mutable copy", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TSharedRef<FMetaHumanCharacterBodyIdentity::FState> Copy = Subsystem->CopyBodyState(Character.Get());
				FMetaHumanRigEvaluatedState EvaluatedStateBefore = Copy->GetVerticesAndVertexNormals();

				Copy->SetGlobalDeltaScale(0.5f);
				FMetaHumanRigEvaluatedState SubsystemEval = Subsystem->GetBodyState(Character.Get())->GetVerticesAndVertexNormals();

				TestFalse(TEXT("Copy state change did not affect subsystem state"), DoVerticesDiffer(EvaluatedStateBefore.Vertices, SubsystemEval.Vertices));
			});
		});

		Describe("ApplyBodyState", [this]()
		{
			It("should update body vertices with Full mode and update LOD model", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				const TSharedRef<FMetaHumanCharacterEditorData>* CharacterEditorDataPtr = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				TestNotNull(TEXT("Editor data"), CharacterEditorDataPtr);
				if (CharacterEditorDataPtr)
				{
					TArray<FVector3f> LODBefore = SnapshotLOD0Positions(CharacterEditorDataPtr->Get().BodyMesh);

					TSharedRef<FMetaHumanCharacterBodyIdentity::FState> Modified = Subsystem->CopyBodyState(Character.Get());
					Modified->SetGlobalDeltaScale(0.5f);

					Subsystem->ApplyBodyState(Character.Get(), Modified, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);

					FMetaHumanRigEvaluatedState EvaluatedStateAfter = Subsystem->GetBodyState(Character.Get())->GetVerticesAndVertexNormals();
					TestTrue(TEXT("Body has vertices after apply"), EvaluatedStateAfter.Vertices.Num() > 0);

					TArray<FVector3f> LODAfter = SnapshotLOD0Positions(CharacterEditorDataPtr->Get().BodyMesh);
					TestTrue(TEXT("LOD0 has vertices"), LODAfter.Num() > 0);
					TestTrue(TEXT("Body LOD0 vertices changed after apply"), DoVerticesDiffer(LODBefore, LODAfter));
				}
			});

			It("apply body state with Minimal mode should not fail", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TSharedRef<FMetaHumanCharacterBodyIdentity::FState> Modified = Subsystem->CopyBodyState(Character.Get());
				Modified->SetGlobalDeltaScale(0.5f);

				// Smoke test, just run with Minimal as input
				Subsystem->ApplyBodyState(Character.Get(), Modified, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);
			});

			It("should fire OnBodyStateChanged delegate", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				bool bDelegateFired = false;
				FDelegateHandle Handle = Subsystem->OnBodyStateChanged(Character.Get()).AddLambda([&bDelegateFired]() { bDelegateFired = true; });

				TSharedRef<FMetaHumanCharacterBodyIdentity::FState> Copy = Subsystem->CopyBodyState(Character.Get());
				Subsystem->ApplyBodyState(Character.Get(), Copy, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);

				TestTrue(TEXT("Delegate fired"), bDelegateFired);
				Subsystem->OnBodyStateChanged(Character.Get()).Remove(Handle);
			});
		});

		Describe("CommitBodyState", [this]()
		{
			It("should update serialized buffer on character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				FSharedBuffer BufferBefore = Character->GetBodyStateData();

				TSharedRef<FMetaHumanCharacterBodyIdentity::FState> Modified = Subsystem->CopyBodyState(Character.Get());
				Modified->SetGlobalDeltaScale(0.5f);
				Subsystem->CommitBodyState(Character.Get(), Modified);

				FSharedBuffer BufferAfter = Character->GetBodyStateData();
				TestTrue(TEXT("Buffer size > 0"), BufferAfter.GetSize() > 0);
				TestFalse(TEXT("Body state buffer changed after commit"), AreBuffersEqual(BufferBefore, BufferAfter));
			});
		});

		Describe("BodyConstraints", [this]()
		{
			It("should return valid constraints", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TArray<FMetaHumanCharacterBodyConstraint> Constraints = Subsystem->GetBodyConstraints(Character.Get());
				TestTrue(TEXT("Has constraints"), Constraints.Num() > 0);
			});
		});

		Describe("BodyType", [this]()
		{
			It("should report non-fixed body type for default parametric body", [this]()
			{
				TestFalse(TEXT("Default is not fixed body type"), Character->bFixedBodyType);
			});

			It("should set scale body with SetBodyGlobalDeltaScale", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				Subsystem->SetBodyGlobalDeltaScale(Character.Get(), 2.0f);
				TestEqual(TEXT("Delta scale set"), Subsystem->GetBodyGlobalDeltaScale(Character.Get()), 2.0f);
			});
		});
	});

	// =========================================================================
	// DNA Rigging Tests
	// =========================================================================
	Describe("DNARigging", [this]()
	{
		BeforeEach([this]()
		{
			SetupCharacterForEditing();
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		Describe("ApplyFaceDNA", [this]()
		{
			It("should apply DNA and update the editor face mesh DNA user data", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				const TSharedRef<FMetaHumanCharacterEditorData>* CharacterEditorDataPtr = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				TestNotNull(TEXT("Editor data"), CharacterEditorDataPtr);
				
				UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Face, GetTransientPackage());
				TestNotNull(TEXT("Archetype DNA"), ArchetypeDNA);
				
				if (ArchetypeDNA && CharacterEditorDataPtr)
				{
					UDNA* FaceMeshUDNA = USkelMeshDNAUtils::GetMeshDNAAsset(CharacterEditorDataPtr->Get().FaceMesh);
					TestNotNull(TEXT("Face mesh has DNA asset"), FaceMeshUDNA);

					// Snapshot the face mesh's reader BEFORE applying so we can verify ApplyFaceDNA
					// actually mutated the face mesh's DNA. Comparing the post-apply mesh reader
					// against the returned reader is not a robust check: whether they alias depends
					// on the SetDNAReader copy policy (an internal implementation detail), not on
					// whether ApplyFaceDNA did its job.
					TSharedPtr<IDNAReader> BeforeFaceMeshReader = FaceMeshUDNA->GetDNAReader();

					TSharedPtr<IDNAReader> ArchetypeDNAReader = ArchetypeDNA->GetDNAReader();
					TestTrue(TEXT("Archetype DNA reader is valid"), ArchetypeDNAReader.IsValid());
					if (!ArchetypeDNAReader.IsValid()) 
					{
						return;
					}
					const bool bApplied = Subsystem->ApplyFaceDNA(Character.Get(), ArchetypeDNAReader.ToSharedRef());
					TestTrue(TEXT("ApplyFaceDNA succeeded"), bApplied);

					TestNotEqual(TEXT("Face mesh DNA reader changed after ApplyFaceDNA"),
						BeforeFaceMeshReader.Get(), FaceMeshUDNA->GetDNAReader().Get());

					UDNA* AfterFaceDNA = USkelMeshDNAUtils::GetMeshDNAAsset(CharacterEditorDataPtr->Get().FaceMesh);
					TestNotNull(TEXT("Face mesh has DNA asset"), AfterFaceDNA);
					if (AfterFaceDNA)
					{
						TestTrue(TEXT("DNA reader valid on mesh"), AfterFaceDNA->GetDNAReader().IsValid());
					}
				}
			});
		});

		Describe("CommitFaceDNA", [this]()
		{
			It("should store DNA buffer in character and set rigging state to Rigged", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TestEqual(TEXT("Default state is Unrigged"), Subsystem->GetRiggingState(Character.Get()), EMetaHumanCharacterRigState::Unrigged);
				TestFalse(TEXT("No face DNA before commit"), Character->HasFaceDNA());

				UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Face, GetTransientPackage());
				TestNotNull(TEXT("Archetype DNA"), ArchetypeDNA);
				if (ArchetypeDNA)
				{
					Subsystem->CommitFaceDNA(Character.Get(), ArchetypeDNA->GetDNAReader().ToSharedRef());

					TestTrue(TEXT("Has face DNA after commit"), Character->HasFaceDNA());
					TArray<uint8> Buffer = Character->GetFaceDNABuffer();
					TestTrue(TEXT("DNA buffer non-empty"), Buffer.Num() > 0);

					TestEqual(TEXT("State is Rigged"), Subsystem->GetRiggingState(Character.Get()), EMetaHumanCharacterRigState::Rigged);
				}
			});
		});

		Describe("ApplyBodyDNA", [this]()
		{
			It("should apply body DNA and update body mesh DNA user data", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				const TSharedRef<FMetaHumanCharacterEditorData>* CharacterEditorDataPtr = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				TestNotNull(TEXT("Editor data"), CharacterEditorDataPtr);

				UDNA* BodyArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Body, GetTransientPackage());
				TestNotNull(TEXT("Body archetype DNA"), BodyArchetypeDNA);
				if (BodyArchetypeDNA && CharacterEditorDataPtr)
				{
					// ApplyBodyDNA with the default bImportMesh=true creates a brand-new
					// USkeletalMesh and replaces CharacterData->BodyMesh with it. The previous
					// body mesh's UDNA is orphaned, not mutated. Verify the swap happened by
					// comparing the BodyMesh pointer before and after the call.
					USkeletalMesh* BeforeBodyMesh = CharacterEditorDataPtr->Get().BodyMesh;
					TestNotNull(TEXT("Body mesh exists before apply"), BeforeBodyMesh);

					TSharedPtr<IDNAReader> AppliedDNAReader = Subsystem->ApplyBodyDNA(Character.Get(), BodyArchetypeDNA->GetDNAReader().ToSharedRef());
					TestTrue(TEXT("ApplyBodyDNA returned valid reader"), AppliedDNAReader.IsValid());

					TestNotEqual(TEXT("Body mesh was replaced after ApplyBodyDNA"),
						BeforeBodyMesh, CharacterEditorDataPtr->Get().BodyMesh.Get());

					UDNA* MeshDNA = USkelMeshDNAUtils::GetMeshDNAAsset(CharacterEditorDataPtr->Get().BodyMesh);
					TestNotNull(TEXT("Post-apply body mesh has DNA asset"), MeshDNA);
					if (MeshDNA)
					{
						TestTrue(TEXT("Post-apply body mesh DNA reader is valid"), MeshDNA->GetDNAReader().IsValid());
					}
				}
			});
		});

		Describe("CommitBodyDNA", [this]()
		{
			It("should store body DNA buffer in character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TestFalse(TEXT("No body DNA before commit"), Character->HasBodyDNA());

				UDNA* BodyArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Body, GetTransientPackage());
				if (BodyArchetypeDNA)
				{
					const bool bFixedBodyType = true;
					Subsystem->CommitBodyDNA(Character.Get(), BodyArchetypeDNA->GetDNAReader().ToSharedRef(), bFixedBodyType);

					TestTrue(TEXT("Has body DNA"), Character->HasBodyDNA());
					TArray<uint8> Buffer = Character->GetBodyDNABuffer();
					TestTrue(TEXT("Body DNA buffer non-empty"), Buffer.Num() > 0);
				}
			});
		});

		Describe("RemoveFaceRig", [this]()
		{
			It("should clear DNA and rigging state to Unrigged", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Face, GetTransientPackage());
				if (ArchetypeDNA)
				{
					Subsystem->CommitFaceDNA(Character.Get(), ArchetypeDNA->GetDNAReader().ToSharedRef());
					TestTrue(TEXT("Has DNA before remove"), Character->HasFaceDNA());

					Subsystem->RemoveFaceRig(Character.Get());

					TestFalse(TEXT("No face DNA after remove"), Character->HasFaceDNA());
					TestTrue(TEXT("DNA buffer cleared"), Character->GetFaceDNABuffer().Num() == 0);
					TestEqual(TEXT("Back to Unrigged"), Subsystem->GetRiggingState(Character.Get()), EMetaHumanCharacterRigState::Unrigged);
				}
			});

			It("should be a no-op when given a null character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				// Now that RemoveFaceRig takes a raw pointer (for BP/Python reflection),
				// passing nullptr must short-circuit cleanly rather than crash.
				AddExpectedError(TEXT("called with invalid character"), EAutomationExpectedErrorFlags::Contains);
				Subsystem->RemoveFaceRig(nullptr);
			});

			It("should be a no-op when given a character not opened for editing", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				TStrongObjectPtr<UMetaHumanCharacter> UneditedCharacter(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				Subsystem->InitializeMetaHumanCharacter(UneditedCharacter.Get());
				AddExpectedError(TEXT("was not previously opened for edit"), EAutomationExpectedErrorFlags::Contains);
				Subsystem->RemoveFaceRig(UneditedCharacter.Get());
			});
		});

		Describe("RemoveBodyRig", [this]()
		{
			It("should clear body DNA and revert mesh", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);

				UDNA* BodyArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Body, GetTransientPackage());
				if (BodyArchetypeDNA)
				{
					const bool bFixedBodyType = true;
					Subsystem->CommitBodyDNA(Character.Get(), BodyArchetypeDNA->GetDNAReader().ToSharedRef(), bFixedBodyType);
					TestTrue(TEXT("Has body DNA before remove"), Character->HasBodyDNA());

					Subsystem->RemoveBodyRig(Character.Get());

					TestFalse(TEXT("No body DNA after remove"), Character->HasBodyDNA());
					TestTrue(TEXT("Body DNA buffer cleared"), Character->GetBodyDNABuffer().Num() == 0);
				}
			});
		});
	});

	// =========================================================================
	// Mesh Import/Conform Tests
	// =========================================================================
	Describe("MeshImport", [this]()
	{
		BeforeEach([this]()
		{
			if (!SetupCharacterForEditing())
			{
				return;
			}
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		Describe("ImportFromFaceDna", [this]()
		{
			It("should import whole rig from DNA, update rigging state and fit face state", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Face, GetTransientPackage());
				TestNotNull(TEXT("Archetype DNA"), ArchetypeDNA);
				if (!ArchetypeDNA)
				{
					return;
				}

				const TSharedRef<FMetaHumanCharacterEditorData>* DataPtr = Subsystem->GetMetaHumanCharacterEditorData(Character.Get());
				TestNotNull(TEXT("Editor data"), DataPtr);
				if (!DataPtr)
				{
					return;
				}

				TSharedRef<IDNAReader> InputDNAReader = ArchetypeDNA->GetDNAReader().ToSharedRef();

				// Import the full rig
				FImportFromDNAParams ImportParams;
				ImportParams.bImportWholeRig = true;
				EImportErrorCode ImportResult = Subsystem->ImportFromFaceDna(Character.Get(), InputDNAReader, ImportParams);
				TestEqual(TEXT("Import returned Success"), ImportResult, EImportErrorCode::Success);

				TestTrue(TEXT("Has face DNA after import"), Character->HasFaceDNA());
				TestEqual(TEXT("Rigging state is Rigged after import"),
					Subsystem->GetRiggingState(Character.Get()),
					EMetaHumanCharacterRigState::Rigged);

				TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState = Subsystem->GetFaceState(Character.Get());
				const FMetaHumanRigEvaluatedState EvaluatedState = FaceState->Evaluate();

				// From UE::MetaHuman::FaceToDnaMeshIndexMapping from MetaHumanCharacterEditorSubsystem.cpp
				const TArray<int32> ConformedMeshIndices = {
					0, // EHeadFitToTargetMeshes::Head
					1, // EHeadFitToTargetMeshes::Teeth
					3, // EHeadFitToTargetMeshes::LeftEye
					4  // EHeadFitToTargetMeshes::RightEye
				};
				constexpr float Tolerance = 0.001f;
				const bool bDnaMatchesState = FMetaHumanCharacterSkelMeshUtils::CompareDnaToStateVertices(
					InputDNAReader, EvaluatedState.Vertices, FaceState, Tolerance, ConformedMeshIndices);
				TestTrue(TEXT("Input DNA vertices match face state vertices after whole-rig import"), bDnaMatchesState);
			});

			It("should conform to DNA face mesh without applying the full rig", [this]()
				{
					UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
					TestNotNull(TEXT("Subsystem"), Subsystem);
					if (!Subsystem)
					{
						return;
					}

					UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(
						EMetaHumanImportDNAType::Face, GetTransientPackage());
					TestNotNull(TEXT("Archetype DNA"), ArchetypeDNA);
					if (!ArchetypeDNA)
					{
						return;
					}

					FMetaHumanRigEvaluatedState EvaluatedStateBefore = Subsystem->GetFaceState(Character.Get())->Evaluate();

					// Do not import the full rig, conform the face state to the DNA mesh
					FImportFromDNAParams ImportParams;
					ImportParams.bImportWholeRig = false;
					ImportParams.AlignmentOptions = EAlignmentOptions::ScalingRotationTranslation;
					EImportErrorCode ImportResult = Subsystem->ImportFromFaceDna(Character.Get(), ArchetypeDNA->GetDNAReader().ToSharedRef(), ImportParams);
					TestEqual(TEXT("Import returned Success"), ImportResult, EImportErrorCode::Success);

					// Face state should be modified
					FMetaHumanRigEvaluatedState EvaluatedStateAfter = Subsystem->GetFaceState(Character.Get())->Evaluate();
					TestTrue(TEXT("Face state vertices changed after fit"),
						DoVerticesDiffer(EvaluatedStateBefore.Vertices, EvaluatedStateAfter.Vertices));

					// Character is not rigged
					TestFalse(TEXT("Has no face DNA after import"), Character->HasFaceDNA());
					TestEqual(TEXT("Rigging state is Unrigged after import"),
						Subsystem->GetRiggingState(Character.Get()),
						EMetaHumanCharacterRigState::Unrigged);
				});
		});
	});

	// =========================================================================
	// Actor Management Tests
	// =========================================================================
	Describe("PreviewActor", [this]()
	{
		BeforeEach([this]()
		{
			TestWorld = MakeUnique<FScopedTestWorld>();
			if (!SetupCharacterForEditing())
			{
				return;
			}
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
			TestWorld.Reset();
		});

		Describe("CreateMetaHumanCharacterEditorActor", [this]()
		{
			It("should spawn editor actor interface in world with face and body skeletal mesh components", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}
				TestNotNull(TEXT("Test world"), TestWorld->World);

				TScriptInterface<IMetaHumanCharacterEditorActorInterface> ActorInterface =
					Subsystem->CreateMetaHumanCharacterEditorActor(Character.Get(), TestWorld->World);

				TestNotNull(TEXT("Spawned actor is not null"), ActorInterface.GetObject());
				TestNotNull(TEXT("Actor interface valid"), ActorInterface.GetInterface());

				if (ActorInterface.GetInterface())
				{
					TestNotNull(TEXT("Actor Face mesh assigned"), ActorInterface->GetFaceComponent()->GetSkeletalMeshAsset());
					TestNotNull(TEXT("Actor Body mesh assigned"), ActorInterface->GetBodyComponent()->GetSkeletalMeshAsset());
				}

				Subsystem->DestroyMetaHumanCharacterEditorActor(ActorInterface);
			});
		});

		Describe("TryGetMetaHumanCharacterEditorActorClass", [this]()
		{
			It("should return valid actor class for valid character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				FText FailureReason;
				TSubclassOf<AActor> ActorClass;
				const bool bResult = Subsystem->TryGetMetaHumanCharacterEditorActorClass(Character.Get(), ActorClass, FailureReason);

				TestTrue(TEXT("Got actor class"), bResult);
				TestNotNull(TEXT("Actor class is valid"), ActorClass.Get());
			});
		});

		Describe("CreateMetaHumanInvisibleDrivingActor", [this]()
		{
			It("should create invisible driving actor for retargeting", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				TScriptInterface<IMetaHumanCharacterEditorActorInterface> ActorInterface =
					Subsystem->CreateMetaHumanCharacterEditorActor(Character.Get(), TestWorld->World);
				TestNotNull(TEXT("Editor actor"), ActorInterface.GetObject());

				if (ActorInterface.GetInterface())
				{
					Subsystem->CreateMetaHumanInvisibleDrivingActor(Character.Get(), ActorInterface, TestWorld->World);

					TObjectPtr<AMetaHumanInvisibleDrivingActor> DrivingActor = Subsystem->GetInvisibleDrivingActor(Character.Get());
					TestNotNull(TEXT("Driving actor created"), DrivingActor.Get());
				}

				Subsystem->DestroyMetaHumanCharacterEditorActor(ActorInterface);
			});
		});
	});

	// =========================================================================
	// Build Pipeline Tests
	// =========================================================================
	Describe("CanBuildMetaHuman", [this]()
	{
		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		It("should return false for default character", [this]()
		{
			if (!SetupCharacterForEditing())
			{
				return;
			}

			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("Subsystem"), Subsystem);
			if (!Subsystem)
			{
				return;
			}

			FText ErrorMessage;
			const bool bCanBuild = Subsystem->CanBuildMetaHuman(Character.Get(), ErrorMessage);
			TestFalse(TEXT("Cannot build default character"), bCanBuild);
			TestFalse(TEXT("Has error message"), ErrorMessage.IsEmpty());
		});

		It("should return false for character with face DNA but without high-res textures", [this]()
		{
			if (!SetupCharacterForEditing())
			{
				return;
			}

			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("Subsystem"), Subsystem);
			if (!Subsystem)
			{
				return;
			}

			UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Face, GetTransientPackage());
			TestNotNull(TEXT("Archetype DNA"), ArchetypeDNA);
			if (!ArchetypeDNA)
			{
				return;
			}

			FImportFromDNAParams ImportParams;
			ImportParams.bImportWholeRig = true;
			Subsystem->ImportFromFaceDna(Character.Get(), ArchetypeDNA->GetDNAReader().ToSharedRef(), ImportParams);

			TestEqual(TEXT("Rigging state is Rigged"),
				Subsystem->GetRiggingState(Character.Get()),
				EMetaHumanCharacterRigState::Rigged);
			TestFalse(TEXT("No high-res textures"), Character->HasHighResolutionTextures());

			FText ErrorMessage;
			const bool bCanBuild = Subsystem->CanBuildMetaHuman(Character.Get(), ErrorMessage);
			TestFalse(TEXT("Cannot build without high-res textures"), bCanBuild);
			TestFalse(TEXT("Has error message"), ErrorMessage.IsEmpty());
		});

		It("should return false for character with high-res textures but no face DNA", [this]()
		{
			if (!SetupCharacterWithTextures())
			{
				return;
			}

			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("Subsystem"), Subsystem);
			if (!Subsystem)
			{
				return;
			}

			TestEqual(TEXT("Rigging state is Unrigged"),
				Subsystem->GetRiggingState(Character.Get()),
				EMetaHumanCharacterRigState::Unrigged);
			TestTrue(TEXT("Has high-res textures"), Character->HasHighResolutionTextures());

			FText ErrorMessage;
			const bool bCanBuild = Subsystem->CanBuildMetaHuman(Character.Get(), ErrorMessage);
			TestFalse(TEXT("Cannot build without face DNA"), bCanBuild);
			TestFalse(TEXT("Has error message"), ErrorMessage.IsEmpty());
		});

		It("should return false for character not added for editing", [this]()
		{
			Character = TStrongObjectPtr<UMetaHumanCharacter>(
				NewObject<UMetaHumanCharacter>(GetTransientPackage()));
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("Subsystem"), Subsystem);
			if (!Subsystem)
			{
				return;
			}
			Subsystem->InitializeMetaHumanCharacter(Character.Get());

			FText ErrorMessage;
			const bool bCanBuild = Subsystem->CanBuildMetaHuman(Character.Get(), ErrorMessage);
			TestFalse(TEXT("Cannot build non-added character"), bCanBuild);
			TestFalse(TEXT("Has error message"), ErrorMessage.IsEmpty());
		});

		It("should return true for character with face DNA and high-res textures", [this]()
		{
			if (!SetupCharacterWithTextures())
			{
				return;
			}
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			TestNotNull(TEXT("Subsystem"), Subsystem);
			if (!Subsystem)
			{
				return;
			}

			UDNA* ArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAsset(EMetaHumanImportDNAType::Face, GetTransientPackage());
			TestNotNull(TEXT("Archetype DNA"), ArchetypeDNA);
			if (!ArchetypeDNA)
			{
				return;
			}

			FImportFromDNAParams ImportParams;
			ImportParams.bImportWholeRig = true;
			Subsystem->ImportFromFaceDna(Character.Get(), ArchetypeDNA->GetDNAReader().ToSharedRef(), ImportParams);

			TestEqual(TEXT("Rigging state is Rigged"),
				Subsystem->GetRiggingState(Character.Get()),
				EMetaHumanCharacterRigState::Rigged);
			TestTrue(TEXT("Has high-res textures"), Character->HasHighResolutionTextures());

			FText ErrorMessage;
			const bool bCanBuild = Subsystem->CanBuildMetaHuman(Character.Get(), ErrorMessage);
			TestTrue(TEXT("Can build with DNA and textures"), bCanBuild);
			TestTrue(TEXT("No error message"), ErrorMessage.IsEmpty());
		});
	});

	// =========================================================================
	// Preview Pipeline Tests
	// =========================================================================
	Describe("PreviewPipeline", [this]()
	{
		BeforeEach([this]()
		{
			if (!SetupCharacterForEditing())
			{
				return;
			}
		});

		AfterEach([this]()
		{
			UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
			if (Subsystem && Character.IsValid() && Subsystem->IsObjectAddedForEditing(Character.Get()))
			{
				Subsystem->RemoveObjectToEdit(Character.Get());
			}
			Character.Reset();
		});

		Describe("GetPreviewCollection", [this]()
		{
			It("should return collection for character added for editing", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				UMetaHumanCollection* Collection = Subsystem->GetPreviewCollection(Character.Get());
				TestNotNull(TEXT("Preview collection is not null"), Collection);
			});

			It("should return nullptr for character not added for editing", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				TStrongObjectPtr<UMetaHumanCharacter> NonEditedCharacter(NewObject<UMetaHumanCharacter>(GetTransientPackage()));
				Subsystem->InitializeMetaHumanCharacter(NonEditedCharacter.Get());

				UMetaHumanCollection* Collection = Subsystem->GetPreviewCollection(NonEditedCharacter.Get());
				TestNull(TEXT("Preview collection is null for non-edited character"), Collection);
			});
		});

		Describe("RunCharacterEditorPipelineForPreview", [this]()
		{
			It("should execute successfully for default character", [this]()
			{
				UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
				TestNotNull(TEXT("Subsystem"), Subsystem);
				if (!Subsystem)
				{
					return;
				}

				// Smoke test
				Subsystem->RunCharacterEditorPipelineForPreview(Character.Get());
			});
		});
	});
}

#endif
