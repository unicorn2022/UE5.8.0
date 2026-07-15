// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class MetaHumanCrowd : ModuleRules
{
	public MetaHumanCrowd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{ 
			"Core", 
			"CoreUObject", 
			"Engine",
			"MassAIBehavior",
			"MassActors",
			"MassCommon",
			"MassCore",
			"MassCrowd",
			"MassEntity",
			"MassLOD",
			"MassMovement",
			"MassCharacterTrajectory",
			"MassNavMeshNavigation",
			"MassNavigation",
			"MassRepresentation",
			"MassSimulation",
			"MassSpawner",
			"MeshDescription",
			"MetaHumanCharacter",
			"Mover",
			"MoverMassIntegration",
			"MetaHumanCharacterPalette",
			"MetaHumanDefaultPipeline",
			"NavigationSystem",
			"MassSmartObjects",
			"PropertyBindingUtils",
			"SmartObjectsModule",
			"StateTreeModule",
		});

		// Private dependencies for the ISKM debug overlay, which is disabled in Shipping.
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"SlateIM",
				"Slate",
				"SlateCore",
			});
		}
	}
}
