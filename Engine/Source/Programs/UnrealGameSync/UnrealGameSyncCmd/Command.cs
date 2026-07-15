// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using UnrealGameSync;

namespace UnrealGameSyncCmd
{
	internal interface ICommand
	{

	}

	internal abstract class Command : ICommand
	{
		internal static BuildConfig EditorConfig => BuildConfig.Development;

		public abstract Task ExecuteAsync(CommandContext context);
	}
}
