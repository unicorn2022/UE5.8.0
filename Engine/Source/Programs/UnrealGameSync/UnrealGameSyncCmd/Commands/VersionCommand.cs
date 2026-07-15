// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using UnrealGameSyncCmd.Utils;

namespace UnrealGameSyncCmd.Commands
{
	internal class VersionCommand : ICommand
	{
		public static void Execute(ILogger logger)
		{
			string version = VersionUtils.GetVersion();
			logger.LogInformation("UnrealGameSync {Version}", version);

		}
	}
}
