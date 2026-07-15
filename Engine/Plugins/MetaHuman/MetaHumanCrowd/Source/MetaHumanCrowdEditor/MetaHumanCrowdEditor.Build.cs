// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class MetaHumanCrowdEditor : ModuleRules
{
	public MetaHumanCrowdEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"AnimationBlueprintLibrary",
			"Core",
			"CoreUObject",
			"Engine",
			"ChaosClothAssetEngine",
			"ChaosOutfitAssetEngine",
			"DataflowEngine",
			"HairStrandsCore",
			"ImageCore",
			"MeshDescription",
			"MeshBoneReduction",
			"MetaHumanCharacter",
			"MetaHumanCharacterEditor",
			"MetaHumanCharacterPalette",
			"MetaHumanCharacterPaletteEditor",
			"MetaHumanDefaultPipeline",
			"MetaHumanDefaultEditorPipeline",
			"MetaHumanCrowd",
			"RHI",
			"RigLogicModule",
			"SkeletalMeshDescription",
			"SkeletalMeshModifiers",
			"SkeletalMeshUtilitiesCommon",
			"StaticMeshDescription",
			"DerivedDataCache",
			"DNACalibModule",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"ContentBrowser",
			"Slate",
			"SlateCore",
			"ToolWidgets",
			"UnrealEd",
		});
	}
}
