// Copyright Epic Games, Inc. All Rights Reserved.

#include "Verification/VerifyMetaHumanPackageSource.h"

#include "MetaHumanAssetReport.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"
#include "ProjectUtilities/MetaHumanDependencyWalker.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/Paths.h"
#include "Misc/RuntimeErrors.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VerifyMetaHumanPackageSource)

#define LOCTEXT_NAMESPACE "VerifyMetaHumanPackageSource"

enum class EDependencyState: int8
{
	Allowed,
	AllowedDoNotFollow,
	Forbidden
};

static EDependencyState IsDependencyAllowed(const FString& Dependency)
{
	static TArray<FString> AllowedPaths = {
		//Content
		"/Game",

		// Core Engine Assets
		"/Engine",
		"/Script/Engine",
		"/Script/CoreUObject",

		// Commonly used engine types
		"/Script/AnimGraph",
		"/Script/AnimGraphRuntime",
		"/Script/AnimationCore",
		"/Script/AnimationData",
		"/Script/AnimationModifiers",
		"/Script/BlueprintGraph",
		"/Script/Chaos",
		"/Script/ChaosCloth",
		"/Script/ChaosOutfitAssetDataflowNodes",
		"/Script/ChaosClothAssetEngine",
		"/Script/ClothingSystemRuntimeInterface",
		"/Script/ClothingSystemRuntimeNv",
		"/Script/ClothingSystemRuntimeCommon",
		"/Script/DataflowEditor",
		"/Script/DataflowEngine",
		"/Script/IKRig",
		"/Script/IKRigDeveloper",
		"/Script/InterchangeCore",
		"/Script/InterchangeEngine",
		"/Script/InterchangeImport",
		"/Script/InterchangePipelines",
		"/Script/LiveLink",
		"/Script/LiveLinkAnimationCore",
		"/Script/LiveLinkGraphNode",
		"/Script/LiveLinkInterface",
		"/Script/MeshDescription",
		"/Script/MetaHumanSDKRuntime",
		"/Script/MovieScene",
		"/Script/MovieSceneTracks",
		"/Script/NavigationSystem",
		"/Script/PBIK",
		"/Script/PhysicsCore",
		"/Script/RigLogicDeveloper",
		"/Script/RigLogicModule",
		"/Script/RigVM",
		"/Script/RigVMDeveloper",
		"/Script/UnrealEd",
		"/Script/USDClasses",

		// Hair-strands plugin
		"/HairStrands",
		"/Script/HairStrands",
		"/Script/HairStrandsCore",

		// Interchange plugin
		"/InterchangeAssets",

		// Niagara plugin
		"/Niagara",
		"/Script/Niagara",
		"/Script/NiagaraCore",
		"/Script/NiagaraEditor",
		"/Script/NiagaraShader",

		// ControlRig plugin
		"/ControlRig",
		"/Script/ControlRig",
		"/Script/ControlRigDeveloper",
		"/Script/ControlRigSpline",

		// MetaHumanCharacter plugin
		"/MetaHumanCharacter",
		"/Script/MetaHumanCharacter",
		"/Script/MetaHumanCharacterPalette",
		"/Script/MetaHumanDefaultPipeline",
		"/Script/MetaHumanDefaultEditorPipeline",
		"/Script/DataHierarchyEditor",

		// ChaosClothAsset plugin
		"/ChaosClothAsset",

		// ChaosOutfitAsset plugin
		"/ChaosOutfitAsset",
		"/Script/ChaosOutfitAssetEngine"
	};

	for (const FString& RootPath : AllowedPaths)
	{
		if (FPaths::IsUnderDirectory(Dependency, RootPath))
		{
			return EDependencyState::AllowedDoNotFollow;
		}
	}

	return EDependencyState::Forbidden;
}

static UObject* GetMainObjectFromPackageName(const FName& PackageName)
{
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPackageName(PackageName, Assets);
	if (Assets.Num())
	{
		return Assets[0].GetAsset();
	}

	return nullptr;
}

void UVerifyMetaHumanPackageSource::Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const
{
	if (!ensureAsRuntimeWarning(ToVerify) || !ensureAsRuntimeWarning(Report))
	{
		return;
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(ToVerify->GetName()));

	const UPackage* RootPackage = ToVerify->GetPackage();
	FName ThisPackage(RootPackage->GetFName());

	TArray<FName> Seeds;
	Seeds.Add(ThisPackage);

	if (UMetaHumanAssetManager::IsAssetOfType(ThisPackage, EMetaHumanAssetType::OutfitClothing) || UMetaHumanAssetManager::IsAssetOfType(ThisPackage, EMetaHumanAssetType::SkeletalClothing) || UMetaHumanAssetManager::IsAssetOfType(ThisPackage, EMetaHumanAssetType::Groom))
	{
		const FName WardrobeItemPackage = UMetaHumanAssetManager::GetWardrobeItemPackage(ThisPackage);
		if (!WardrobeItemPackage.IsNone())
		{
			// WI validity is the type-specific verifier's responsibility, not ours.
			Seeds.Add(WardrobeItemPackage);
		}
	}

	const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	UE::MetaHuman::DependencyWalker::WalkDependencies(Seeds,
		[ToVerify, Report, &Args, &AssetRegistry]
		(const FName& SourcePackage, const FName& Dependency) -> UE::MetaHuman::DependencyWalker::EVisitResult

	{
		using EVisitResult = UE::MetaHuman::DependencyWalker::EVisitResult;

		Args.Add(TEXT("SourceName"), FText::FromName(SourcePackage));
		Args.Add(TEXT("DependencyName"), FText::FromName(Dependency));
		Args.Add(TEXT("ShortDependencyName"), FText::FromString(FPaths::GetCleanFilename(Dependency.ToString())));

		const EDependencyState AllowState = IsDependencyAllowed(Dependency.ToString());
		if (AllowState == EDependencyState::Forbidden)
		{
			UObject* SourceObject = GetMainObjectFromPackageName(SourcePackage);
			Report->AddError({FText::Format(LOCTEXT("DependencyOutOfTree", "The Asset {SourceName} is attempting to reference {DependencyName} which is not in the correct folder to be included in the package"), Args), SourceObject});
			return EVisitResult::Skip;
		}

		if (AllowState == EDependencyState::AllowedDoNotFollow)
		{
			// Engine/plugin leaf: accepted, but its deps aren't our concern.
			return EVisitResult::Skip;
		}

		// Check that referenced asset files actually exist.
		FString DependencyFilename;
		FPackageName::TryConvertLongPackageNameToFilename(Dependency.ToString(), DependencyFilename, FPackageName::GetAssetPackageExtension());
		if (!IFileManager::Get().FileExists(*DependencyFilename))
		{
			Args.Add(TEXT("DependencyFileName"), FText::FromString(DependencyFilename));
			Report->AddError({FText::Format(LOCTEXT("DependencyOnMissingAsset", "The Asset {SourceName} is attempting to reference {DependencyName} which does not seem to be a file on disk ({DependencyFileName} is missing)."), Args), ToVerify});
		}

		// Check for any asset types that need warning about among the dependencies.
		TArray<FAssetData> PackagedAssets;
		AssetRegistry.GetAssetsByPackageName(Dependency, PackagedAssets);
		for (const FAssetData& AssetData : PackagedAssets)
		{
			const UClass* AssetClass = AssetData.GetClass();
			if (!AssetClass)
			{
				continue;
			}

			if (AssetClass->IsChildOf<UTexture>())
			{
				if (UTexture* Texture = Cast<UTexture>(AssetData.GetAsset()))
				{
					// Ignore the default placeholder textures in Assemblies
					if (Texture->VirtualTextureStreaming && !AssetData.GetObjectPathString().Contains(TEXT("/Common/Lookdev_UHM/Common/Textures/Placeholders/")))
					{
						// Count all textures using VT
						Report->AddWarning({FText::Format(LOCTEXT("ItemUsesVirtualTexturesWarning", "The Texture {ShortDependencyName} uses Virtual Texture Streaming. This may not work correctly in projects which do not have Virtual Texture support enabled."), Args), Texture});
					}
				}
			}
			else if (AssetClass->IsChildOf<UMaterial>())
			{
				// Only check Materials directly defined in the package. MIs that use these
				// don't need additional warnings, and MIs that use Materials from approved
				// packages are not the package author's responsibility.
				if (UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset()))
				{
					const UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
					if (EditorOnlyData && EditorOnlyData->FrontMaterial.IsConnected())
					{
						// Count all materials that require substrate enabled
						Report->AddWarning({FText::Format(LOCTEXT("ItemUsesSubstrateMaterialsWarning", "The Material {ShortDependencyName} uses Substrate. This will not work correctly in projects which do not have Substrate support enabled."), Args), Material});
					}
				}
			}
		}

		return EVisitResult::Follow;
	});
}

#undef LOCTEXT_NAMESPACE
