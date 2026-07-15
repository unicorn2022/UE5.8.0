// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MJPEGDecoderElectra : ModuleRules
	{
		public MJPEGDecoderElectra(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"Engine",
                    "Projects",
                    "ElectraBase",
                    "ElectraSamples",
					"ElectraCodecFactory",
					"ElectraDecoders"
                });
		}
	}
}
