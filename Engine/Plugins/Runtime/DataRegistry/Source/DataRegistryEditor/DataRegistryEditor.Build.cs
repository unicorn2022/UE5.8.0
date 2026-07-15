// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DataRegistryEditor : ModuleRules
	{
		public DataRegistryEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetDefinition",
					"Core",
					"CoreUObject",
					"Engine",
					"GameplayTags",
					"DataRegistry",
					"GameplayTagsEditor",
					"DataTableEditor",
					"InputCore",
					"PropertyEditor",
					"Slate",
					"SlateCore",					
					"BlueprintGraph",
					"Kismet",
					"KismetCompiler",
					"GraphEditor",
					"UnrealEd",
					"WorkspaceMenuStructure",
					"ContentBrowser",
					"EditorWidgets",
					"ApplicationCore",
				}
			);
		}
	}
}
