// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using UnrealBuildBase;

public class Catch2Extras : ModuleRules
{
	public Catch2Extras(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

#if WITH_CATCH2
		string Catch2Version = Catch2.Catch2Version;
#else
		string Catch2Version = "v3.11.0";
#endif

		// Just setup our include path for catch_amalgamated.hpp/cpp
		PublicSystemIncludePaths.Add(Path.Combine(Unreal.EngineDirectory.FullName, "Source", "ThirdParty", "Catch2", Catch2Version, "extras"));
	}
}
