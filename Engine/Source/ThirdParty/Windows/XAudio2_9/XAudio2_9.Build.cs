// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class XAudio2_9 : ModuleRules
{
	public XAudio2_9(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PublicDelayLoadDLLs.Add("XAudio2_9.dll");
			PublicSystemLibraries.Add("xaudio2.lib");
			if (Target.Architecture.bIsX64)
			{
				// TODO: Wait for EOS to catch up to fully remove redist.
				RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/Windows/XAudio2_9/x64/xaudio2_9redist.dll");
			}
		}
	}
}

