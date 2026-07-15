// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeOpenUSDChaosClothAssetPipeline.h"

#include "InterchangeChaosClothAssetFactoryNode.h"

#include "USDProjectSettings.h"

#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeFactoryBaseNode.h"
#include "Nodes/InterchangeSourceNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeOpenUSDChaosClothAssetPipeline)

FString UInterchangeOpenUSDChaosClothAssetPipeline::GetPipelineCategory(UClass* AssetClass)
{
	return TEXT("Physics");
}

void UInterchangeOpenUSDChaosClothAssetPipeline::ExecutePipeline(
	UInterchangeBaseNodeContainer* InBaseNodeContainer,
	const TArray<UInterchangeSourceData*>& SourceDatas,
	const FString& ContentBasePath
)
{
	using namespace UE::Interchange;

	const UUsdProjectSettings* UsdProjectSettings = GetDefault<UUsdProjectSettings>();
	if (!UsdProjectSettings)
	{
		return;
	}

	const FString SubstratePreviewSurfaceMaterial = UsdProjectSettings->ReferenceSubstratePreviewSurfaceMaterial.ToString();

	// TODO: VT material support? The USDImportNode_v3 didn't list them so I didn't add them here
	TMap<FString, FString> OldToNewMaterial = {
		{UsdProjectSettings->ReferencePreviewSurfaceMaterial.ToString(), 					PreviewSurfaceMaterialReplacement.ToString()},
		{UsdProjectSettings->ReferencePreviewSurfaceTranslucentMaterial.ToString(), 		PreviewSurfaceTranslucentMaterialReplacement.ToString()},
		{UsdProjectSettings->ReferencePreviewSurfaceTwoSidedMaterial.ToString(), 			PreviewSurfaceTwoSidedMaterialReplacement.ToString()},
		{UsdProjectSettings->ReferencePreviewSurfaceTranslucentTwoSidedMaterial.ToString(), PreviewSurfaceTranslucentTwoSidedMaterialReplacement.ToString()},
		{SubstratePreviewSurfaceMaterial, 													PreviewSurfaceMaterialReplacement.ToString()},
		{UsdProjectSettings->ReferenceDisplayColorMaterial.ToString(), 						DisplayColorMaterialReplacement.ToString()}
	};

	// Use cloth specific parent materials because we want the materials to import into UEFN
	// and the default USD ones use operations that are not allowed
	//
	// TODO: Is this *needed* if we have substrate enabled? We'll use the substrate parent materials then... Presumably those
	// also use operations that are not allowed
	InBaseNodeContainer->IterateNodesOfType<UInterchangeChaosClothAssetFactoryNode>(
		[&OldToNewMaterial,
		 &SubstratePreviewSurfaceMaterial,
		 this,
		 InBaseNodeContainer](const FString& NodeUid, UInterchangeChaosClothAssetFactoryNode* Node)
		{
			// We presume the cloth factory node will declare the materials it will use as factory dependencies, and we'll search through there.
			// We could add some user attribute or tag of some kind but doing this should be simpler for now, and hopefully we can at some point
			// just remove the need to swap parent materials like this anyway?
			TSet<FString> FactoryNodeUids = Node->GetFactoryDependencies();
			for (const FString& FactoryNodeUid : FactoryNodeUids)
			{
				UInterchangeMaterialInstanceFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialInstanceFactoryNode>(
					InBaseNodeContainer->GetFactoryNode(FactoryNodeUid)
				);
				if (!MaterialFactoryNode)
				{
					continue;
				}

				FString CustomParentPath;
				if (MaterialFactoryNode->GetCustomParent(CustomParentPath))
				{
					// The USD pipeline will do its own material remapping in case substrate is enabled. Unfortunately in that case
					// it remaps to the same parent material, and uses the material factory node's properties to control twosided vs translucent.
					// This means that if we're looking at one of those materials we have to watch out for those properties too and remap
					// our reference material manually
					if (CustomParentPath == SubstratePreviewSurfaceMaterial)
					{
						bool bIsTranslucent = false;
						if (TEnumAsByte<EBlendMode> BlendMode;
							MaterialFactoryNode->GetCustomBlendMode(BlendMode) && BlendMode == EBlendMode::BLEND_TranslucentGreyTransmittance)
						{
							bIsTranslucent = true;
							MaterialFactoryNode->SetCustomBlendMode(EBlendMode::BLEND_Opaque);	  // Clear the blend mode override as our material
																								  // instance will already cover it
						}

						bool bIsTwoSided = false;
						if (MaterialFactoryNode->GetCustomTwoSided(bIsTwoSided) && bIsTwoSided)
						{
							bIsTwoSided = true;
							MaterialFactoryNode->SetCustomTwoSided(false);	  // Clear the value from the node as our different reference material
																			  // will already handle it
						}

						if (bIsTwoSided && bIsTranslucent)
						{
							MaterialFactoryNode->SetCustomParent(PreviewSurfaceTranslucentTwoSidedMaterialReplacement.ToString());
						}
						else if (bIsTwoSided)
						{
							MaterialFactoryNode->SetCustomParent(PreviewSurfaceTwoSidedMaterialReplacement.ToString());
						}
						else if (bIsTranslucent)
						{
							MaterialFactoryNode->SetCustomParent(PreviewSurfaceTranslucentMaterialReplacement.ToString());
						}
						else
						{
							MaterialFactoryNode->SetCustomParent(PreviewSurfaceMaterialReplacement.ToString());
						}
					}
					else if (FString* ReplacementPath = OldToNewMaterial.Find(CustomParentPath))
					{
						MaterialFactoryNode->SetCustomParent(*ReplacementPath);
					}
				}
			}
		}
	);
}
