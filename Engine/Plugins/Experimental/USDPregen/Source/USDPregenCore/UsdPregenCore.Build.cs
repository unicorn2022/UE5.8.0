// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;

namespace UnrealBuildTool.Rules
{
	public class USDPregenCore : ModuleRules
	{
		public USDPregenCore(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;
			bLegalToDistributeObjectCode = true;
			bUseUnity = false;
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"UnrealUSDWrapper",
				}
			);

			UnrealUSDWrapper.CheckAndSetupUsdSdk(Target, this);
		}
	}
}