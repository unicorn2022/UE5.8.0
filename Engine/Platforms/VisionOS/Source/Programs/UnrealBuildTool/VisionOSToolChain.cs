// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	class VisionOSToolChain : IOSToolChain
	{
		protected override string XnuPlatformDefine => "XNU_PLATFORM_XROS";

		public VisionOSToolChain(ReadOnlyTargetRules InTarget, VisionOSProjectSettings InProjectSettings, ILogger InLogger)
			: base(InTarget, InProjectSettings, () => new VisionOSToolChainSettings(InProjectSettings, InLogger), ClangToolChainOptions.None, InLogger)
		{
		}
	}
}
