// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SpeedTreeImporter : ModuleRules
	{
		public SpeedTreeImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
				    "CoreUObject",
				    "Engine",
				    "Slate",
					"SlateCore",
                    "InputCore",
					"EditorFramework",
                    "UnrealEd",
                    "MainFrame",
                    "MeshDescription",
					"StaticMeshDescription"
                }
				);
				
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SpeedTree");

			if (Target.bUseHeaderUnitsForPch)
			{
				PCHUsage = PCHUsageMode.NoPCHs; // PortableCrt.h in Speedtree uses localtime_s
			}
		}
	}
}
