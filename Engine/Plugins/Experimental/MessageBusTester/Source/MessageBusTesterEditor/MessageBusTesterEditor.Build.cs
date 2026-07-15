// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MessageBusTesterEditor : ModuleRules
{
	public MessageBusTesterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"EditorStyle",
 				"InputCore",
 				"MessageLog",
				"Projects",
				"RenderCore",
				"Settings",
				"Slate",
				"SlateCore",
				"MessageBusTester",
                "WorkspaceMenuStructure",
				"UdpMessaging",
				"Messaging",
				"Networking"
            }
		);
	}
}
