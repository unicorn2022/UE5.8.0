// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ScribbleEditor : ModuleRules
    {
        public ScribbleEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"MainFrame",
					"AppFramework",
					"GraphEditor",
					"Scribble"
				}
			);

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
					"Slate",
                    "SlateCore",
                    "InputCore",
					"Engine",
					"EditorFramework",
					"UnrealEd",
                    "EditorStyle",
					"EditorWidgets",
                    "ApplicationCore",
                    "PropertyEditor",
				}
            );
        }
    }
}
