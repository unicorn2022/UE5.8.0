// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Find.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Editor/EditorEngine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/NameTypes.h"
#include "UObject/ScriptInterface.h"
#include "Stats/StatsMisc.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "ImageUtils.h"
#include "SkelMeshDNAUtils.h"
#include "DNA.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterTextureSynthesis.h"
#include "MetaHumanInstance.h"
#include "MetaHumanFaceTextureSynthesizer.h"
#include "MetaHumanCharacterPalette.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanWardrobeItem.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "Cloud/MetaHumanTextureSynthesisServiceRequest.h"
#include "Cloud/MetaHumanARServiceRequest.h"
#include "Cloud/MetaHumanTDSUtils.h"
#include "Cloud/MetaHumanCloudServicesSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCharacterEditorTests, Log, All)


struct FScopedTestWorld
{
	FScopedTestWorld()
	{
		const FName UniqueWorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("MetaHumanCharacterTestWorld"));
		World = NewObject<UWorld>(GetTransientPackage(), UniqueWorldName);
		World->WorldType = EWorldType::EditorPreview;

		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(World->WorldType);
		WorldContext.SetCurrentWorld(World);

		World->CreatePhysicsScene(nullptr);

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
		const bool bInformEngineOfWorld = false;
		World->DestroyWorld(bInformEngineOfWorld);
	}

	UWorld* World = nullptr;
};

namespace SynthethicDataGenerator 
{
	FMetaHumanCharacterEyelashesProperties GenerateEyelashesPropertiesData()
	{
		FMetaHumanCharacterEyelashesProperties GeneratedEyelashesProperties;

		GeneratedEyelashesProperties.Type = static_cast<EMetaHumanCharacterEyelashesType>(FMath::RandRange(0, static_cast<uint8>(EMetaHumanCharacterEyelashesType::Count) - 1));
		GeneratedEyelashesProperties.DyeColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		GeneratedEyelashesProperties.Melanin = FMath::FRandRange(0.f,1.f);
		GeneratedEyelashesProperties.Redness = FMath::FRandRange(0.f, 1.f);
		GeneratedEyelashesProperties.Roughness = FMath::FRandRange(0.f, 1.f);
		GeneratedEyelashesProperties.SaltAndPepper = FMath::FRandRange(0.f, 1.f);
		GeneratedEyelashesProperties.Lightness = FMath::FRandRange(0.f, 1.f);
		
		GeneratedEyelashesProperties.bEnableGrooms = FMath::RandBool();

		return GeneratedEyelashesProperties;

	}

	FMetaHumanCharacterTeethProperties GenerateTeethPropertiesData()
	{
		FMetaHumanCharacterTeethProperties GeneratedTeethProperties;

		GeneratedTeethProperties.ToothLength = FMath::FRandRange(-1.f, 1.f);
		GeneratedTeethProperties.ToothSpacing = FMath::FRandRange(-1.f, 1.f);
		GeneratedTeethProperties.UpperShift = FMath::FRandRange(-1.f, 1.f);
		GeneratedTeethProperties.LowerShift = FMath::FRandRange(-1.f, 1.f);
		GeneratedTeethProperties.Overbite = FMath::FRandRange(-1.f, 1.f);
		GeneratedTeethProperties.Overjet = FMath::FRandRange(-1.f, 1.f);
		GeneratedTeethProperties.WornDown = FMath::FRandRange(0.f, 1.f);
		GeneratedTeethProperties.Polycanine = FMath::FRandRange(0.f, 1.f);
		GeneratedTeethProperties.RecedingGums = FMath::FRandRange(0.f, 1.f);
		GeneratedTeethProperties.Narrowness = FMath::FRandRange(0.f, 1.f);
		GeneratedTeethProperties.Variation = FMath::FRandRange(0.f, 1.f);
		GeneratedTeethProperties.JawOpen = FMath::FRandRange(0.f, 1.f);

		GeneratedTeethProperties.TeethColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		GeneratedTeethProperties.GumColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		GeneratedTeethProperties.PlaqueColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		GeneratedTeethProperties.PlaqueAmount = FMath::FRandRange(0.f, 1.f);

		GeneratedTeethProperties.EnableShowTeethExpression = FMath::RandBool();

		return GeneratedTeethProperties;
	}

	FMetaHumanCharacterMakeupSettings GenerateMakeupPropertiesData() 
	{
		FMetaHumanCharacterMakeupSettings MakeupProperties;

		// Foundation
		MakeupProperties.Foundation.bApplyFoundation = FMath::RandBool();
		MakeupProperties.Foundation.Color = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		MakeupProperties.Foundation.Intensity = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Foundation.Roughness = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Foundation.Concealer = FMath::FRandRange(0.f, 1.f);

		//Eye
		MakeupProperties.Eyes.Type = static_cast<EMetaHumanCharacterEyeMakeupType>(FMath::RandRange(0, static_cast<uint8>(EMetaHumanCharacterEyeMakeupType::Count) - 1));
		MakeupProperties.Eyes.PrimaryColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		MakeupProperties.Eyes.SecondaryColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		MakeupProperties.Eyes.Roughness = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Eyes.Opacity = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Eyes.Metalness = FMath::FRandRange(0.f, 1.f);

		//Blush
		MakeupProperties.Blush.Type = static_cast<EMetaHumanCharacterBlushMakeupType>(FMath::RandRange(0, static_cast<uint8>(EMetaHumanCharacterBlushMakeupType::Count) - 1));
		MakeupProperties.Blush.Color = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);
		MakeupProperties.Blush.Intensity = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Blush.Roughness = FMath::FRandRange(0.f, 1.f);

		//Lips
		MakeupProperties.Lips.Type = static_cast<EMetaHumanCharacterLipsMakeupType>(FMath::RandRange(0, static_cast<uint8>(EMetaHumanCharacterLipsMakeupType::Count) - 1));
		MakeupProperties.Lips.Color = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		MakeupProperties.Lips.Roughness = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Lips.Opacity = FMath::FRandRange(0.f, 1.f);
		MakeupProperties.Lips.Metalness = FMath::FRandRange(0.f, 1.f);

		return MakeupProperties;
	}

	FMetaHumanCharacterEyeProperties GenerateEyesSettings()
	{
		FMetaHumanCharacterEyeProperties EyeProperties;

		//Iris
		EyeProperties.Iris.IrisPattern = static_cast<EMetaHumanCharacterEyesIrisPattern>(FMath::RandRange(0, static_cast<uint8>(EMetaHumanCharacterEyesIrisPattern::Count) - 1));
		EyeProperties.Iris.IrisRotation = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.PrimaryColorU = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.PrimaryColorV = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.SecondaryColorU = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.SecondaryColorV = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.ColorBlend = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.BlendMethod = static_cast<EMetaHumanCharacterEyesBlendMethod>(FMath::RandRange(0.f,1.f));
		EyeProperties.Iris.ShadowDetails = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.LimbalRingSize = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.LimbalRingSoftness = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Iris.LimbalRingColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);
		EyeProperties.Iris.GlobalSaturation = FMath::FRandRange(0.f, 10.f);
		EyeProperties.Iris.GlobalTint = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		//Pupils
		EyeProperties.Pupil.Dilation = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Pupil.Feather = FMath::FRandRange(0.f, 1.f);

		//Cornea
		EyeProperties.Cornea.Size = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Cornea.LimbusSoftness = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Cornea.LimbusColor = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);

		//Sclera
		EyeProperties.Sclera.Rotation = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Sclera.bUseCustomTint = FMath::RandBool();
		EyeProperties.Sclera.Tint = FLinearColor(
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand(),
			FMath::FRand()
		);
		EyeProperties.Sclera.TransmissionSpread = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Sclera.VascularityIntensity = FMath::FRandRange(0.f, 1.f);
		EyeProperties.Sclera.VascularityCoverage = FMath::FRandRange(0.f, 1.f);
		
		return EyeProperties;
	}
}

static bool CheckSynthesizedTexturesInFaceMaterial(const UMaterialInterface* FaceMaterial, const UMetaHumanCharacter* Character, EFaceTextureType TextureType)
{
	check(FaceMaterial);
	check(Character);

	if (!Character->SynthesizedFaceTextures.Contains(TextureType))
	{
		return false;
	}

	// Get the Texture for the slot with the same name as the TextureType
	const bool bWithVTSupport = Character->HasHighResolutionTextures();
	const FName TextureSlotName = FMetaHumanCharacterSkinMaterials::GetFaceTextureParameterName(TextureType, bWithVTSupport);
	UTexture* OutTexture = nullptr;
	if (!FaceMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(TextureSlotName), OutTexture))
	{
		return false;
	}

	if (OutTexture == nullptr)
	{
		return false;
	}

	return Character->SynthesizedFaceTextures[TextureType].Get() == Cast<UTexture2D>(OutTexture);
}

static TUniquePtr<UE::MetaHuman::TDSUtils::FScopedExchangeCodeHandler> MakeTDSScopedExchangeCodeHandler()
{
	FString TemplateHost;

	if (FParse::Value(FCommandLine::Get(), TEXT("-TdsTemplateHost="), TemplateHost))
	{
		FString TemplateName;

		if (FParse::Value(FCommandLine::Get(), TEXT("-TdsTemplateName="), TemplateName))
		{
			const UMetaHumanCloudServicesSettings* Settings = GetDefault<UMetaHumanCloudServicesSettings>();

			return MakeUnique<UE::MetaHuman::TDSUtils::FScopedExchangeCodeHandler>(
				TemplateHost,
				TemplateName,
				Settings->ProdEosConstants.ClientCredentialsId
			);
		}
	}

	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterEditorTest, "MetaHumanCreator.Character.AssetCreation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterEditorTest::RunTest(const FString& InParams)
{
	// Create a transient world where we can spawn an actor
	FScopedTestWorld TestWorld;
	UTEST_NOT_NULL_EXPR(TestWorld.World);

	// Get archetypes and validate
	// Call this first as it forces garbage collection
	USkeletalMesh* FaceArchetypeMesh = UMetaHumanCharacterEditorSubsystem::GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType::MetaHuman);
	UTEST_NOT_NULL_EXPR(FaceArchetypeMesh);
	
	USkeletalMesh* BodyArchetypeMesh = UMetaHumanCharacterEditorSubsystem::GetBodyArchetypeMesh(EMetaHumanCharacterTemplateType::MetaHuman);
	UTEST_NOT_NULL_EXPR(BodyArchetypeMesh);

	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());
	UTEST_TRUE("Initial FaceStateData is empty", Character->GetFaceStateData().GetSize() == 0);
	UTEST_TRUE("Initial BodyStateData is empty", Character->GetBodyStateData().GetSize() == 0);
	UTEST_TRUE("MetaHuman Character Synthesized Face Textures are empty for new Character", Character->SynthesizedFaceTextures.IsEmpty());

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(Character->IsCharacterValid());
	UTEST_FALSE("MetaHuman Character Face State is valid", Character->GetFaceStateData().GetSize() == 0);
	UTEST_FALSE("MetaHuman Character Body State is valid", Character->GetBodyStateData().GetSize() == 0);

	UTEST_TRUE("Character is added for editing", MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	ON_SCOPE_EXIT
	{
		// Ensure the subsystem cleans up any resources in the case
		// In particular for handling lifetime issues with the titan texture synthesis static array of allocations
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};
	
	// Check the CreateMetaHumanCharacterEditorActor expectations
	FText FailureReason;
	TSubclassOf<AActor> EditorActorClass;
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryGetMetaHumanCharacterEditorActorClass(Character, EditorActorClass, FailureReason));
	UTEST_NOT_NULL_EXPR(EditorActorClass.Get());

	TScriptInterface<IMetaHumanCharacterEditorActorInterface> CharacterActor = MetaHumanCharacterSubsystem->CreateMetaHumanCharacterEditorActor(Character, TestWorld.World);
	UTEST_NOT_NULL_EXPR(CharacterActor.GetObject());
	UTEST_NOT_SAME_PTR("MetaHuman Character Actor Face Skeletal Mesh", CharacterActor->GetFaceComponent()->GetSkeletalMeshAsset(), FaceArchetypeMesh);
	UTEST_NOT_SAME_PTR("MetaHuman Character Actor Body Skeletal Mesh", CharacterActor->GetBodyComponent()->GetSkeletalMeshAsset(), BodyArchetypeMesh);
	UTEST_FALSE("MetaHuman Character FaceStateData has data", Character->GetFaceStateData().GetSize() == 0);
	UTEST_FALSE("MetaHuman Character BodyStateData has data", Character->GetBodyStateData().GetSize() == 0);

	UTEST_EQUAL("MetaHuman Character synthesized face textures expected count", Character->SynthesizedFaceTextures.Num(), static_cast<int32>(EFaceTextureType::Count));

	// Check that synthesized textures are referenced by the Face preview 
	MetaHumanCharacterSubsystem->UpdateCharacterPreviewMaterial(Character, EMetaHumanCharacterSkinPreviewMaterial::Editable);
	const TArray<FSkeletalMaterial>& FaceMaterials = CharacterActor->GetFaceComponent()->GetSkeletalMeshAsset()->GetMaterials();
	const FSkeletalMaterial* MaterialSlot = Algo::FindBy(FaceMaterials, TEXT("head_shader_shader"), &FSkeletalMaterial::MaterialSlotName);
	UTEST_NOT_NULL("MetaHuman Character Face Material slot", MaterialSlot);
	const UMaterialInterface* FaceMaterial = MaterialSlot->MaterialInterface;
	UTEST_NOT_NULL("MetaHuman Character Face Material", FaceMaterial);

	// Check the textures objects are set correctly for the local material (no synthesized animated textures)
	TArray<EFaceTextureType> LocalTextureTypes =
	{
		EFaceTextureType::Basecolor,
		EFaceTextureType::Normal,
		EFaceTextureType::Cavity
	};
	for (EFaceTextureType TextureType : LocalTextureTypes)
	{
		UTEST_TRUE("MetaHuman Character face material texture slot", CheckSynthesizedTexturesInFaceMaterial(FaceMaterial, Character, TextureType));
	}

	return true;
}

// This test reproduces the crash from UE-289477, where RemoveObjectToEdit can be called from within TryAddObjectToEdit
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetHumanCharacterOpenAndClose, "MetaHumanCreator.Regression.OpenAndClose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetHumanCharacterOpenAndClose::RunTest(const FString& InParams)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	UTEST_NOT_NULL_EXPR(Subsystem);

	UMetaHumanCharacter* Char1 = NewObject<UMetaHumanCharacter>();
	UMetaHumanCharacter* Char2 = NewObject<UMetaHumanCharacter>();

	UTEST_NOT_NULL_EXPR(Char1);
	UTEST_NOT_NULL_EXPR(Char2); 
	
	Subsystem->InitializeMetaHumanCharacter(Char1);
	Subsystem->InitializeMetaHumanCharacter(Char2);

	UTEST_TRUE_EXPR(Subsystem->TryAddObjectToEdit(Char1));

	// Autorigging needs to run at least once in an editor session before attempting any texture service requests
	{
		// Do TDS authentication, if requested
		TUniquePtr<UE::MetaHuman::TDSUtils::FScopedExchangeCodeHandler> ScopedExchangeHandler = MakeTDSScopedExchangeCodeHandler();
		UTEST_TRUE("Exchange code succesfully generated", !ScopedExchangeHandler.IsValid() || ScopedExchangeHandler->HasExchangeCode());

		// AR may throw errors but we are not tracking them as part of this test, only need to kick off the request
		bSuppressLogErrors = true;
		Subsystem->RequestAutoRigging(Char1, FMetaHumanCharacterAutoRiggingRequestParams
			{
				.RigType = EMetaHumanRigType::JointsOnly,
				.bReportProgress = false,
				.bBlocking = true
			});
		bSuppressLogErrors = false;
	}

	Subsystem->RequestTextureSources(Char1, 
									 FMetaHumanCharacterTextureRequestParams
									 {
										 .bReportProgress = false,
										 .bBlocking = true
									 });

	// Register the removal of Char1 into TaskGraph, simulating the removal of an object while
	// another one is being added to the subsystem. It is important for this to be registered
	// in the game thread since this is a requirement for RemoveObjectToEdit
	bool bChar1Removed = false;
	AsyncTask(ENamedThreads::GameThread, [Char1, Subsystem, &bChar1Removed]
			  {
				Subsystem->RemoveObjectToEdit(Char1);
				bChar1Removed = true;
			  });

	// This will flush the tasks, which will call the registered lambda above
	UTEST_TRUE_EXPR(Subsystem->TryAddObjectToEdit(Char2));
	UTEST_TRUE_EXPR(bChar1Removed);

	Subsystem->RequestTextureSources(Char1,
									 FMetaHumanCharacterTextureRequestParams
									 {
										 .bReportProgress = false,
										 .bBlocking = true
									 });

	Subsystem->RemoveObjectToEdit(Char2);

	// If we get to this point without any errors in the log the requests were successful

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterEditorSkinPropertiesTest, "MetaHumanCreator.Character.SkinProperties", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);


bool FMetaHumanCharacterEditorSkinPropertiesTest::RunTest(const FString& InParams)
{
	// Makes sure FMetaHumanCharacterAccentRegions has the correct property names
	// Properties names should match the values in the EMetaHumanCharacterAccentRegion
	for (EMetaHumanCharacterAccentRegion AccentRegion : TEnumRange<EMetaHumanCharacterAccentRegion>())
	{
		const FString AccentRegionName = StaticEnum<EMetaHumanCharacterAccentRegion>()->GetAuthoredNameStringByValue(static_cast<int64>(AccentRegion));
		const FStructProperty* AccentRegionProperty = FindFProperty<FStructProperty>(FMetaHumanCharacterAccentRegions::StaticStruct(), *AccentRegionName);

		const FString PropertyTestName = FString::Format(TEXT("FMetaHumanCharacterAccentRegions has '{0}' property"), { AccentRegionName });
		UTEST_NOT_NULL(PropertyTestName, AccentRegionProperty);

		UTEST_SAME_PTR("Accent Region Param is of type FMetaHumanCharacterAccentRegionProperties", AccentRegionProperty->Struct.Get(), FMetaHumanCharacterAccentRegionProperties::StaticStruct());
	}

	// Makes sure the FMetaHumanCharacterAccentRegionProperties has the correct property names
	// Property names should match the values in EMetaHumanCharacterAccentRegionParameter
	for (EMetaHumanCharacterAccentRegionParameter AccentRegionParam : TEnumRange<EMetaHumanCharacterAccentRegionParameter>())
	{
		const FString AccentRegionParamName = StaticEnum<EMetaHumanCharacterAccentRegionParameter>()->GetAuthoredNameStringByValue(static_cast<int64>(AccentRegionParam));
		const FFloatProperty* AccentRegionParamProperty = FindFProperty<FFloatProperty>(FMetaHumanCharacterAccentRegionProperties::StaticStruct(), *AccentRegionParamName);

		const FString PropertyTestName = FString::Format(TEXT("FMetaHumanCharacterAccentRegionProperties has '{0}' property"), { AccentRegionParamName });
		UTEST_NOT_NULL(PropertyTestName, AccentRegionParamProperty);
	}

	// Makes sure the FMetaHumanCharacterFrecklesProperties has the correct property names
	// Property names should match the values in EMetaHumanCharacterFrecklesParameter
	for (EMetaHumanCharacterFrecklesParameter FrecklesParam : TEnumRange<EMetaHumanCharacterFrecklesParameter>())
	{
		const FString FrecklesParamName = StaticEnum<EMetaHumanCharacterFrecklesParameter>()->GetAuthoredNameStringByValue(static_cast<int64>(FrecklesParam));
		const FProperty* FrecklesParamProperty = FindFProperty<FProperty>(FMetaHumanCharacterFrecklesProperties::StaticStruct(), *FrecklesParamName);

		const FString PropertyTestName = FString::Format(TEXT("FMetaHumanCharacterFrecklesProperties has '{0}' property"), { FrecklesParamName });
		UTEST_NOT_NULL(PropertyTestName, FrecklesParamProperty);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterEditorMakeupPropertiesTest, "MetaHumanCreator.Character.Makeup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterEditorMakeupPropertiesTest::RunTest(const FString& InParams)
{
	// Foundation
	const FStructProperty* FoundationPropety = FindFProperty<FStructProperty>(FMetaHumanCharacterMakeupSettings::StaticStruct(), "Foundation");
	UTEST_NOT_NULL("FMetaHumanCharacterMakeupSettings has Foundation property", FoundationPropety);
	UTEST_SAME_PTR("Foundation makeup property is of type FMetaHumanCharacterAccentRegionProperties", FoundationPropety->Struct.Get(), FMetaHumanCharacterFoundationMakeupProperties::StaticStruct());

	bool bHasMetaData = FoundationPropety->HasMetaData("ShowOnlyInnerProperties");
	UTEST_TRUE("Foundation property has ShowOnlyInnerProperties metadata", bHasMetaData);

	//Eyes
	const FStructProperty* EyesPropety = FindFProperty<FStructProperty>(FMetaHumanCharacterMakeupSettings::StaticStruct(), "Eyes");
	UTEST_NOT_NULL("FMetaHumanCharacterMakeupSettings has Eyes property", EyesPropety);
	UTEST_SAME_PTR("Eyes makeup property is of type FMetaHumanCharacterEyeMakeupProperties", EyesPropety->Struct.Get(), FMetaHumanCharacterEyeMakeupProperties::StaticStruct());

	bHasMetaData = EyesPropety->HasMetaData("ShowOnlyInnerProperties");
	UTEST_TRUE("Eyes property has ShowOnlyInnerProperties metadata", bHasMetaData);

	for (EMetaHumanCharacterEyeMakeupType EyeType : TEnumRange<EMetaHumanCharacterEyeMakeupType>())
	{
		if (EyeType == EMetaHumanCharacterEyeMakeupType::None)
		{
			continue;
		}

		const FString EyeMaskTypeName = StaticEnum<EMetaHumanCharacterEyeMakeupType>()->GetAuthoredNameStringByValue(static_cast<int64>(EyeType));
		const FString EyeMaskTexturePath = FString::Format(TEXT("/Script/Engine.Texture2D'/" UE_PLUGIN_NAME "/Textures/ArtistDelights/EyeMakeup/T_EyeMakeup_{0}.T_EyeMakeup_{0}'"), { EyeMaskTypeName });

		UObject* EyeTextureAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *EyeMaskTexturePath);
		const FString EyeTextureExistanceTestName = FString::Format(TEXT("Texture for Eye Type '{0}' loaded succesfully."), { EyeMaskTypeName });
		
		UTEST_NOT_NULL(EyeTextureExistanceTestName, EyeTextureAsset);

		if (EyeTextureAsset)
		{
			bool bIsTexture = EyeTextureAsset->IsA(UTexture2D::StaticClass());
			const FString EyeTextureValidAssetTypeTestName = FString::Format(TEXT("Eye Type '{0}' asset is a texture."), { EyeMaskTypeName });
			UTEST_TRUE(EyeTextureValidAssetTypeTestName, bIsTexture);
		}
	}

	//Blush
	const FStructProperty* BlushPropety = FindFProperty<FStructProperty>(FMetaHumanCharacterMakeupSettings::StaticStruct(), "Blush");
	UTEST_NOT_NULL("FMetaHumanCharacterMakeupSettings has Blush property", BlushPropety);
	UTEST_SAME_PTR("Blush makeup property is of type FMetaHumanCharacterBlushMakeupProperties", BlushPropety->Struct.Get(), FMetaHumanCharacterBlushMakeupProperties::StaticStruct());

	bHasMetaData = BlushPropety->HasMetaData("ShowOnlyInnerProperties");
	UTEST_TRUE("Blush property has ShowOnlyInnerProperties metadata", bHasMetaData);

	for (EMetaHumanCharacterBlushMakeupType BlushType : TEnumRange<EMetaHumanCharacterBlushMakeupType>())
	{
		if(BlushType == EMetaHumanCharacterBlushMakeupType::None)
		{
			continue;	
		}

		const FString BlushMaskTypeName = StaticEnum<EMetaHumanCharacterBlushMakeupType>()->GetAuthoredNameStringByValue(static_cast<int64>(BlushType));
		const FString BlushMaskTexureName = FString::Format(TEXT("/Script/Engine.Texture2D'/" UE_PLUGIN_NAME "/Textures/ArtistDelights/BlushMakeup/T_BlushMakeup_{0}.T_BlushMakeup_{0}'"), { BlushMaskTypeName });

		UObject* BlushTextureAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *BlushMaskTexureName);
		const FString BlushTextureExistanceTestName = FString::Format(TEXT("Texture for Blush Type '{0}' loaded succesfully."), { BlushMaskTypeName });

		UTEST_NOT_NULL(BlushTextureExistanceTestName, BlushTextureAsset);

		if (BlushTextureAsset)
		{
			bool bIsTexture = BlushTextureAsset->IsA(UTexture2D::StaticClass());
			const FString BlushTextureValidAssetTypeTestName = FString::Format(TEXT("Blush Type '{0}' asset is a texture."), { BlushMaskTypeName });
			UTEST_TRUE(BlushTextureValidAssetTypeTestName, bIsTexture);
		}
	}

	//Lips
	const FStructProperty* LipsPropety = FindFProperty<FStructProperty>(FMetaHumanCharacterMakeupSettings::StaticStruct(), "Lips");
	UTEST_NOT_NULL("FMetaHumanCharacterMakeupSettings has Lips property", LipsPropety);
	UTEST_SAME_PTR("Lips makeup property is of type FMetaHumanCharacterLipsMakeupProperties", LipsPropety->Struct.Get(), FMetaHumanCharacterLipsMakeupProperties::StaticStruct());

	bHasMetaData = LipsPropety->HasMetaData("ShowOnlyInnerProperties");
	UTEST_TRUE("Lips property has ShowOnlyInnerProperties metadata", bHasMetaData);

	for (EMetaHumanCharacterLipsMakeupType LipsType : TEnumRange<EMetaHumanCharacterLipsMakeupType>())
	{
		if(LipsType == EMetaHumanCharacterLipsMakeupType::None)
		{
			continue;
		}

		const FString LipsMaskTypeName = StaticEnum<EMetaHumanCharacterLipsMakeupType>()->GetAuthoredNameStringByValue(static_cast<int64>(LipsType));
		const FString LipsMaskTextureName = FString::Format(TEXT("/Script/Engine.Texture2D'/" UE_PLUGIN_NAME "/Textures/ArtistDelights/LipsMakeup/T_LipsMakeup_{0}.T_LipsMakeup_{0}'"), { LipsMaskTypeName });

		UObject* LipsTextureAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *LipsMaskTextureName);
		const FString LipsTextureExistanceTestName = FString::Format(TEXT("Texture for Lips Type '{0}' loaded succesfully."), { LipsMaskTypeName });

		UTEST_NOT_NULL(LipsTextureExistanceTestName, LipsTextureAsset);

		if (LipsTextureAsset)
		{
			bool bIsTexture = LipsTextureAsset->IsA(UTexture2D::StaticClass());
			const FString LipsTextureValidAssetTypeTestName = FString::Format(TEXT("Lips Type '{0}' asset is a texture."), { LipsMaskTypeName });
			UTEST_TRUE(LipsTextureValidAssetTypeTestName, bIsTexture);
		}
	}

	// Committing MakeupProperties to Character Test
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	UTEST_TRUE_EXPR(Character->IsCharacterValid());

	ON_SCOPE_EXIT
	{
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};

	FMetaHumanCharacterMakeupSettings SettingsToApply = SynthethicDataGenerator::GenerateMakeupPropertiesData();
	
	MetaHumanCharacterSubsystem->CommitMakeupSettings(Character, SettingsToApply);

	UTEST_TRUE("Makeup Properties are changed when commiting new values to Character", SettingsToApply == Character->MakeupSettings);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterEditorViewportPropertiesTest, "MetaHumanCreator.Character.Viewport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterEditorViewportPropertiesTest::RunTest(const FString& InParams)
{
	// Levels
	const FEnumProperty* EnvironmentProperty = FindFProperty<FEnumProperty>(FMetaHumanCharacterViewportSettings::StaticStruct(), "CharacterEnvironment");
	UTEST_NOT_NULL("FMetaHumanCharacterViewportSettings has Environment property", EnvironmentProperty);
	UTEST_SAME_PTR("Environment property is of type EMetaHumanCharacterEnvironment", EnvironmentProperty->GetEnum(), StaticEnum<EMetaHumanCharacterEnvironment>());

	for (EMetaHumanCharacterEnvironment Environment : TEnumRange<EMetaHumanCharacterEnvironment>())
	{
		const FString EnvironmentTypeName = StaticEnum<EMetaHumanCharacterEnvironment>()->GetAuthoredNameStringByValue(static_cast<int64>(Environment));
		const FString EnvironmentLevelName = FString::Format(TEXT("/Script/Engine.Level'/" UE_PLUGIN_NAME "/LightingEnvironments/{0}.{0}'"), { EnvironmentTypeName });

		UObject* EnvironmentLevelAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *EnvironmentLevelName);
		const FString EnvironmentExistenceTestName = FString::Format(TEXT("Environment '{0}' loaded successfully."), { EnvironmentTypeName });

		UTEST_NOT_NULL(EnvironmentExistenceTestName, EnvironmentLevelAsset);

		if (EnvironmentLevelAsset)
		{
			bool bIsLevel = EnvironmentLevelAsset->IsA(UWorld::StaticClass());
			const FString EnvironmentValidAssetTypeTestName = FString::Format(TEXT("Environment '{0}' asset is a world."), { EnvironmentTypeName });
			UTEST_TRUE(EnvironmentValidAssetTypeTestName, bIsLevel);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterEditorEyesPropertiesTest, "MetaHumanCreator.Character.Eyes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterEditorEyesPropertiesTest::RunTest(const FString& InParams)
{
	// Apply/Commit Eye settings i CommitSkinSettings for eyes sclera
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	UTEST_TRUE_EXPR(Character->IsCharacterValid());

	ON_SCOPE_EXIT
	{
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};

	FMetaHumanCharacterEyesSettings EyeSettings;
	EyeSettings.EyeLeft = SynthethicDataGenerator::GenerateEyesSettings();
	EyeSettings.EyeRight = SynthethicDataGenerator::GenerateEyesSettings();

	MetaHumanCharacterSubsystem->CommitEyesSettings(Character, EyeSettings);

	UTEST_TRUE("Eyes are changed when commiting new values to Character", EyeSettings == Character->EyesSettings);

	// Sclera Test
	FMetaHumanCharacterEyesSettings DefaultEyesSettings; 
	FMetaHumanCharacterSkinSettings SkinSettingsWithSkinUChanged;

	MetaHumanCharacterSubsystem->CommitEyesSettings(Character, DefaultEyesSettings);

	UTEST_TRUE("Eyes are changed when commiting default values to Character", DefaultEyesSettings == Character->EyesSettings);

	float SkinUValue = 0.5f;
	while(SkinUValue == 0.5f)
	{
		SkinUValue = FMath::RandRange(0.f, 1.f);
	}

	SkinSettingsWithSkinUChanged.Skin.U = SkinUValue;
	Character->EyesSettings.EyeLeft.Sclera.bUseCustomTint = true;
	Character->EyesSettings.EyeRight.Sclera.bUseCustomTint = true;

	MetaHumanCharacterSubsystem->CommitSkinSettings(Character, SkinSettingsWithSkinUChanged);

	UTEST_TRUE("Eyes sclera settings gets changed after applying different skin tone", Character->EyesSettings != DefaultEyesSettings);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterEditorTeethEyelashesPropertiesTest, "MetaHumanCreator.Character.Teeth&Eyelashes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterEditorTeethEyelashesPropertiesTest::RunTest(const FString& InParams)
{
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	UTEST_TRUE_EXPR(Character->IsCharacterValid());

	ON_SCOPE_EXIT
	{
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};

	FMetaHumanCharacterHeadModelSettings SettingsToApply;
	// Do multiple iterations?
	do
	{
		SettingsToApply.Eyelashes = SynthethicDataGenerator::GenerateEyelashesPropertiesData();
		SettingsToApply.Teeth = SynthethicDataGenerator::GenerateTeethPropertiesData();
	} while(SettingsToApply.Eyelashes == Character->HeadModelSettings.Eyelashes || SettingsToApply.Teeth == Character->HeadModelSettings.Teeth);

	MetaHumanCharacterSubsystem->CommitHeadModelSettings(Character, SettingsToApply);

	UTEST_TRUE("Eyelashes and teeth are changed when commiting new values to Character", SettingsToApply.Eyelashes == Character->HeadModelSettings.Eyelashes && SettingsToApply.Teeth == Character->HeadModelSettings.Teeth);
	
	return true;
}

// Ensures requesting mixed resolutions works as expected from MH-16562
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterRequestTexturesMixedResolutions, "MetaHumanCreator.TextureSynthesis.MixedResolutions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterRequestTexturesMixedResolutions::RunTest(const FString& InParams)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	UTEST_NOT_NULL_EXPR(Subsystem);

	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>();

	UTEST_NOT_NULL_EXPR(Character);

	Subsystem->InitializeMetaHumanCharacter(Character);

	// Request lower resolution animated maps compared to base color
	Character->SkinSettings.DesiredTextureSourcesResolutions.FaceAlbedo = ERequestTextureResolution::Res4k;
	Character->SkinSettings.DesiredTextureSourcesResolutions.FaceAnimatedMaps = ERequestTextureResolution::Res2k;
	Character->SkinSettings.DesiredTextureSourcesResolutions.FaceNormal = ERequestTextureResolution::Res8k;

	UTEST_TRUE_EXPR(Subsystem->TryAddObjectToEdit(Character));

	// Do TDS authentication, if requested
	TUniquePtr<UE::MetaHuman::TDSUtils::FScopedExchangeCodeHandler> ScopedExchangeHandler = MakeTDSScopedExchangeCodeHandler();
	UTEST_TRUE("Exchange code succesfully generated", !ScopedExchangeHandler.IsValid() || ScopedExchangeHandler->HasExchangeCode());

	Subsystem->RequestTextureSources(Character,
									 FMetaHumanCharacterTextureRequestParams
									 {
										.bReportProgress = false,
										.bBlocking = true,
									 });

	auto IsSameResolution = [Character](EFaceTextureType TextureType, ERequestTextureResolution Target)
		{
			const FInt32Point Resolution = Character->GetSynthesizedFaceTexturesResolution(TextureType);
			return Resolution.X == (int32) Target && Resolution.Y == (int32) Target;
		};

	UTEST_TRUE("Face Abedo is 4k", IsSameResolution(EFaceTextureType::Basecolor, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Animated CM1 is 2k", IsSameResolution(EFaceTextureType::Basecolor_Animated_CM1, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Animated CM2 is 2k", IsSameResolution(EFaceTextureType::Basecolor_Animated_CM2, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Animated CM2 is 2k", IsSameResolution(EFaceTextureType::Basecolor_Animated_CM2, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Normal is 8k", IsSameResolution(EFaceTextureType::Normal, ERequestTextureResolution::Res8k));
	UTEST_TRUE("Face Animated WM1 is 2k", IsSameResolution(EFaceTextureType::Normal_Animated_WM1, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Animated WM2 is 2k", IsSameResolution(EFaceTextureType::Normal_Animated_WM2, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Animated WM3 is 2k", IsSameResolution(EFaceTextureType::Normal_Animated_WM3, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Cavity is 2k", IsSameResolution(EFaceTextureType::Cavity, ERequestTextureResolution::Res2k));

	// Request higher resolution animated maps compared to base color
	Character->SkinSettings.DesiredTextureSourcesResolutions.FaceAlbedo = ERequestTextureResolution::Res2k;
	Character->SkinSettings.DesiredTextureSourcesResolutions.FaceAnimatedMaps = ERequestTextureResolution::Res4k;
	Character->SkinSettings.DesiredTextureSourcesResolutions.FaceNormal = ERequestTextureResolution::Res2k;

	Subsystem->RequestTextureSources(Character,
									 FMetaHumanCharacterTextureRequestParams
									 {
										.bReportProgress = false,
										.bBlocking = true,
									 });

	UTEST_TRUE("Face Abedo is 2k", IsSameResolution(EFaceTextureType::Basecolor, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Animated CM1 is 4k", IsSameResolution(EFaceTextureType::Basecolor_Animated_CM1, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Animated CM2 is 4k", IsSameResolution(EFaceTextureType::Basecolor_Animated_CM2, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Animated CM2 is 4k", IsSameResolution(EFaceTextureType::Basecolor_Animated_CM2, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Normal is 2k", IsSameResolution(EFaceTextureType::Normal, ERequestTextureResolution::Res2k));
	UTEST_TRUE("Face Animated WM1 is 4k", IsSameResolution(EFaceTextureType::Normal_Animated_WM1, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Animated WM2 is 4k", IsSameResolution(EFaceTextureType::Normal_Animated_WM2, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Animated WM3 is 4k", IsSameResolution(EFaceTextureType::Normal_Animated_WM3, ERequestTextureResolution::Res4k));
	UTEST_TRUE("Face Cavity is 2k", IsSameResolution(EFaceTextureType::Cavity, ERequestTextureResolution::Res2k));

	Subsystem->RemoveObjectToEdit(Character);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterSynthesizeAndUpdateTexturesTest, "MetaHumanCreator.TextureSynthesis.SynthesizeAndUpdate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterSynthesizeAndUpdateTexturesTest::RunTest(const FString& InParams)
{
	// Test Texture Synthesis helper

	// Initialize the synthesizer
	FMetaHumanFaceTextureSynthesizer FaceTextureSynthesizer;
	FMetaHumanCharacterTextureSynthesis::InitFaceTextureSynthesizer(FaceTextureSynthesizer);
	UTEST_VALID("Face Texture Synthesizer is valid", FaceTextureSynthesizer);

	// Create the Texture objects and Images
	TMap<EFaceTextureType, FMetaHumanCharacterTextureInfo> SynthesizedTexturesInfo;
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>> SynthesizedFaceTextures;
	TMap<EFaceTextureType, FImage> CachedSynthesizedImages;
	FMetaHumanCharacterTextureSynthesis::InitSynthesizedFaceData(FaceTextureSynthesizer,
																 SynthesizedTexturesInfo,
																 SynthesizedFaceTextures,
																 CachedSynthesizedImages);

	// Do some sanity checks to the created data
	UTEST_EQUAL("Number of synthesized Textures", SynthesizedFaceTextures.Num(), static_cast<int32>(EFaceTextureType::Count));

	for (const TPair<EFaceTextureType, TObjectPtr<UTexture2D>>& FaceTexturePair : SynthesizedFaceTextures)
	{
		UTEST_NOT_NULL("Synthesized face texture not null", FaceTexturePair.Value.Get());
	}
	for (TPair<EFaceTextureType, FImage> Kvp : CachedSynthesizedImages)
	{
		const FImage& CachedImage = Kvp.Value;
		UTEST_EQUAL("Cached image size X", CachedImage.SizeX, FaceTextureSynthesizer.GetTextureSizeX());
		UTEST_EQUAL("Cached image size Y", CachedImage.SizeY, FaceTextureSynthesizer.GetTextureSizeY());
		UTEST_EQUAL("Cached image format", CachedImage.Format, FaceTextureSynthesizer.GetTextureFormat());
		UTEST_EQUAL("Cached image color space", CachedImage.GammaSpace, FaceTextureSynthesizer.GetTextureColorSpace());
	}

	FMetaHumanCharacterSkinProperties SkinProperties = {};

	// Test and time synthesize on single thread
	bool bResult = false;
	{
		FScopeLogTime LogTimePtr(TEXT("FMetaHumanCharacterTextureSynthesis::SynthesizeFaceTextures"), nullptr, FScopeLogTime::ScopeLog_Milliseconds);
		bResult = FMetaHumanCharacterTextureSynthesis::SynthesizeFaceTextures(SkinProperties, FaceTextureSynthesizer, CachedSynthesizedImages);
	}
	UTEST_TRUE("Synthesize and update textures result", bResult);

	// Test and time update only
	{
		FScopeLogTime LogTimePtr(TEXT("FMetaHumanCharacterTextureSynthesis::UpdateFaceTextures"), nullptr, FScopeLogTime::ScopeLog_Milliseconds);
		bResult = FMetaHumanCharacterTextureSynthesis::UpdateFaceTextures(CachedSynthesizedImages, SynthesizedFaceTextures);
	}
	UTEST_TRUE("Synthesize textures async", bResult);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanTextureSynthesisServiceTests, "MetaHumanCreator.Service.TexturesRequestService", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanTextureSynthesisServiceTests::RunTest(const FString& InParams)
{
	using namespace UE::MetaHuman;
	constexpr int32 TestResolution = 2048;
	FFaceTextureRequestCreateParams FaceTextureRequestCreateParams;
	FaceTextureRequestCreateParams.HighFrequency = 1;
	
	struct FLocalTestState
	{
		FLocalTestState()
			: bDone(false)
			, ResultCode(EMetaHumanServiceRequestResult::Ok)
		{}

		bool bDone;
		EMetaHumanServiceRequestResult ResultCode;
	};

	TSharedRef<FLocalTestState> LocalState = MakeShared<FLocalTestState>();
	TSharedRef<FFaceTextureSynthesisServiceRequest> TextureSynthesisServiceRequest = FFaceTextureSynthesisServiceRequest::CreateRequest(FaceTextureRequestCreateParams);
	TextureSynthesisServiceRequest->FaceTextureSynthesisRequestCompleteDelegate.BindLambda([LocalState](TSharedPtr<FFaceHighFrequencyData> HighFrequencyData)
		{
			if (LocalState->bDone)
			{
				// we might still be invoked even if an error has occurred so for this test we just do an early out
				return;
			}

			int32 TotalTypeSum = ((static_cast<int32>(EFaceTextureType::Count) + 1)*static_cast<int32>(EFaceTextureType::Count)/2);
			for(EFaceTextureType Type : TEnumRange<EFaceTextureType>())
			{
				TotalTypeSum -= (static_cast<int32>(Type) + 1);
				FImage TextureImage;
				TConstArrayView<uint8> PngData = (*HighFrequencyData)[Type];
				if (PngData.Num() == 0)
				{
					continue;
				}
				verify(FImageUtils::DecompressImage(PngData.GetData(), PngData.Num(), TextureImage));
				UTexture2D* Texture = FImageUtils::CreateTexture2DFromImage(TextureImage);
				if (Texture->GetSizeX() != TestResolution || Texture->GetSizeY() != TestResolution)
				{
					// in this test we just report a server error
					// in real code this would need to do something more intelligent
					TotalTypeSum = 1;
					break;
				}
			}
			LocalState->bDone = true;
			LocalState->ResultCode = TotalTypeSum == 0 ? EMetaHumanServiceRequestResult::Ok : EMetaHumanServiceRequestResult::ServerError;
		}
	);

	TextureSynthesisServiceRequest->OnMetaHumanServiceRequestFailedDelegate.BindLambda([LocalState](EMetaHumanServiceRequestResult Result)
		{
			LocalState->bDone = true;
			LocalState->ResultCode = Result;
		}
	);

	// start the request
	const TArray<FFaceTextureRequestParams, TInlineAllocator<static_cast<int32>(EFaceTextureType::Count)>> TextureTypesToRequest =
	{
		{ EFaceTextureType::Basecolor, TestResolution },
		{ EFaceTextureType::Basecolor_Animated_CM1, TestResolution },
		{ EFaceTextureType::Basecolor_Animated_CM2, TestResolution },
		{ EFaceTextureType::Basecolor_Animated_CM3, TestResolution },
		{ EFaceTextureType::Normal, TestResolution },
		{ EFaceTextureType::Normal_Animated_WM1, TestResolution },
		{ EFaceTextureType::Normal_Animated_WM2, TestResolution },
		{ EFaceTextureType::Normal_Animated_WM3, TestResolution },
		{ EFaceTextureType::Cavity, TestResolution },
	};

	{
		// Do TDS authentication, if requested
		TUniquePtr<UE::MetaHuman::TDSUtils::FScopedExchangeCodeHandler> ScopedExchangeHandler = MakeTDSScopedExchangeCodeHandler();
		UTEST_TRUE("Exchange code succesfully generated", !ScopedExchangeHandler.IsValid() || ScopedExchangeHandler->HasExchangeCode());

		TextureSynthesisServiceRequest->RequestTexturesAsync(MakeConstArrayView<FFaceTextureRequestParams>(TextureTypesToRequest.GetData(), TextureTypesToRequest.Num()));

		while (!LocalState->bDone)
		{
			const float DeltaTime = 0.1f;
			FHttpModule::Get().GetHttpManager().Tick(DeltaTime);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FTSTicker::GetCoreTicker().Tick(DeltaTime);
			FPlatformProcess::Sleep(0.05f);
		}
	}

	UTEST_TRUE("Didn't get all the textures, missing, invalid, or dupes", LocalState->ResultCode==EMetaHumanServiceRequestResult::Ok);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterAutoRigServiceTest, "MetaHumanCreator.Service.AutoRigServiceTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterAutoRigServiceTest::RunTest(const FString& InParams)
{
	// EOS SDK reports an expected error during authentication when the user account doesn't have the
	// 'friends_list' scope - the test does not require it, but the SDK still queries the block list.
	AddExpectedError(TEXT("insufficient_scopes"), EAutomationExpectedMessageFlags::Contains, 0);

	// Create a transient world where we can spawn an actor
	FScopedTestWorld TestWorld;
	UTEST_NOT_NULL_EXPR(TestWorld.World);

	// Get archetype and validate
	// Call this first as it forces garbage collection
	USkeletalMesh* FaceArchetypeMesh = UMetaHumanCharacterEditorSubsystem::GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType::MetaHuman);
	UTEST_NOT_NULL_EXPR(FaceArchetypeMesh);

	UDNA* DNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(FaceArchetypeMesh);
	UTEST_NOT_NULL_EXPR(DNAAsset);
	TSharedPtr<IDNAReader> DNAReader = DNAAsset->GetDNAReader();
	UTEST_VALID("Archetype DNA is valid", DNAReader);
	// Map vertices by creating map from skeletal mesh user asset.
	TSharedPtr<FDNAToSkelMeshMap> FaceArchetypeDnaToSkelMeshMap = TSharedPtr<FDNAToSkelMeshMap>(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(FaceArchetypeMesh));
	UTEST_VALID("DNA to SkeletalMesh map is valid", FaceArchetypeDnaToSkelMeshMap);
	
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());
	UTEST_TRUE("Initial FaceStateData is empty", Character->GetFaceStateData().GetSize() == 0);

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	UTEST_TRUE_EXPR(Character->IsCharacterValid());

	ON_SCOPE_EXIT
	{
		// Ensure the subsystem cleans up any resources in the case
		// In particular for handling lifetime issues with the titan texture synthesis static array of allocations
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};

	// Spawn the editor actor
	FText FailureReason;
	TSubclassOf<AActor> EditorActorClass;
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryGetMetaHumanCharacterEditorActorClass(Character, EditorActorClass, FailureReason));
	UTEST_NOT_NULL_EXPR(EditorActorClass.Get());

	TScriptInterface<IMetaHumanCharacterEditorActorInterface> CharacterActor = MetaHumanCharacterSubsystem->CreateMetaHumanCharacterEditorActor(Character, TestWorld.World);
	UTEST_NOT_NULL_EXPR(CharacterActor.GetObject());
	UTEST_NOT_SAME_PTR("MetaHuman Character Actor Face Skeletal Mesh", CharacterActor->GetFaceComponent()->GetSkeletalMeshAsset(), FaceArchetypeMesh);
	
	{
		// Do TDS authentication, if requested
		TUniquePtr<UE::MetaHuman::TDSUtils::FScopedExchangeCodeHandler> ScopedExchangeHandler = MakeTDSScopedExchangeCodeHandler();
		UTEST_TRUE("Exchange code succesfully generated", !ScopedExchangeHandler.IsValid() || ScopedExchangeHandler->HasExchangeCode());

		// Reset the character to set a face state and autorig with blend shapes
		MetaHumanCharacterSubsystem->ResetCharacterFace(Character);
		MetaHumanCharacterSubsystem->RequestAutoRigging(Character, FMetaHumanCharacterAutoRiggingRequestParams
														{
															.RigType = EMetaHumanRigType::JointsAndBlendShapes,
															.bReportProgress = false,
															.bBlocking = true
														});
	}

	// Fetch updated DNA.
	DNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(CharacterActor->GetFaceComponent()->GetSkeletalMeshAsset());
	UTEST_NOT_NULL_EXPR(DNAAsset);
	TSharedPtr<IDNAReader> UpdatedDNAReader = DNAAsset->GetDNAReader();
	UTEST_VALID("Updated DNA is valid", UpdatedDNAReader);

	// Map joints explicitly.
	TSharedRef<FDNAToSkelMeshMap> FaceDnaToSkelMeshMap = MakeShared<FDNAToSkelMeshMap>(*MetaHumanCharacterSubsystem->GetFaceDnaToSkelMeshMap(Character));
	FaceDnaToSkelMeshMap->MapJoints(DNAReader.Get());
	UTEST_FALSE("MetaHuman Character FaceStateData has data", Character->GetFaceStateData().GetSize() == 0);

	UTEST_EQUAL("DNA number of LODs", DNAReader->GetLODCount(), UpdatedDNAReader->GetLODCount());
	// Make sure DNAs have the same mesh count and same vertex count per mesh.
	const int32 MeshCount = DNAReader->GetMeshCount();
	UTEST_EQUAL("DNA mesh count", MeshCount, UpdatedDNAReader->GetMeshCount());
	for (int32 MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
	{
		UTEST_EQUAL(FString::Printf(TEXT("DNA vertex position count for mesh %d"), MeshIndex), DNAReader->GetVertexPositionCount(MeshIndex), UpdatedDNAReader->GetVertexPositionCount(MeshIndex));
	}
	// Make sure DNAs have the same joint count and same joints name in the same order.
	const int32 JointCount = DNAReader->GetJointCount();
	UTEST_EQUAL("DNA joint count", JointCount, UpdatedDNAReader->GetJointCount());
	for (int32 JointIndex = 0; JointIndex < JointCount; JointIndex++)
	{
		UTEST_EQUAL("DNA joint name", DNAReader->GetJointName(JointIndex), UpdatedDNAReader->GetJointName(JointIndex));
	}

	// Test bind pose hierarchy.
	const USkeletalMesh* UpdateSkeletalMesh = CharacterActor->GetFaceComponent()->GetSkeletalMeshAsset();
	const FReferenceSkeleton* RefSkeleton = &UpdateSkeletalMesh->GetRefSkeleton();
	for (int32 JointIndex = 0; JointIndex < UpdatedDNAReader->GetJointCount(); JointIndex++)
	{
		const FString BoneNameStr = UpdatedDNAReader->GetJointName(JointIndex);
		const FName BoneName = FName{ BoneNameStr };
		const int32 BoneIndex = RefSkeleton->FindBoneIndex(BoneName);
		const int32 ExpectedJointIndex = FaceDnaToSkelMeshMap->GetUEBoneIndex(JointIndex);

		UTEST_TRUE("DNA joint not found in Skeleton hierarchy", BoneIndex != INDEX_NONE);
		UTEST_EQUAL(FString::Printf(TEXT("DNA joint index %d mismatch"), ExpectedJointIndex), ExpectedJointIndex, BoneIndex);
	}

	// Test if vertex positions match.
	FSkeletalMeshModel* ImportedModel = UpdateSkeletalMesh->GetImportedModel();
	UTEST_EQUAL("Skeletal mesh number of LODs", ImportedModel->LODModels.Num(), UpdatedDNAReader->GetLODCount());
	for (int32 LODIndex = 0; LODIndex < UpdatedDNAReader->GetLODCount(); LODIndex++)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		for (int32 MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
		{
			// Only check vertices with a valid mapping
			// TODO: re-assess whether this test can be strengthened to cover blend shape meshes
			if (!FaceDnaToSkelMeshMap->ImportDNAVtxToUEVtxIndex[LODIndex][MeshIndex].IsEmpty())
			{
				const int32 VertexCount = UpdatedDNAReader->GetVertexPositionCount(MeshIndex);
				// Build position-to-normal index mapping for this mesh (mirrors UpdateBaseMesh).
				TArray<int32> DNAPositionToNormalIndex;
				DNAPositionToNormalIndex.Init(INDEX_NONE, VertexCount);
				const int32 NumLayouts = static_cast<int32>(UpdatedDNAReader->GetVertexLayoutCount(MeshIndex));
				for (int32 LayoutIndex = 0; LayoutIndex < NumLayouts; ++LayoutIndex)
				{
					const FVertexLayout Layout = UpdatedDNAReader->GetVertexLayout(MeshIndex, LayoutIndex);
					if (Layout.Position >= 0 && Layout.Position < VertexCount && Layout.Normal >= 0)
					{
						DNAPositionToNormalIndex[Layout.Position] = Layout.Normal;
					}
				}
				for (int32 DNAVertexIndex = 0; DNAVertexIndex < VertexCount; DNAVertexIndex++)
				{
					int32 VertexIndex = FaceDnaToSkelMeshMap->ImportDNAVtxToUEVtxIndex[LODIndex][MeshIndex][DNAVertexIndex];
					TArray<FSoftSkinVertex> Vertices;
					LODModel.GetVertices(Vertices);
					UTEST_TRUE("Skeletal mesh vertex index valid", Vertices.IsValidIndex(VertexIndex));
					const FVector UpdatedPosition = UpdatedDNAReader->GetVertexPosition(MeshIndex, DNAVertexIndex);
					UTEST_EQUAL_TOLERANCE("Skeletal mesh vertex correct position", FVector{ Vertices[VertexIndex].Position }, UpdatedPosition, UE_KINDA_SMALL_NUMBER);
					if (DNAPositionToNormalIndex.IsValidIndex(DNAVertexIndex) && DNAPositionToNormalIndex[DNAVertexIndex] != INDEX_NONE)
					{
						const int32 NormalIndex = DNAPositionToNormalIndex[DNAVertexIndex];
						const FVector UpdatedNormal = UpdatedDNAReader->GetVertexNormal(MeshIndex, NormalIndex);
						UTEST_EQUAL_TOLERANCE("Skeletal mesh vertex correct normal", FVector{ Vertices[VertexIndex].TangentZ }, UpdatedNormal, UE_KINDA_SMALL_NUMBER);
					}
				}
			}
		}
	}

	// From UE::MetaHuman::FaceToDnaMeshIndexMapping from MetaHumanCharacterEditorSubsystem.cpp
	const TArray<int32> ConformedMeshIndices = { 0, 1, 3, 4 };
	constexpr float Tolerance = 0.001f;

	// Check that after AR, the face state head meshes match the returned DNA
	{
		TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState = MetaHumanCharacterSubsystem->GetFaceState(Character);
		const FMetaHumanRigEvaluatedState Evaluated = FaceState->Evaluate();

		UTEST_TRUE(
			"Returned/aligned DNA matches face state on conformed meshes after autorig (MH-17828)",
			FMetaHumanCharacterSkelMeshUtils::CompareDnaToStateVertices(
				UpdatedDNAReader,
				Evaluated.Vertices,
				FaceState,
				Tolerance,
				ConformedMeshIndices));
	}

	// Verify state and DNA match after asset reload
	{
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);

		// The persistent buffers must still be there — that's the asset on disk.
		UTEST_FALSE("FaceStateData persists across edit-mode teardown", Character->GetFaceStateData().GetSize() == 0);
		UTEST_FALSE("FaceDNABuffer persists across edit-mode teardown", Character->GetFaceDNABuffer().IsEmpty());
		UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
		UTEST_TRUE_EXPR(Character->IsCharacterValid());

		CharacterActor = MetaHumanCharacterSubsystem->CreateMetaHumanCharacterEditorActor(Character, TestWorld.World);
		UTEST_NOT_NULL_EXPR(CharacterActor.GetObject());

		UDNA* ReloadedDNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(CharacterActor->GetFaceComponent()->GetSkeletalMeshAsset());
		UTEST_NOT_NULL_EXPR(ReloadedDNAAsset);
		TSharedPtr<IDNAReader> ReloadedDNAReader = ReloadedDNAAsset->GetDNAReader();
		UTEST_VALID("Reloaded DNA is valid", ReloadedDNAReader);

		TSharedRef<const FMetaHumanCharacterIdentity::FState> ReloadedFaceState = MetaHumanCharacterSubsystem->GetFaceState(Character);
		const FMetaHumanRigEvaluatedState ReloadedEvaluated = ReloadedFaceState->Evaluate();

		UTEST_TRUE(
			"Persisted DNA matches face state on conformed meshes after edit-mode reload (MH-17828)",
			FMetaHumanCharacterSkelMeshUtils::CompareDnaToStateVertices(
				ReloadedDNAReader,
				ReloadedEvaluated.Vertices,
				ReloadedFaceState,
				Tolerance,
				ConformedMeshIndices));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterUpdateBodyRigTest, "MetaHumanCreator.Character.UpdateBodyRig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterUpdateBodyRigTest::RunTest(const FString& InParams)
{
	// Create a transient world where we can spawn an actor
	FScopedTestWorld TestWorld;
	UTEST_NOT_NULL_EXPR(TestWorld.World);

	// Check body archetype
	USkeletalMesh* BodyArchetypeMesh = UMetaHumanCharacterEditorSubsystem::GetBodyArchetypeMesh(EMetaHumanCharacterTemplateType::MetaHuman);
	UTEST_NOT_NULL_EXPR(BodyArchetypeMesh);
	
	UDNA* DNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(BodyArchetypeMesh);
	UTEST_NOT_NULL_EXPR(DNAAsset);
	TSharedPtr<IDNAReader> DNAReader = DNAAsset->GetDNAReader();
	UTEST_VALID("Archetype DNA is valid", DNAReader);
	// Map vertices by creating map from skeletal mesh user asset.
	TSharedPtr<FDNAToSkelMeshMap> BodyArchetypeDnaToSkelMeshMap = TSharedPtr<FDNAToSkelMeshMap>(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(BodyArchetypeMesh));
	UTEST_VALID("DNA to SkeletalMesh map is valid", BodyArchetypeDnaToSkelMeshMap);

	// Create and check character
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());
	UTEST_TRUE("Initial BodyStateData is empty", Character->GetBodyStateData().GetSize() == 0);
	UTEST_TRUE("Initial BodyDNABuffer is empty", Character->GetBodyDNABuffer().Num() == 0);

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	UTEST_TRUE_EXPR(Character->IsCharacterValid());

	ON_SCOPE_EXIT
	{
		// Ensure the subsystem cleans up any resources in the case
		// In particular for handling lifetime issues with the titan texture synthesis static array of allocations
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};

	// Spawn the editor actor

	FText FailureReason;
	TSubclassOf<AActor> EditorActorClass;
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryGetMetaHumanCharacterEditorActorClass(Character, EditorActorClass, FailureReason));
	UTEST_NOT_NULL_EXPR(EditorActorClass.Get());

	TScriptInterface<IMetaHumanCharacterEditorActorInterface> CharacterActor = MetaHumanCharacterSubsystem->CreateMetaHumanCharacterEditorActor(Character, TestWorld.World);
	UTEST_NOT_NULL_EXPR(CharacterActor.GetObject());
	UTEST_NOT_SAME_PTR("MetaHuman Character Actor Body Skeletal Mesh", CharacterActor->GetBodyComponent()->GetSkeletalMeshAsset(), BodyArchetypeMesh);

	// Apply constraint, and check updated dna against archetype
	FMetaHumanCharacterBodyConstraint BodyConstraint;
	BodyConstraint.bIsActive = true;
	BodyConstraint.Name = "height";
	BodyConstraint.TargetMeasurement = 170;
	MetaHumanCharacterSubsystem->SetBodyConstraints(Character, {BodyConstraint});

	// Fully apply body state to update dna
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState = MetaHumanCharacterSubsystem->GetBodyState(Character);
	MetaHumanCharacterSubsystem->ApplyBodyState(Character, BodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);

	// Check Height has been applied to model
	float ActualHeight = BodyState->GetMeasurement(0);
	UTEST_EQUAL_TOLERANCE("Body constraint applied", ActualHeight, 170, 1.0f);

	// Fetch updated DNA.
	DNAAsset = USkelMeshDNAUtils::GetMeshDNAAsset(CharacterActor->GetBodyComponent()->GetSkeletalMeshAsset());
	UTEST_NOT_NULL_EXPR(DNAAsset);
	TSharedPtr<IDNAReader> UpdatedDNAReader = DNAAsset->GetDNAReader();
	UTEST_VALID("Updated DNA is valid", UpdatedDNAReader);

	UTEST_EQUAL("DNA number of LODs", DNAReader->GetLODCount(), UpdatedDNAReader->GetLODCount());
	// Make sure DNAs have the same mesh count and same vertex count per mesh.
	const int32 MeshCount = DNAReader->GetMeshCount();
	UTEST_EQUAL("DNA mesh count", MeshCount, UpdatedDNAReader->GetMeshCount());
	for (int32 MeshIndex = 0; MeshIndex < MeshCount; MeshIndex++)
	{
		UTEST_EQUAL(FString::Printf(TEXT("DNA vertex position count for mesh %d"), MeshIndex), DNAReader->GetVertexPositionCount(MeshIndex), UpdatedDNAReader->GetVertexPositionCount(MeshIndex));
	}
	// Make sure DNAs have the same joint count and same joints name in the same order.
	const int32 JointCount = DNAReader->GetJointCount();
	UTEST_EQUAL("DNA joint count", JointCount, UpdatedDNAReader->GetJointCount());
	for (int32 JointIndex = 0; JointIndex < JointCount; JointIndex++)
	{
		UTEST_EQUAL("DNA joint name", DNAReader->GetJointName(JointIndex), UpdatedDNAReader->GetJointName(JointIndex));
	}

	// Check body dna commited to character
	UTEST_FALSE("Body DNA not committed", Character->HasBodyDNA());
	MetaHumanCharacterSubsystem->CommitBodyDNA(Character, UpdatedDNAReader.ToSharedRef());
	UTEST_TRUE("Body DNA committed", Character->HasBodyDNA());
	
	// Test conforming to archetype
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;
	TargetMeshKey.BodyMesh = TSoftObjectPtr<UObject>(BodyArchetypeMesh);
	FMetaHumanCharacterTargetKeyPoints KeyPoints;
	TMap<FName, EKeyPointType> KeyPointTypes;
	MetaHumanCharacterSubsystem->CommitTargetMeshKeypoints(Character, TargetMeshKey, KeyPoints, KeyPointTypes);
	
	TArray<FVector3f> BodyVertices;
	TArray<int32> BodyVertexIndices;
	{
		constexpr int32 MeshId = 0;
		const int32 NumVertices = DNAReader->GetVertexPositionCount(MeshId);					
		BodyVertices.SetNumUninitialized(NumVertices);
		for (int32 i = 0; i < NumVertices; ++i)
		{
			BodyVertices[i] = FVector3f(DNAReader->GetVertexPosition(MeshId, i));
		}
		const int32 NumFaces = DNAReader->GetFaceCount(MeshId); 					
		for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
		{
			TArrayView<const uint32> LayoutIndices = DNAReader->GetFaceVertexLayoutIndices(MeshId, FaceIndex);
			for (const uint32 LayoutIndex : LayoutIndices)
			{
				int32 PositionIndex = DNAReader->GetVertexLayout(MeshId, LayoutIndex).Position;
				BodyVertexIndices.Add(PositionIndex);
			}
		}
	}
		
	FConformTargetParams TargetParams;
	TargetParams.ConformTargetMesh.BodyVertices = BodyVertices;
	TargetParams.ConformTargetMesh.BodyVertexIndices = BodyVertexIndices;
	TargetParams.BodyConformSolveSettings.Iterations = 1;
	TargetParams.BodyConformSolveSettings.FaceIterations = 1;

	bool bIterationCalled = false;
	bool bCompleteCalled = false;
	MetaHumanCharacterSubsystem->OnAsyncMeshConformIteration(Character).AddLambda([&] (const FMetaHumanRigEvaluatedState& InBodyVerticesAndNormals, const FMetaHumanRigEvaluatedState& InFaceVerticesAndNormals)
	{
		bIterationCalled = true;
    });

	MetaHumanCharacterSubsystem->OnAsyncMeshConformCompleted(Character).AddLambda([&] (bool bSuccess, bool bWasCancelled)
	{
		bCompleteCalled = true;
	});
		
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->ConformTargetMeshesAsync(Character, TargetMeshKey, TargetParams, true));
	UTEST_TRUE("Conform iteration called", bIterationCalled);
	UTEST_TRUE("Conform completion called", bCompleteCalled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanCharacterDuplicationTest, "MetaHumanCreator.Character.AssetDuplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterDuplicationTest::RunTest(const FString& InParams)
{
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());
	UTEST_TRUE("Initial FaceStateData is empty", Character->GetFaceStateData().GetSize() == 0);
	UTEST_TRUE("Initial BodyStateData is empty", Character->GetBodyStateData().GetSize() == 0);

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(Character->IsCharacterValid());
	UTEST_FALSE("MetaHuman Character Face State is valid", Character->GetFaceStateData().GetSize() == 0);
	UTEST_FALSE("MetaHuman Character Body State is valid", Character->GetBodyStateData().GetSize() == 0);

	UTEST_TRUE("Character is added for editing", MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));

	MetaHumanCharacterSubsystem->CommitFaceState(Character, MetaHumanCharacterSubsystem->GetFaceState(Character));

	FMetaHumanRigEvaluatedState FaceState = MetaHumanCharacterSubsystem->GetFaceState(Character)->Evaluate();

	UMetaHumanCharacter* DuplicateCharacter = CastChecked<UMetaHumanCharacter>(StaticDuplicateObject(Character, Character->GetOuter(), FName(*FString::Printf(TEXT("%s_Duplicate"), *Character->GetName()))));
	UTEST_NOT_NULL_EXPR(DuplicateCharacter);
	UTEST_TRUE_EXPR(DuplicateCharacter->IsCharacterValid());
	UTEST_TRUE("Added DuplicateCharacter for editing", MetaHumanCharacterSubsystem->TryAddObjectToEdit(DuplicateCharacter));

	ON_SCOPE_EXIT
	{
		// Ensure the subsystem cleans up any resources in the case
		// In particular for handling lifetime issues with the titan texture synthesis static array of allocations
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(DuplicateCharacter);
	};

	FMetaHumanRigEvaluatedState DuplicateFaceState = MetaHumanCharacterSubsystem->GetFaceState(DuplicateCharacter)->Evaluate();

	int32 NumSame = 0;
	int32 NumDifferent = 0;
	bool bSuccess = true;
	for (int32 V = 0; V < FaceState.Vertices.Num(); ++V)
	{
		FVector3f Diff = FaceState.Vertices[V] - DuplicateFaceState.Vertices[V];
		if (Diff.Length() > 0.00001)
		{
			NumDifferent++;
			bSuccess = false;
		}
		else
		{
			NumSame++;
		}
	}
	UE_LOGF(LogMetaHumanCharacterEditorTests, Display, "Number of vertices which are the same = %d; number which are different = %d", NumSame, NumDifferent);

	NumSame = 0;
	NumDifferent = 0;
	for (int32 V = 0; V < FaceState.VertexNormals.Num(); ++V)
	{
		FVector3f Diff = FaceState.VertexNormals[V] - DuplicateFaceState.VertexNormals[V];
		if (Diff.Length() > 0.00001)
		{
			NumDifferent++;
			bSuccess = false;
		}
		else
		{
			NumSame++;
		}
	}
	UE_LOGF(LogMetaHumanCharacterEditorTests, Display, "Number of vertex normals which are the same = %d; number which are different = %d", NumSame, NumDifferent);

	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanInitializeFromPresetTest, "MetaHumanCreator.Character.InitializeFromPreset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanInitializeFromPresetTest::RunTest(const FString& InParams)
{
	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>(GetTransientPackage());
	UTEST_FALSE_EXPR(Character->IsCharacterValid());
	UTEST_TRUE("Initial FaceStateData is empty", Character->GetFaceStateData().GetSize() == 0);

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(MetaHumanCharacterSubsystem);

	MetaHumanCharacterSubsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE_EXPR(MetaHumanCharacterSubsystem->TryAddObjectToEdit(Character));
	UTEST_TRUE_EXPR(Character->IsCharacterValid());

	ON_SCOPE_EXIT
	{
		// Ensure the subsystem cleans up any resources in the case
		// In particular for handling lifetime issues with the titan texture synthesis static array of allocations
		MetaHumanCharacterSubsystem->RemoveObjectToEdit(Character);
	};

	UMetaHumanCharacter* PresetCharacter = LoadObject<UMetaHumanCharacter>(nullptr, TEXT("/" UE_PLUGIN_NAME "/Optional/Presets/Ada.Ada"));

	UTEST_NOT_NULL("Optional preset", PresetCharacter);

	MetaHumanCharacterSubsystem->InitializeFromPreset(Character, PresetCharacter);

	UTEST_EQUAL("Global delta", Character->FaceEvaluationSettings.GlobalDelta, PresetCharacter->FaceEvaluationSettings.GlobalDelta);
	UTEST_EQUAL("High Frequency delta", Character->FaceEvaluationSettings.HighFrequencyDelta, PresetCharacter->FaceEvaluationSettings.HighFrequencyDelta);
	UTEST_EQUAL("Face texture index", Character->SkinSettings.Skin.FaceTextureIndex, PresetCharacter->SkinSettings.Skin.FaceTextureIndex);
	UTEST_EQUAL("Head scale", Character->FaceEvaluationSettings.HeadScale, PresetCharacter->FaceEvaluationSettings.HeadScale);

	//UTEST_EQUAL("Face texture info", Character->SynthesizedFaceTexturesInfo.Num(), PresetCharacter->SynthesizedFaceTexturesInfo.Num());
	UTEST_EQUAL("Body texture info", Character->HighResBodyTexturesInfo.Num(), PresetCharacter->HighResBodyTexturesInfo.Num());

	UTEST_EQUAL("Preview material type", Character->PreviewMaterialType, PresetCharacter->PreviewMaterialType);
	UTEST_EQUAL("Skin properties", Character->SkinSettings.Skin, PresetCharacter->SkinSettings.Skin);
	UTEST_EQUAL("Eyelashes properties", Character->HeadModelSettings.Eyelashes, PresetCharacter->HeadModelSettings.Eyelashes);
	UTEST_EQUAL("Teeth properties", Character->HeadModelSettings.Teeth, PresetCharacter->HeadModelSettings.Teeth);	

	TNotNull<UMetaHumanCollection*> PresetCollection = PresetCharacter->GetMutableInternalCollection();
	const TArray<FMetaHumanCharacterPaletteItem>& PresetItems = PresetCollection->GetItems();

	TNotNull<UMetaHumanCollection*> TargetCollection = Character->GetMutableInternalCollection();
	const TArray<FMetaHumanCharacterPaletteItem>& TargetItems = TargetCollection->GetItems();

	UTEST_EQUAL("Mutable collection items", TargetItems.Num(), PresetItems.Num());

	TNotNull<UMetaHumanInstance*> TargetInstance = TargetCollection->GetMutableDefaultInstance();
	TNotNull<const UMetaHumanInstance*> PresetInstance = PresetCollection->GetMutableDefaultInstance();
	for (int32 ItemIndex = 0; ItemIndex < PresetItems.Num(); ItemIndex++)
	{
		const FMetaHumanCharacterPaletteItem& PresetItem = PresetItems[ItemIndex];
		if (PresetItem.SlotName == UE::MetaHuman::CharacterPipelineSlots::Character
			|| PresetItem.SlotName == NAME_None
			|| !PresetItem.WardrobeItem)
		{
			continue;
		}
		FMetaHumanPaletteItemKey PaletteItemKey = PresetItem.GetItemKey();
		const FMetaHumanPipelineSlotSelection SlotSelectionItem(PresetItem.SlotName, PaletteItemKey);
		if (PresetInstance->ContainsSlotSelection(SlotSelectionItem))
		{
			UTEST_TRUE("Resulting character does not have a required slot selection", TargetInstance->ContainsSlotSelection(SlotSelectionItem));
			const FMetaHumanCharacterPaletteItem& TargetItem = TargetItems[ItemIndex];
			UTEST_EQUAL_EXPR(TargetItem.DisplayName, PresetItem.DisplayName);
			UTEST_EQUAL_EXPR(TargetItem.SlotName, PresetItem.SlotName);
			UTEST_EQUAL_EXPR(TargetItem.Variation, PresetItem.Variation);
			// TODO: Compare wardrobe items.
			if (PresetItem.WardrobeItem->IsExternal())
			{
				UTEST_EQUAL("Wardrobe items are not the same", TargetItem.WardrobeItem, PresetItem.WardrobeItem);
			}
		}
	}

	const TMap<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& InstanceParams = TargetInstance->GetOverriddenInstanceParameters();
	for (const TPair<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& Pair : PresetInstance->GetOverriddenInstanceParameters())
	{
		UTEST_TRUE("Override parameter path", InstanceParams.Contains(Pair.Key));
		UTEST_TRUE("Override parameter property bag", InstanceParams[Pair.Key].Identical(&InstanceParams[Pair.Key], 0));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST( FMetaHumanCharacterTargetKeyPointsChangedBroadcastTest, "MetaHumanCreator.Character.TargetKeyPointsChangedBroadcast", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanCharacterTargetKeyPointsChangedBroadcastTest::RunTest(const FString& InParams)
{
	UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UTEST_NOT_NULL_EXPR(Subsystem);

	UMetaHumanCharacter* Character = NewObject<UMetaHumanCharacter>();
	UTEST_NOT_NULL_EXPR(Character);

	Subsystem->InitializeMetaHumanCharacter(Character);
	UTEST_TRUE("Character added for editing", Subsystem->TryAddObjectToEdit(Character));

	FMetaHumanCharacterTargetMeshKey TargetMeshKey;
	TargetMeshKey.CombinedMesh = TSoftObjectPtr<UObject>();

	FMetaHumanCharacterTargetKeyPoints KeyPoints;
	const FName KeyPointName(TEXT("TestKP1"));
	KeyPoints.CharacterBodyVertexIndexes.Add(KeyPointName, 42);
	KeyPoints.TargetBodyPositions.Add(KeyPointName, FVector3f(1.0f, 2.0f, 3.0f));

	TMap<FName, EKeyPointType> KeyPointTypes;
	KeyPointTypes.Add(KeyPointName, EKeyPointType::Custom);

	int32 BroadcastCount = 0;
	const FDelegateHandle Handle = Subsystem->OnTargetMeshKeyPointsChanged(Character).AddLambda(
		[&BroadcastCount]() { ++BroadcastCount; });

	Subsystem->CommitTargetMeshKeypoints(Character, TargetMeshKey, KeyPoints, KeyPointTypes);
	UTEST_EQUAL("Delegate fired once for live commit", BroadcastCount, 1);

	FMetaHumanCharacterTargetKeyPoints EmptyKeyPoints;
	TMap<FName, EKeyPointType> EmptyKeyPointTypes;
	Subsystem->CommitTargetMeshKeypoints(Character, TargetMeshKey, EmptyKeyPoints, EmptyKeyPointTypes);
	UTEST_EQUAL("Delegate fires again on a follow-up commit (Undo path)", BroadcastCount, 2);

	const FMetaHumanCharacterTargetKeyPoints* StoredKeyPoints =
		Character->TargetMeshKeyPointsCollection.PerMeshTargetKeyPoints.Find(TargetMeshKey);
	UTEST_TRUE(
		"Committed keypoint was removed after second (empty) commit",
		StoredKeyPoints == nullptr || !StoredKeyPoints->CharacterBodyVertexIndexes.Contains(KeyPointName));

	Subsystem->OnTargetMeshKeyPointsChanged(Character).Remove(Handle);

	UMetaHumanCharacter* UnregisteredCharacter = NewObject<UMetaHumanCharacter>();
	UTEST_NOT_NULL_EXPR(UnregisteredCharacter);

	// OnTargetMeshKeyPointsChanged() requires a registered character, so we can't attach a probe
	// here — the assertion is simply that the commit doesn't crash on an unknown character.
	Subsystem->CommitTargetMeshKeypoints(UnregisteredCharacter, TargetMeshKey, KeyPoints, KeyPointTypes);
	UTEST_TRUE("Unregistered-character commit did not crash", true);

	Subsystem->RemoveObjectToEdit(Character);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
