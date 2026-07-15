// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CQTest.h"
#include "CompositeActor.h"
#include "Components/CompositeShadowReflectionCatcherComponent.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Layers/CompositeLayerShadowReflection.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	int32 GetShadowReflectionCatcherCount(const ACompositeActor* InActor)
	{
		if (!InActor)
		{
			return 0;
		}

		TArray<UCompositeShadowReflectionCatcherComponent*> Catchers;
		InActor->GetComponents<UCompositeShadowReflectionCatcherComponent>(Catchers);
		return Catchers.Num();
	}

	ACompositeActor* FindCompositeActor(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}

		TActorIterator<ACompositeActor> It(InWorld);
		return It ? *It : nullptr;
	}

	UWorld* CreateTestWorld(UPackage* InPackage, FName InName)
	{
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld=*/false, InName, InPackage);
		WorldContext.SetCurrentWorld(World);
		return World;
	}

	void TearDownTestWorld(UWorld*& InOutWorld)
	{
		if (!InOutWorld)
		{
			return;
		}

		GEngine->DestroyWorldContext(InOutWorld);
		InOutWorld->DestroyWorld(/*bBroadcastWorldDestroyedEvent=*/true);
		InOutWorld->MarkAsGarbage();
		InOutWorld = nullptr;
	}
}

TEST_CLASS_WITH_FLAGS(
	FCompositeShadowReflectionPersistenceTests,
	"Composite.UnitTests.ShadowReflectionPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
	FString OriginalPackageName;
	FString TempFilename;

	UPackage* OriginalPackage = nullptr;
	UWorld* OriginalWorld = nullptr;
	ACompositeActor* CompositeActor = nullptr;
	AStaticMeshActor* HiddenActor = nullptr;
	UCompositeLayerShadowReflection* Layer = nullptr;

	UWorld* ReloadedWorld = nullptr;

	BEFORE_EACH()
	{
		OriginalPackageName = FString::Printf(
			TEXT("/Engine/Transient/ShadowReflectionTest_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		TempFilename = FPaths::ConvertRelativePathToFull(
			FPackageName::LongPackageNameToFilename(OriginalPackageName, FPackageName::GetMapPackageExtension()));

		OriginalPackage = CreatePackage(*OriginalPackageName);
		OriginalPackage->SetFlags(RF_Standalone | RF_Public);

		OriginalWorld = CreateTestWorld(OriginalPackage, FName("PersistedWorld"));
		OriginalWorld->SetFlags(RF_Standalone | RF_Public);
		OriginalPackage->ThisContainsMap();

		CompositeActor = OriginalWorld->SpawnActor<ACompositeActor>();
		HiddenActor = OriginalWorld->SpawnActor<AStaticMeshActor>();

		Layer = NewObject<UCompositeLayerShadowReflection>(CompositeActor);
		CompositeActor->SetCompositeLayers({ Layer });
		Layer->SetIsEnabled(true);
		Layer->SetActors({ HiddenActor });

		// Pre-save sanity check: fixture is correctly set up.
		ASSERT_THAT(AreEqual(2, GetShadowReflectionCatcherCount(CompositeActor)));
	}

	AFTER_EACH()
	{
		CompositeActor = nullptr;
		HiddenActor = nullptr;
		Layer = nullptr;

		TearDownTestWorld(OriginalWorld);
		TearDownTestWorld(ReloadedWorld);

		OriginalPackage = nullptr;

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

		if (!TempFilename.IsEmpty())
		{
			IFileManager::Get().Delete(*TempFilename, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
		}
	}

	TEST_METHOD(SurvivesSaveReloadWithoutDuplication)
	{
		// 1. Save the world to disk.
		const bool bSaved = UPackage::SavePackage(OriginalPackage, OriginalWorld, *TempFilename, FSavePackageArgs());
		ASSERT_THAT(IsTrue(bSaved));

		// 2. Tear down the in-memory world, then move the package out of its long-name slot
		//    so LoadPackage cannot short-circuit and return it.
		TearDownTestWorld(OriginalWorld);

		const FString SavedLongName = OriginalPackageName;
		const FName DiscardedName = MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass(), TEXT("DiscardedShadowReflectionPkg"));
		OriginalPackage->Rename(
			*DiscardedName.ToString(),
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		OriginalPackage = nullptr;

		// 3. Reload from disk and register a world context so teardown is uniform.
		UPackage* LoadedPackage = LoadPackage(nullptr, *SavedLongName, LOAD_None);
		ASSERT_THAT(IsNotNull(LoadedPackage));

		ReloadedWorld = UWorld::FindWorldInPackage(LoadedPackage);
		ASSERT_THAT(IsNotNull(ReloadedWorld));

		FWorldContext& ReloadedContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
		ReloadedContext.SetCurrentWorld(ReloadedWorld);

		ACompositeActor* ReloadedActor = FindCompositeActor(ReloadedWorld);
		ASSERT_THAT(IsNotNull(ReloadedActor));

		// 4. The actual invariant.
		ASSERT_THAT(AreEqual(2, GetShadowReflectionCatcherCount(ReloadedActor)));
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
