// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PlainPropsUObject : ModuleRules
{
	public PlainPropsUObject(ReadOnlyTargetRules Target) : base(Target)
	{
		bDisableStaticAnalysis = true;
		bValidateInternalApi = false; // for UE_INTERNAL FLinkerLoad::ResolveResource access

		CppStandard = CppStandardVersion.Cpp20;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"CorePreciseFP",
				"JsonObjectGraph",	// for UE::JsonObjectGraph::Stringify for debugging
				"PlainProps"
		});
	}
}
