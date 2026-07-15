// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	class VisionOSToolChainSettings : IOSToolChainSettings
	{
		public VisionOSToolChainSettings(IOSProjectSettings ProjectSettings, ILogger Logger)
			: base("XROS", "XRSimulator", "xros", ProjectSettings, Logger)
		{
		}
	}
}
