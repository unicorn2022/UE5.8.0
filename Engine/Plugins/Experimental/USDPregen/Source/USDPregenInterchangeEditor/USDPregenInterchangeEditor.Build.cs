// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDPregenInterchangeEditor : ModuleRules
	{
		public USDPregenInterchangeEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseUnity = false;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"DesktopPlatform",
					"EditorFramework",
					"Engine",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"UnrealEd",
					"UnrealUSDWrapper",
					"USDPregenInterchange"
				}
			);
		}
	}
}
