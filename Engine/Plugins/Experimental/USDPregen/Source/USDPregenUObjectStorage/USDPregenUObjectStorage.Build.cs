// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;

namespace UnrealBuildTool.Rules
{
	public class USDPregenUObjectStorage : ModuleRules
	{
		public USDPregenUObjectStorage(ReadOnlyTargetRules Target) : base(Target)
		{            
			bUseRTTI = false;
			bLegalToDistributeObjectCode = true;
			bUseUnity = false;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealUSDWrapper",
					"USDPregenCore",
					"USDPregenInterchange",
					"USDPregenWrapper"
				}
			);
		}
	}
}