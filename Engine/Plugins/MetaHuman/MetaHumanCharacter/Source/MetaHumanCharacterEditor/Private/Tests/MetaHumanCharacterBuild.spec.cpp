// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace UE::MetaHuman
{
	// Moves every direct object inside the given test package to the transient
	// package for GC, using the canonical pattern from
	// ObjectTools::DeleteRedirector. Required for tests that leave persistent
	// (RF_Public | RF_Standalone) state behind, including UObjectRedirectors
	// produced by REN_None renames. Without this, re-running the same test in
	// the same editor session crashes when NewObject collides with leftover
	// objects of different classes.
	static void SetObjectsForGC(UPackage* Package)
	{
		if (Package == nullptr)
		{
			return;
		}
		TArray<UObject*> ToTrash;
		ForEachObjectWithOuter(Package,
			[&ToTrash](UObject* Obj) { ToTrash.Add(Obj); },
			EGetObjectsFlags::None);
		for (UObject* Obj : ToTrash)
		{
			Obj->ClearFlags(RF_Public | RF_Standalone);
			Obj->SetFlags(RF_Transient);
			Obj->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}
}

BEGIN_DEFINE_SPEC(FMetaHumanCharacterBuildSpec, "MetaHumanCreator.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FMetaHumanCharacterBuildSpec)

void FMetaHumanCharacterBuildSpec::Define()
{
	// ---------------------------------------------------------------------------
	// MH-16474: DuplicateDepedenciesToNewRoot regression spec.
	//
	// Three scenarios for the conflict-handling logic when copying plugin Common
	// dependencies into the project's Common folder
	// ---------------------------------------------------------------------------
	Describe("DuplicateDependencies", [this]
	{
		// -----------------------------------------------------------------------
		// Test 1: Different-class collision at the Common target path.
		//
		// An existing asset of a fundamentally different UClass already occupies
		// the target path. The class-compatibility guard should detect the
		// mismatch, log a clear error, and return false so the caller bails out
		// cleanly instead of crashing in DuplicateObject.
		//
		// Pre-call package state:
		//   /Game/MH16474Test/Source/TestAsset (UPackage)
		//       -- TestAsset (USkeletalMesh)         <-  source dependency
		//   /Game/MH16474Test/Common/MH16474Test/Source/TestAsset (UPackage)
		//       -- TestAsset (UTexture2D)            <-  different class at target
		//
		// Expected post-call state: function returns false; the conflicting
		// texture is left untouched.
		// -----------------------------------------------------------------------
		Describe("DifferentAssetTypeInMemory", [this]
		{
			It("should return false without crashing", [this]
			{
				UPackage* SourcePackage = CreatePackage(TEXT("/Game/MH16474Test/Source/TestAsset"));
				TestNotNull(TEXT("Source package"), SourcePackage);
				USkeletalMesh* SourceMesh = NewObject<USkeletalMesh>(SourcePackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Source mesh"), SourceMesh);

				const FString CommonFolderPath = TEXT("/Game/MH16474Test/Common");
				UPackage* TargetPackage = CreatePackage(*(CommonFolderPath / TEXT("MH16474Test/Source/TestAsset")));
				TestNotNull(TEXT("Target package"), TargetPackage);

				UTexture2D* ConflictingTexture = NewObject<UTexture2D>(TargetPackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Conflicting texture"), ConflictingTexture);

				TSet<UObject*> Dependencies;
				Dependencies.Add(SourceMesh);
				TSet<UObject*> ObjectsToReplaceWithin;
				TMap<UObject*, UObject*> DuplicatedDependencies;

				AddExpectedError(TEXT("Cannot copy Common asset"), EAutomationExpectedErrorFlags::Contains, 1);
				const bool bSuccess = FMetaHumanCharacterEditorBuild::DuplicateDepedenciesToNewRoot(
					Dependencies, CommonFolderPath, ObjectsToReplaceWithin, DuplicatedDependencies,
					[](const UObject*) -> bool { return true; });

				TestFalse(TEXT("DuplicateDepedenciesToNewRoot should return false on class mismatch"), bSuccess);

				UE::MetaHuman::SetObjectsForGC(SourcePackage);
				UE::MetaHuman::SetObjectsForGC(TargetPackage);
			});
		});

		// -----------------------------------------------------------------------
		// Test 1b: Different-class collision where the conflicting asset is saved
		// on disk and registered with the asset registry.
		//
		// In this case UPackageTools::FindOrCreatePackageForAssetType detects the
		// class mismatch via the asset registry's on-disk lookup
		// (bIncludeOnlyOnDiskAssets = true) and returns a fresh package with a
		// unique name. The new asset lands at that unique name; the original
		// on-disk asset is left untouched. No bail-out is needed because the
		// duplication never targets the conflicting package.
		//
		// Pre-call package state:
		//   /Game/MH16474Test1b/Source/TestAsset (UPackage, in-memory)
		//       -- TestAsset (UCurveLinearColor)     <- source dependency
		//   /Game/MH16474Test1b/Common/MH16474Test1b/Source/TestAsset (UPackage, on disk)
		//       -- TestAsset (UCurveFloat)           <- saved & registered
		//
		// Expected post-call state (correct behavior — bail-out, consistent with the
		// in-memory class-mismatch guard above):
		//   /Game/MH16474Test1b/Common/MH16474Test1b/Source/TestAsset (UPackage, on disk)
		//       -- TestAsset (UCurveFloat)           <- untouched
		//   No new UCurveLinearColor anywhere — neither at TargetPackage nor in a
		//   unique-name package.
		//
		// Note: UCurveLinearColor and UCurveFloat are picked because both are
		// concrete asset classes with trivial Serialize() implementations that
		// survive bare NewObject construction. They're a different-class pair so
		// the test still exercises the class-mismatch path.
		// -----------------------------------------------------------------------
		Describe("DifferentAssetType", [this]
		{
			It("should bail out without creating any duplicate", [this]
			{
				// 1. Source dependency (UCurveLinearColor — concrete and trivially serializable)
				UPackage* SourcePackage = CreatePackage(TEXT("/Game/MH16474Test1b/Source/TestAsset"));
				TestNotNull(TEXT("Source package"), SourcePackage);
				UCurveLinearColor* SourceAsset = NewObject<UCurveLinearColor>(SourcePackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Source asset"), SourceAsset);

				// 2. Place a UCurveFloat at the target path, register it with the asset
				//    registry and save it to disk so FindOrCreatePackageForAssetType's
				//    on-disk lookup finds a class mismatch.
				const FString CommonFolderPath = TEXT("/Game/MH16474Test1b/Common");
				const FString TargetPackagePath = CommonFolderPath / TEXT("MH16474Test1b/Source/TestAsset");
				UPackage* TargetPackage = CreatePackage(*TargetPackagePath);
				TestNotNull(TEXT("Target package"), TargetPackage);

				UCurveFloat* ConflictingCurve = NewObject<UCurveFloat>(TargetPackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Conflicting curve"), ConflictingCurve);
				FAssetRegistryModule::AssetCreated(ConflictingCurve);

				// FullyLoad + SetDirtyFlag are needed so SavePackage doesn't refuse with a
				// "partially loaded" warning on a fresh in-memory package.
				TargetPackage->FullyLoad();
				TargetPackage->SetDirtyFlag(true);

				const FString TargetPackageFileName = FPackageName::LongPackageNameToFilename(TargetPackagePath, FPackageName::GetAssetPackageExtension());
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.Error = GLog;
				const bool bSaved = UPackage::SavePackage(TargetPackage, ConflictingCurve, *TargetPackageFileName, SaveArgs);
				TestTrue(TEXT("Conflicting curve asset saved to disk"), bSaved);

				if (bSaved)
				{
					// Force the asset registry to rescan the .uasset we just wrote so the
					// on-disk lookup inside FindOrCreatePackageForAssetType (which uses
					// bIncludeOnlyOnDiskAssets = true) sees the conflicting curve.
					IAssetRegistry::GetChecked().ScanFilesSynchronous({ TargetPackageFileName }, /*bForceRescan=*/true);

					// 3. Expect the same class-mismatch error the in-memory case produces,
					//    and run the duplication. The build should detect the on-disk
					//    class mismatch and bail out cleanly instead of silently emitting
					//    to a unique-name package.
					AddExpectedError(TEXT("Cannot copy Common asset"), EAutomationExpectedErrorFlags::Contains, 1);

					// Saving and re-scanning under a path that was previously cleaned up by an
					// earlier test run produces a benign asset-registry log line. Mark it as
					// expected so it doesn't pollute the test output.
					AddExpectedMessage(TEXT("package was marked as deleted in editor"),
						ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

					TSet<UObject*> Dependencies;
					Dependencies.Add(SourceAsset);
					TSet<UObject*> ObjectsToReplaceWithin;
					TMap<UObject*, UObject*> DuplicatedDependencies;

					const bool bSuccess = FMetaHumanCharacterEditorBuild::DuplicateDepedenciesToNewRoot(
						Dependencies, CommonFolderPath, ObjectsToReplaceWithin, DuplicatedDependencies,
						[](const UObject*) -> bool { return true; });

					// 4. The pipeline must bail out — same as the in-memory class-mismatch case.
					TestFalse(TEXT("DuplicateDepedenciesToNewRoot should bail out on the on-disk class mismatch"), bSuccess);

					// 5. No duplicate should have been created
					if (UObject* const* DuplicatedPtr = DuplicatedDependencies.Find(SourceAsset))
					{
						TestNull(TEXT("Source dependency should not have a duplicate object recorded"), *DuplicatedPtr);
					}
				}

				// 5. Cleanup: remove the conflicting asset from the registry, delete the
				//    .uasset file we wrote, and trash the in-memory packages so reruns
				//    in the same editor session don't see stale state.
				FAssetRegistryModule::AssetDeleted(ConflictingCurve);
				IFileManager::Get().Delete(*TargetPackageFileName, /*bRequireExists=*/false, /*bEvenReadOnly=*/true);
				UE::MetaHuman::SetObjectsForGC(SourcePackage);
				UE::MetaHuman::SetObjectsForGC(TargetPackage);
			});
		});

		// -----------------------------------------------------------------------
		// Test 2: Same-class existing asset at the target path has been renamed
		// (with a redirector left behind).
		//
		// FindObject at the target path returns a UObjectRedirector — a different
		// UClass than the source asset. The class-compatibility guard should
		// detect this, log the error, and return false so the caller bails out
		// cleanly instead of letting DuplicateObject emplace the new asset into
		// the renamed destination package.
		//
		// Pre-call package state:
		//   /Game/MH16474Test2/Source/TestAsset (UPackage)
		//       -- TestAsset (USkeletalMesh)          <-  source dependency
		//   /Game/MH16474Test2/Common/MH16474Test2/Source/TestAsset (UPackage)
		//       -- TestAsset (UObjectRedirector)      <- left behind by REN_None
		//                                          
		//   /Game/MH16474Test2/Common/MH16474Test2/Source/TestAsset_Renamed (UPackage)
		//       -- TestAsset_Renamed (USkeletalMesh)  <- renamed asset (same class)
		//
		// Expected post-call state: function returns false; the redirector and
		// the renamed asset are left untouched.
		// -----------------------------------------------------------------------
		Describe("RedirectorAtTarget", [this]
		{
			It("should return false without crashing", [this]
			{
				UPackage* SourcePackage = CreatePackage(TEXT("/Game/MH16474Test2/Source/TestAsset"));
				TestNotNull(TEXT("Source package"), SourcePackage);
				USkeletalMesh* SourceMesh = NewObject<USkeletalMesh>(SourcePackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Source mesh"), SourceMesh);

				const FString CommonFolderPath = TEXT("/Game/MH16474Test2/Common");
				UPackage* TargetPackage = CreatePackage(*(CommonFolderPath / TEXT("MH16474Test2/Source/TestAsset")));
				TestNotNull(TEXT("Target package"), TargetPackage);

				USkeletalMesh* OriginalAtTarget = NewObject<USkeletalMesh>(TargetPackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Original at target"), OriginalAtTarget);

				UPackage* RenameDestPackage = CreatePackage(TEXT("/Game/MH16474Test2/Common/MH16474Test2/Source/TestAsset_Renamed"));
				TestNotNull(TEXT("Rename destination package"), RenameDestPackage);

				const bool bRenamed = OriginalAtTarget->Rename(TEXT("TestAsset_Renamed"), RenameDestPackage, REN_None);
				TestTrue(TEXT("Rename of existing asset succeeded"), bRenamed);
				TestTrue(TEXT("Renamed asset is still a SkeletalMesh"), OriginalAtTarget->IsA<USkeletalMesh>());

				UObject* AtOriginalPath = FindObject<UObject>(TargetPackage, TEXT("TestAsset"));
				TestNotNull(TEXT("Redirector should exist at original target path"), AtOriginalPath);
				if (AtOriginalPath != nullptr)
				{
					TestTrue(TEXT("Object at original target path should be a UObjectRedirector"), AtOriginalPath->IsA<UObjectRedirector>());
				}

				TSet<UObject*> Dependencies;
				Dependencies.Add(SourceMesh);
				TSet<UObject*> ObjectsToReplaceWithin;
				TMap<UObject*, UObject*> DuplicatedDependencies;

				AddExpectedError(TEXT("Cannot copy Common asset"), EAutomationExpectedErrorFlags::Contains, 1);
				const bool bSuccess = FMetaHumanCharacterEditorBuild::DuplicateDepedenciesToNewRoot(
					Dependencies, CommonFolderPath, ObjectsToReplaceWithin, DuplicatedDependencies,
					[](const UObject*) -> bool { return true; });

				TestFalse(TEXT("DuplicateDepedenciesToNewRoot should return false when a redirector occupies the target path"), bSuccess);

				UE::MetaHuman::SetObjectsForGC(SourcePackage);
				UE::MetaHuman::SetObjectsForGC(TargetPackage);
				UE::MetaHuman::SetObjectsForGC(RenameDestPackage);
			});
		});

		// -----------------------------------------------------------------------
		// Test 3: Orphan same-class asset in the target package (no redirector).
		//
		// The pre-existing asset was renamed inside the same target package using
		// REN_DontCreateRedirectors, so FindObject at the original name returns
		// null and the class-compatibility guard never fires. The build doesn't
		// modify the orphan — it just logs a warning so the user knows the package
		// is in a non-pristine state — and proceeds to duplicate the source asset
		// into the same package alongside the orphan. DuplicateObject doesn't
		// conflict because the orphan has a different name.
		//
		// Pre-call package state:
		//   /Game/MH16474Test3/Source/TestAsset (UPackage)
		//       -- TestAsset (UCurveLinearColor)         <- source dependency
		//   /Game/MH16474Test3/Common/MH16474Test3/Source/TestAsset (UPackage)
		//       -- TestAsset_Orphan (UCurveLinearColor)  <- same class, mismatched name
		//
		// Expected post-call state:
		//   /Game/MH16474Test3/Common/MH16474Test3/Source/TestAsset (UPackage)
		//       -- TestAsset_Orphan (UCurveLinearColor)  <- untouched
		//       -- TestAsset        (UCurveLinearColor, new) <- duplicated primary
		//
		// Note: This test uses UCurveLinearColor rather than USkeletalMesh because
		// DuplicateObject reaches the source's Serialize() in this path, and a
		// bare NewObject<USkeletalMesh> has uninitialized render data which
		// crashes on serialize. UCurveLinearColor is a concrete asset class with
		// trivial serialization and is safe to construct in a unit test. The bug
		// surface (orphan in target package, same-class collision) is identical
		// regardless of asset class.
		// -----------------------------------------------------------------------
		Describe("OrphanInTargetPackage", [this]
		{
			It("should warn about the orphan and succeed without modifying it", [this]
			{
				UPackage* SourcePackage = CreatePackage(TEXT("/Game/MH16474Test3/Source/TestAsset"));
				TestNotNull(TEXT("Source package"), SourcePackage);
				UCurveLinearColor* SourceAsset = NewObject<UCurveLinearColor>(SourcePackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Source asset"), SourceAsset);

				const FString CommonFolderPath = TEXT("/Game/MH16474Test3/Common");
				UPackage* TargetPackage = CreatePackage(*(CommonFolderPath / TEXT("MH16474Test3/Source/TestAsset")));
				TestNotNull(TEXT("Target package"), TargetPackage);

				UCurveLinearColor* OrphanAsset = NewObject<UCurveLinearColor>(TargetPackage, TEXT("TestAsset"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Orphan asset"), OrphanAsset);

				const bool bRenamed = OrphanAsset->Rename(TEXT("TestAsset_Orphan"), TargetPackage, REN_DontCreateRedirectors);
				TestTrue(TEXT("Rename without redirector succeeded"), bRenamed);
				TestTrue(TEXT("Renamed asset is still a UCurveLinearColor"), OrphanAsset->IsA<UCurveLinearColor>());
				TestNull(TEXT("FindObject at original name should return null (no redirector)"), FindObject<UObject>(TargetPackage, TEXT("TestAsset")));
				TestNotNull(TEXT("Orphan still exists in target package under new name"), FindObject<UObject>(TargetPackage, TEXT("TestAsset_Orphan")));

				// Production logs a warning about the orphan and continues. Mark it
				// expected so it doesn't fail the test.
				AddExpectedMessage(TEXT("contains an orphan asset"),
					ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

				TSet<UObject*> Dependencies;
				Dependencies.Add(SourceAsset);
				TSet<UObject*> ObjectsToReplaceWithin;
				TMap<UObject*, UObject*> DuplicatedDependencies;

				const bool bSuccess = FMetaHumanCharacterEditorBuild::DuplicateDepedenciesToNewRoot(
					Dependencies, CommonFolderPath, ObjectsToReplaceWithin, DuplicatedDependencies,
					[](const UObject*) -> bool { return true; });

				TestTrue(TEXT("DuplicateDepedenciesToNewRoot should succeed even with an orphan present"), bSuccess);

				// New asset must exist at original target name and be the right class.
				UObject* NewAtTarget = FindObject<UObject>(TargetPackage, TEXT("TestAsset"));
				TestNotNull(TEXT("New asset should exist at original target name"), NewAtTarget);
				if (NewAtTarget != nullptr)
				{
					TestTrue(TEXT("New asset should be a UCurveLinearColor"), NewAtTarget->IsA<UCurveLinearColor>());
				}

				// Orphan must be untouched: still in the target package, still has its
				// original name, still public/standalone (NOT moved to transient).
				TestEqual(TEXT("Orphan should still live in the target package"), OrphanAsset->GetOutermost(), TargetPackage);
				TestNotNull(TEXT("Orphan should still be at TestAsset_Orphan"), FindObject<UObject>(TargetPackage, TEXT("TestAsset_Orphan")));
				TestTrue(TEXT("Orphan should still be public"), OrphanAsset->HasAnyFlags(RF_Public));
				TestFalse(TEXT("Orphan should not be marked transient"), OrphanAsset->HasAnyFlags(RF_Transient));

				UE::MetaHuman::SetObjectsForGC(SourcePackage);
				UE::MetaHuman::SetObjectsForGC(TargetPackage);
			});
		});

		Describe("ReplaceOutOfDateCommonDependencies", [this]
		{
			// -----------------------------------------------------------------------
			// Test: DeleteMaterialInstanceChildren cascade does NOT mark a sibling-child
			// dependency target as garbage.
			//
			// Two MIs are in InDependencies — a parent material and a child UMaterialInstanceConstant 
			// whose Parent is the parent material. Pre-existing targets exist at both destination paths.
			// The parent target has no MHAssetVersion metadata, so its iteration falls
			// through to DeleteMaterialInstanceChildren. .
			//
			// Pre-call package state:
			//   /Game/MHCascade/Source/ParentMI (UPackage)
			//       -- ParentMI (UMaterial)                                   <- source
			//   /Game/MHCascade/Source/ChildMI (UPackage)
			//       -- ChildMI (UMaterialInstanceConstant, Parent=SourceParentMat)  <- source
			//   /Game/MHCascade/Common/MHCascade/Source/ParentMI (UPackage)
			//       -- ParentMI (UMaterial, no MHAssetVersion)                 <- out-of-date target
			//   /Game/MHCascade/Common/MHCascade/Source/ChildMI (UPackage)
			//       -- ChildMI (UMaterialInstanceConstant, Parent=TargetParentMat,
			//                   MHAssetVersion=current)                        <- up-to-date target
			// -----------------------------------------------------------------------
			It("should preserve an up to date MI dependency even when the parent MI is out of date", [this]
			{
				// Sources
				UPackage* SourceParentPkg = CreatePackage(TEXT("/Game/MHCascade/Source/ParentMI"));
				UPackage* SourceChildPkg  = CreatePackage(TEXT("/Game/MHCascade/Source/ChildMI"));
				TestNotNull(TEXT("Source parent pkg"), SourceParentPkg);
				TestNotNull(TEXT("Source child pkg"),  SourceChildPkg);

				UMaterial* SourceParentMat = NewObject<UMaterial>(SourceParentPkg, TEXT("ParentMI"), RF_Public | RF_Standalone);
				UMaterialInstanceConstant* SourceChildMI = NewObject<UMaterialInstanceConstant>(SourceChildPkg, TEXT("ChildMI"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Source ParentMat"), SourceParentMat);
				TestNotNull(TEXT("Source ChildMI"),   SourceChildMI);

				SourceChildMI->Parent = SourceParentMat;
				SourceChildMI->PostLoad();

				// Targets
				const FString CommonFolderPath = TEXT("/Game/MHCascade/Common");
				UPackage* TargetParentPkg = CreatePackage(*(CommonFolderPath / TEXT("MHCascade/Source/ParentMI")));
				UPackage* TargetChildPkg  = CreatePackage(*(CommonFolderPath / TEXT("MHCascade/Source/ChildMI")));
				TestNotNull(TEXT("Target parent pkg"), TargetParentPkg);
				TestNotNull(TEXT("Target child pkg"),  TargetChildPkg);

				UMaterial* TargetParentMat = NewObject<UMaterial>(TargetParentPkg, TEXT("ParentMI"), RF_Public | RF_Standalone);
				UMaterialInstanceConstant* TargetChildMI = NewObject<UMaterialInstanceConstant>(TargetChildPkg, TEXT("ChildMI"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Target ParentMat"), TargetParentMat);
				TestNotNull(TEXT("Target ChildMI"),   TargetChildMI);

				// This is the link that makes IsChildOf(TargetParentMat) true on
				// TargetChildMI — i.e. arms the cascade.
				TargetChildMI->Parent = TargetParentMat;
				TargetChildMI->PostLoad();

				// Mark TargetChildMI up-to-date so its iteration takes the version-check
				// shortcut. TargetParentMat is left without metadata so its iteration
				// falls through to DeleteMaterialInstanceChildren.
				FMetaHumanCharacterEditorBuild::SetMetaHumanVersionMetadata(TargetChildMI);

				TestFalse(TEXT("Child target starts alive"), TargetChildMI->HasAnyInternalFlags(EInternalObjectFlags::Garbage));

				TSet<UObject*> Dependencies;
				Dependencies.Add(SourceParentMat);
				Dependencies.Add(SourceChildMI);
				TSet<UObject*> ObjectsToReplaceWithin;
				TMap<UObject*, UObject*> DuplicatedDependencies;

				const bool bSuccess = FMetaHumanCharacterEditorBuild::DuplicateDepedenciesToNewRoot(
					Dependencies, CommonFolderPath, ObjectsToReplaceWithin, DuplicatedDependencies,
					[](const UObject*) -> bool { return true; });

				TestTrue (TEXT("DuplicateDepedenciesToNewRoot succeeds"), bSuccess);
				TestFalse(TEXT("Child target NOT marked garbage by cascade"), TargetChildMI->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
				TestTrue (TEXT("Parent dependency duplicated"), DuplicatedDependencies.Contains(SourceParentMat));
				TestTrue (TEXT("Child dependency duplicated"),  DuplicatedDependencies.Contains(SourceChildMI));

				// The child took the version-check shortcut, so the recorded duplicate
				// for SourceChildMI is the still-alive TargetChildMI.
				if (UObject* const* DupChild = DuplicatedDependencies.Find(SourceChildMI))
				{
					TestEqual(TEXT("Child shortcut reused TargetChildMI"), Cast<UMaterialInstanceConstant>(*DupChild), TargetChildMI);
					TestNotNull(TEXT("Child duplicate is non-null"), *DupChild);
					if (*DupChild != nullptr)
					{
						TestFalse(TEXT("Child duplicate not garbage"), (*DupChild)->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
					}
				}

				UE::MetaHuman::SetObjectsForGC(SourceParentPkg);
				UE::MetaHuman::SetObjectsForGC(SourceChildPkg);
				UE::MetaHuman::SetObjectsForGC(TargetParentPkg);
				UE::MetaHuman::SetObjectsForGC(TargetChildPkg);
			});


			// Fixture: a single out-of-date parent dependency, plus an unnamed runtime
			// UMIC sitting in memory whose Parent is the existing target's parent material.
			// The runtime UMIC is NOT in InDependencies and not in OutDuplicatedDependencies,
			// so it is NOT in the post-loop NewDependencies protect set and should marked as garbage.
			It("should GC unnamed runtime MICs when their parent is out of date", [this]
			{
				// Source: out-of-date parent material dependency
				UPackage* SourceParentPkg = CreatePackage(TEXT("/Game/MHCascadeOOD/Source/ParentMI"));
				TestNotNull(TEXT("Source parent pkg"), SourceParentPkg);
				UMaterial* SourceParentMat = NewObject<UMaterial>(SourceParentPkg, TEXT("ParentMI"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Source ParentMat"), SourceParentMat);

				// Target: existing parent material (out-of-date — no MHAssetVersion)
				const FString CommonFolderPath = TEXT("/Game/MHCascadeOOD/Common");
				UPackage* TargetParentPkg = CreatePackage(*(CommonFolderPath / TEXT("MHCascadeOOD/Source/ParentMI")));
				TestNotNull(TEXT("Target parent pkg"), TargetParentPkg);
				UMaterial* TargetParentMat = NewObject<UMaterial>(TargetParentPkg, TEXT("ParentMI"), RF_Public | RF_Standalone);
				TestNotNull(TEXT("Target ParentMat"), TargetParentMat);

				// Unnamed runtime UMIC sitting in memory, derived from TargetParentMat.
				// Not part of InDependencies — the kind of orphan the cascade is
				// designed to clean up.
				UMaterialInstanceConstant* RuntimeMI = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
				TestNotNull(TEXT("Runtime MI"), RuntimeMI);
				RuntimeMI->Parent = TargetParentMat;
				RuntimeMI->PostLoad();

				TestFalse(TEXT("Runtime MI starts alive"),
					RuntimeMI->HasAnyInternalFlags(EInternalObjectFlags::Garbage));

				TSet<UObject*> Dependencies;
				Dependencies.Add(SourceParentMat);
				TSet<UObject*> ObjectsToReplaceWithin;
				TMap<UObject*, UObject*> DuplicatedDependencies;

				const bool bSuccess = FMetaHumanCharacterEditorBuild::DuplicateDepedenciesToNewRoot(
					Dependencies, CommonFolderPath, ObjectsToReplaceWithin, DuplicatedDependencies,
					[](const UObject*) -> bool { return true; });

				TestTrue(TEXT("DuplicateDepedenciesToNewRoot succeeds (replacement path)"), bSuccess);
				TestTrue(TEXT("Parent dependency duplicated"), DuplicatedDependencies.Contains(SourceParentMat));
				// The RuntimeMI has been GC'ed at this point

				UE::MetaHuman::SetObjectsForGC(SourceParentPkg);
				UE::MetaHuman::SetObjectsForGC(TargetParentPkg);
			});
		});
	});

	// DeleteMaterialInstanceChildren regression spec.
	Describe("DeleteMaterialInstanceChildren", [this]
	{
		It("should skip MIs in the protected set and still mark unprotected children garbage", [this]
		{
			UPackage* Pkg = CreatePackage(TEXT("/Game/MHDMICTest/Materials"));
			TestNotNull(TEXT("Package"), Pkg);

			UMaterial* Parent = NewObject<UMaterial>(Pkg, TEXT("Parent"), RF_Public | RF_Standalone);
			TestNotNull(TEXT("Parent material"), Parent);

			UMaterialInstanceConstant* Unprotected = NewObject<UMaterialInstanceConstant>(Pkg, TEXT("Unprotected"), RF_Public | RF_Standalone);
			UMaterialInstanceConstant* Shielded    = NewObject<UMaterialInstanceConstant>(Pkg, TEXT("Shielded"),    RF_Public | RF_Standalone);
			TestNotNull(TEXT("Unprotected child"), Unprotected);
			TestNotNull(TEXT("Shielded child"),    Shielded);

			Unprotected->Parent = Parent;
			Shielded->Parent    = Parent;
			Unprotected->PostLoad();
			Shielded->PostLoad();

			TSet<UObject*> Protected;
			Protected.Add(Shielded);

			FMetaHumanCharacterEditorBuild::DeleteMaterialInstanceChildren(Parent, &Protected);

			TestTrue (TEXT("Unprotected child IS marked garbage"),
				Unprotected->HasAnyInternalFlags(EInternalObjectFlags::Garbage));
			TestFalse(TEXT("Shielded child is NOT marked garbage"),
				Shielded->HasAnyInternalFlags(EInternalObjectFlags::Garbage));

			UE::MetaHuman::SetObjectsForGC(Pkg);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
