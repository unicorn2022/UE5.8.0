// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class MutableRuntime : ModuleRules
    {
        public MutableRuntime(ReadOnlyTargetRules Target) : base(Target)
		{
			ShortName = "MuR";

			DefaultBuildSettings = BuildSettingsVersion.Latest;
			IWYUSupport = IWYUSupport.KeepAsIsForNow;
			//bUseUnity = false;

			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"ImageCore",
				"ClothingSystemRuntimeCommon",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
				"GeometryCore",
				"TraceLog", 
				}
			);
		}
	}
}
