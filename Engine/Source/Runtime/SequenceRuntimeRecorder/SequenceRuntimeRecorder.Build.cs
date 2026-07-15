// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SequenceRuntimeRecorder : ModuleRules
	{
		public SequenceRuntimeRecorder(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"TimeManagement",
                    "SerializedRecorderInterface",

                }
            );

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"SlateCore",
					"Slate",
					"InputCore",
					"Engine",
                }
            );
		}
	}
}
