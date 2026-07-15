// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SandboxedEditing : ModuleRules
	{
		public SandboxedEditing(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",  
				}
			);
			
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetTools",
					"ContentBrowser",
					"DesktopPlatform",
					"Engine",
					"FileSandboxCore",
					"FileSandboxUI",
					"FileUtilities",
					"InputCore",
					"NamingTokens",
					"Slate",
					"SlateCore",
					"SourceControl",
					"ToolMenus",
					"ToolWidgets",
					"UnrealEd",
					"WorkspaceMenuStructure",
				}
			);
		}
	}
}
