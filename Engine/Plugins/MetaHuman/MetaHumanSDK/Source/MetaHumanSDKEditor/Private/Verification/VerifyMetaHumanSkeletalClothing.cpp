// Copyright Epic Games, Inc. All Rights Reserved.

#include "Verification/VerifyMetaHumanSkeletalClothing.h"

#include "MetaHumanAssetReport.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"
#include "ProjectUtilities/MetaHumanDependencyWalker.h"
#include "Verification/MetaHumanCharacterVerification.h"

#include "Algo/AllOf.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Misc/Paths.h"
#include "Misc/RuntimeErrors.h"
#include "TextureCompiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VerifyMetaHumanSkeletalClothing)

#define LOCTEXT_NAMESPACE "VerifyMetaHumanClothing"

namespace UE::MetaHuman::Private
{
void VerifyWardrobeItem(const USkeletalMesh* SkeletalMeshAsset, UMetaHumanAssetReport* Report)
{
	FString RootFolder = FPaths::GetPath(SkeletalMeshAsset->GetPathName());

	TArray<FAssetData> TopLevelItems;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(RootFolder), TopLevelItems);

	bool bWardrobeItemFound = false;
	const bool bMetaHumanCharacterEditorLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("MetaHumanCharacterEditor"));

	for (const FAssetData& Item : TopLevelItems)
	{
		if (FPaths::GetBaseFilename(Item.PackageName.ToString()).StartsWith(TEXT("WI_")))
		{
			bWardrobeItemFound = true;
			if (bMetaHumanCharacterEditorLoaded)
			{
				FMetaHumanCharacterVerification::Get().VerifySkelMeshClothingWardrobeItem(Item.GetAsset(), SkeletalMeshAsset, Report);
			}
		}
	}

	if (bWardrobeItemFound && !bMetaHumanCharacterEditorLoaded)
	{
		Report->AddError({LOCTEXT("WardrobeItemPluginsNotLoaded", "Wardrobe Items can not be verified without the MetaHuman Creator plugin enabled. Open the Plugin Editor and enable the \"MetaHuman Creator\" plugin to allow verification of this asset.")});
	}

	// 2008 Check for MetaHuman Wardrobe Item per asset
	if (!bWardrobeItemFound)
	{
		Report->AddWarning({LOCTEXT("MissingWardrobeItem", "The package does not contain a Wardrobe Item. Certain features will not work or will be at default values")});
	}
}

void VerifySkeletalMeshClothing(TNotNull<const USkeletalMesh*> InSkeletalMesh, TNotNull<UMetaHumanAssetReport*> InReport)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("SkelMeshName"), FText::FromName(InSkeletalMesh->GetFName()));

	// 2000 Missing face culling map - Defined in the wardrobe item and by naming convention  "T_assetname_bmask"

	// 2001 LODS Incomplete:
	if (InSkeletalMesh->GetLODNum() < 4)
	{
		InReport->AddWarning(FMetaHumanAssetReportItem{ FText::Format(LOCTEXT("SkelMeshMissingLods", "{SkelMeshName} does not have at least 4 levels of detail"), Args), InSkeletalMesh });
	}

	// 2002 Contains correct skeleton
	if (const USkeleton* TargetSkeleton = InSkeletalMesh->GetSkeleton())
	{
		Args.Add(TEXT("SkeletonName"), FText::FromName(TargetSkeleton->GetFName()));
		if (!UMetaHumanAssetManager::IsMetaHumanBodyCompatibleSkeleton(TargetSkeleton))
		{
			InReport->AddError(FMetaHumanAssetReportItem{ FText::Format(LOCTEXT("SkeletonMissmatch", "The Skeleton {SkeletonName} used by {SkelMeshName} is not compatible with the MetaHuman Body Skeleton"), Args), InSkeletalMesh });
		}
	}
	else
	{
		InReport->AddError(FMetaHumanAssetReportItem{ FText::Format(LOCTEXT("SkeletonMissing", "The SkelMesh {SkelMeshName} does not have a skeleton correctly assigned"), Args), InSkeletalMesh });
	}

	// 2003 Contains appropriate vertex count
	if (InSkeletalMesh->GetLODNum() && InSkeletalMesh->GetMeshDescription(0)->Vertices().Num() > 100000)
	{
		InReport->AddWarning({ FText::Format(LOCTEXT("SkelMeshVertexCountHigh", "{SkelMeshName} has more than 100000 vertices"), Args), InSkeletalMesh });
	}

	// 2005 Has Materials. Cleared slots are force-filled with the engine default material
	// rather than left null, so detect that as "missing" too.
	const UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	if (Algo::AllOf(InSkeletalMesh->GetMaterials(), [DefaultMaterial](const FSkeletalMaterial& Material)
		{
			return Material.MaterialInterface == nullptr || Material.MaterialInterface == DefaultMaterial;
		}))
	{
		InReport->AddWarning({ FText::Format(LOCTEXT("SkelMeshMissingMaterials", "{SkelMeshName} has not got any Materials assigned"), Args), InSkeletalMesh });
	}
}

void VerifyStaticMeshClothing(TNotNull<const UStaticMesh*> InStaticMesh, TNotNull<UMetaHumanAssetReport*> InReport)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("StaticMeshName"), FText::FromName(InStaticMesh->GetFName()));

	// 2000 Missing face culling map - blocked pending discussion with tech artists

	// 2001 LODS Incomplete:
	if (InStaticMesh->GetNumLODs() < 4)
	{
		InReport->AddWarning(FMetaHumanAssetReportItem{ FText::Format(LOCTEXT("StaticMeshMissingLods", "{StaticMeshName} does not have at least 4 levels of detail"), Args), InStaticMesh });
	}

	// 2003 Contains appropriate vertex count
	if (InStaticMesh->GetNumLODs() && InStaticMesh->GetMeshDescription(0)->Vertices().Num() > 100000)
	{
		InReport->AddWarning(FMetaHumanAssetReportItem{ FText::Format(LOCTEXT("StaticMeshVertexCountHigh", "{StaticMeshName} has more than 100000 vertices"), Args), InStaticMesh });
	}

	// 2005 Has Materials. See VerifySkeletalMeshClothing for the default-material rationale.
	const UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	if (Algo::AllOf(InStaticMesh->GetStaticMaterials(), [DefaultMaterial](const FStaticMaterial& Material)
		{
			return Material.MaterialInterface == nullptr || Material.MaterialInterface == DefaultMaterial;
		}))
	{
		InReport->AddWarning(FMetaHumanAssetReportItem{ FText::Format(LOCTEXT("StaticMeshMissingMaterials", "{StaticMeshName} has not got any Materials assigned"), Args), InStaticMesh });
	}
}

} // namespace UE::MetaHuman::Private

void UVerifyMetaHumanSkeletalClothing::Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const
{
	using namespace UE::MetaHuman::Private;

	if (!ensureAsRuntimeWarning(ToVerify) || !ensureAsRuntimeWarning(Report))
	{
		return;
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(ToVerify->GetName()));
	const USkeletalMesh* SkeletalMeshAsset = Cast<USkeletalMesh>(ToVerify);
	if (!SkeletalMeshAsset)
	{
		return;
	}

	if (Options.bVerifyPackagingRules)
	{
		// Check any wardrobe items that are present
		VerifyWardrobeItem(SkeletalMeshAsset, Report);

		// Verify that all clothing assets in the package are compatible
		VerifyClothingCompatibleAssets(ToVerify, Report);
	}

	VerifySkeletalMeshClothing(SkeletalMeshAsset, Report);
}

void UVerifyMetaHumanSkeletalClothing::VerifyClothingCompatibleAssets(const UObject* ToVerify, UMetaHumanAssetReport* Report)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(ToVerify->GetName()));

	const UPackage* RootPackage = ToVerify->GetPackage();
	const FName RootPackageName(RootPackage->GetFName());

	TArray<FName> Seeds;
	Seeds.Add(RootPackageName);
	const FName WardrobeItemPackage = UMetaHumanAssetManager::GetWardrobeItemPackage(RootPackageName);
	if (!WardrobeItemPackage.IsNone())
	{
		Seeds.Add(WardrobeItemPackage);
	}

	const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	// Texture2Ds are collected and processed in a second pass so FinishCompilation can
	// batch them — async texture compilation makes GetSizeX/Y return placeholder dims.
	TArray<UTexture2D*> CandidateTextures;

	UE::MetaHuman::DependencyWalker::WalkDependencies(Seeds,
		[&AssetRegistry, Report, &CandidateTextures]
		(const FName& /*SourcePackage*/, const FName& Dependency) -> UE::MetaHuman::DependencyWalker::EVisitResult
	{
		using EVisitResult = UE::MetaHuman::DependencyWalker::EVisitResult;

		// Project content only. Engine/plugin packages are not the clothing author's
		// responsibility (VerifyMetaHumanPackageSource handles those) and descending
		// would walk the whole engine dep graph.
		if (!FPaths::IsUnderDirectory(Dependency.ToString(), TEXT("/Game")))
		{
			return EVisitResult::Skip;
		}

		TArray<FAssetData> PackagedAssets;
		AssetRegistry.GetAssetsByPackageName(Dependency, PackagedAssets);
		for (const FAssetData& AssetData : PackagedAssets)
		{
			const FString AssetName = AssetData.AssetName.ToString();
			if (AssetName.EndsWith(TEXT("CombinedSkelMesh")) || AssetName.Contains(TEXT("bodyShape")))
			{
				// Body-resize meshes used by outfit assets, not the clothing itself.
				continue;
			}

			UObject* Asset = AssetData.GetSoftObjectPath().TryLoad();
			if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset))
			{
				UE::MetaHuman::Private::VerifySkeletalMeshClothing(SkelMesh, Report);
			}
			else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
			{
				UE::MetaHuman::Private::VerifyStaticMeshClothing(StaticMesh, Report);
			}
			else if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
			{
				CandidateTextures.Add(Texture);
			}
		}

		return EVisitResult::Follow;
	});

	// 2004 Texture map resolution too high.
	// UTexture2D::GetSizeX/Y returns placeholder dimensions while async texture compilation
	// is in flight; finish the batch before reading sizes.
	if (!CandidateTextures.IsEmpty())
	{
		TArray<UTexture*> TexturesAwaitingCompile;
		TexturesAwaitingCompile.Reserve(CandidateTextures.Num());
		for (UTexture2D* Texture : CandidateTextures)
		{
			TexturesAwaitingCompile.Add(Texture);
		}
		FTextureCompilingManager::Get().FinishCompilation(TexturesAwaitingCompile);
	}

	for (const UTexture2D* Texture : CandidateTextures)
	{
		static constexpr int32 MaxDim = 4096;
		Args.Add(TEXT("TextureName"), FText::FromName(Texture->GetFName()));

		if (Texture->GetSizeX() > MaxDim || Texture->GetSizeY() > MaxDim)
		{
			Args.Add(TEXT("MaxDim"), MaxDim);
			Args.Add(TEXT("SizeX"), Texture->GetSizeX());
			Args.Add(TEXT("SizeY"), Texture->GetSizeY());
			Report->AddWarning({FText::Format(LOCTEXT("TextureSizeHigh", "{TextureName} has a dimension greater than {MaxDim} ({SizeX} x {SizeY}). This may result in very large file sizes and poor performance"), Args), Texture});
		}
	}
}

#undef LOCTEXT_NAMESPACE
