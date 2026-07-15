// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using UnrealGameSync;
using UnrealGameSyncCmd.Utils;

using UserErrorException = UnrealGameSyncCmd.Exceptions.UserErrorException;

namespace UnrealGameSyncCmd.Commands
{
	internal class SyncCommandOptions
	{
		[CommandLine("-Clean")]
		public bool Clean { get; set; }

		[CommandLine("-Build")]
		public bool Build { get; set; }

		[CommandLine("-Binaries")]
		public bool Binaries { get; set; }

		[CommandLine("-NoGPF", Value = "false")]
		[CommandLine("-NoProjectFiles", Value = "false")]
		public bool ProjectFiles { get; set; } = true;

		[CommandLine("-Clobber")]
		public bool Clobber { get; set; }

		[CommandLine("-Refilter")]
		public bool Refilter { get; set; }

		[CommandLine("-Only")]
		public bool SingleChange { get; set; }

		[CommandLine("-ArchiveSources", ListSeparator = ';')]
		public List<string> ArchiveSources { get; set; } = new List<string>();
	}

	internal class SyncCommand : Command
	{
		private static async Task<bool> IsCodeChangeAsync(IPerforceConnection perforce, int change)
		{
			DescribeRecord describeRecord = await perforce.DescribeAsync(change);
			return IsCodeChange(describeRecord);
		}

		private static bool IsCodeChange(DescribeRecord describeRecord)
		{
			foreach (DescribeFileRecord file in describeRecord.Files)
			{
				if (PerforceUtils.CodeExtensions.Any(extension => file.DepotFile.EndsWith(extension, StringComparison.OrdinalIgnoreCase)))
				{
					return true;
				}
			}
			return false;
		}

		public override async Task ExecuteAsync(CommandContext context)
		{
			ILogger logger = context.Logger!;
			UserSettings? globalSettings = context.GlobalSettings;
			UserProjectSettings projectSettings = context.UserProjectSettings!;
			UserWorkspaceSettings settings = context.UserWorkspaceSettings!;
			IPerforceConnection perforceClient = context.PerforceClient!;
			WorkspaceStateWrapper state = context.WorkspaceState!;
			ProjectInfo projectInfo = context.ProjectInfo!;
			ConfigFile projectConfig = context.ProjectConfig!;
			GlobalSettingsFile userSettings = context.UserSettings!;

			context.Arguments!.TryGetPositionalArgument(out string? changeString);
			changeString ??= "latest";

			SyncCommandOptions syncOptions = new SyncCommandOptions();
			context.Arguments.ApplyTo(syncOptions);
			context.Arguments.CheckAllArgumentsUsed();

			bool syncLatest = String.Equals(changeString, "latest", StringComparison.OrdinalIgnoreCase);

			int change;
			if (!Int32.TryParse(changeString, out change))
			{
				if (syncLatest)
				{
					List<ChangesRecord> changes = await perforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, $"//{settings.ClientName}/...");
					change = changes[0].Number;
				}
				else
				{
					throw new UserErrorException("Unknown change type for sync '{Change}'", changeString);
				}
			}

			WorkspaceUpdateOptions options = syncOptions.SingleChange ? WorkspaceUpdateOptions.SyncSingleChange : WorkspaceUpdateOptions.Sync;
			if (syncOptions.Clean)
			{
				options |= WorkspaceUpdateOptions.Clean;
			}
			if (syncOptions.Build)
			{
				options |= WorkspaceUpdateOptions.Build;
			}
			if (syncOptions.ProjectFiles)
			{
				options |= WorkspaceUpdateOptions.GenerateProjectFiles;
			}
			if (syncOptions.Clobber)
			{
				options |= WorkspaceUpdateOptions.Clobber;
			}
			if (syncOptions.Refilter)
			{
				options |= WorkspaceUpdateOptions.Refilter;
			}
			options |= WorkspaceUpdateContext.GetOptionsFromConfig(userSettings.Global, settings);
			options |= WorkspaceUpdateOptions.RemoveFilteredFiles;

			// add the base content for uefn projects
			if (projectInfo.IsUEFNProject && settings.SyncBaseContent)
			{
				options |= WorkspaceUpdateOptions.SyncBaseContent;
			}

			string[] syncFilter = SyncFilterUtils.ReadSyncFilter(settings, projectSettings, userSettings, projectConfig, state.Current.ProjectIdentifier);

			using WorkspaceLock? workspaceLock = CreateWorkspaceLock(settings.RootDir);
			if (workspaceLock != null && !await workspaceLock.TryAcquireAsync())
			{
				logger.LogError("Another process is already syncing this workspace.");
				return;
			}

			WorkspaceUpdateContext updateContext = new WorkspaceUpdateContext(context.ServiceProvider, globalSettings, settings, projectSettings, change, options, BuildConfig.Development, syncFilter, projectSettings.BuildSteps, null);
			updateContext.PerforceSyncOptions = userSettings.Global.Perforce;

			if (syncOptions.Binaries)
			{
				if (context.UserProjectSettings != null && context.UserProjectSettings.ArchivesDeactivated)
				{
					logger.LogWarning("Binaries sync is deactivated in the project settings.");
				}
				else
				{
					HashSet<string> archiveSources = BaseArchive.DefaultArchiveSources;
					if (syncOptions.ArchiveSources.Any())
					{
						archiveSources = syncOptions.ArchiveSources.ToHashSet(StringComparer.OrdinalIgnoreCase);
					}

					List<BaseArchiveChannel> archives = await BaseArchive.EnumerateChannelsAsync(perforceClient, context.HordeClient, context.CloudStorage, projectConfig, state.Current.ProjectIdentifier, archiveSources, CancellationToken.None);

					BaseArchiveChannel? editorArchiveInfo = archives.FirstOrDefault(x => x.Type == IArchiveChannel.EditorArchiveType);
					if (editorArchiveInfo == null)
					{
						throw new UserErrorException("No editor archives found for project");
					}

					KeyValuePair<int, IArchive> revision = editorArchiveInfo.ChangeNumberToArchive.LastOrDefault(x => x.Key <= change);
					if (revision.Key == 0)
					{
						throw new UserErrorException($"No editor archives found for CL {change}");
					}

					if (revision.Key < change)
					{
						int lastChange = revision.Key;

						List<ChangesRecord> changeRecords = await perforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, $"//{settings.ClientName}/...@{revision.Key + 1},{change}");
						foreach (ChangesRecord changeRecord in changeRecords.OrderBy(x => x.Number))
						{
							if (await IsCodeChangeAsync(perforceClient, changeRecord.Number))
							{
								if (syncLatest)
								{
									updateContext.ChangeNumber = lastChange;
								}
								else
								{
									throw new UserErrorException($"No editor binaries found for CL {change} (last archive at CL {revision.Key}, but CL {changeRecord.Number} is a code change)");
								}
								break;
							}
							change = changeRecord.Number;
						}
					}

					updateContext.Options |= WorkspaceUpdateOptions.SyncArchives;
					updateContext.ArchiveTypeToArchive[IArchiveChannel.EditorArchiveType] = revision.Value;
				}
			}

			// additional paths to sync
			updateContext.AdditionalPathsToSync = ConfigUtils.GetAdditionalPathsToSync(projectConfig);

			// base content paths
			if (settings.SyncBaseContent && projectInfo.IsUEFNProject)
			{
				updateContext.BaseContentPaths = ConfigUtils.GetBaseContentPaths(projectConfig, projectInfo.ProjectIdentifier);
			}

			WorkspaceUpdate update = new WorkspaceUpdate(updateContext);
			(WorkspaceUpdateResult result, string message) = await update.ExecuteAsync(perforceClient.Settings, projectInfo, state, context.Logger!, CancellationToken.None);
			if (result == WorkspaceUpdateResult.FilesToClobber)
			{
				logger.LogWarning("The following files are modified in your workspace:");
				foreach (string file in updateContext.ClobberFiles.Keys.OrderBy(x => x))
				{
					logger.LogWarning("  {File}", file);
				}
				logger.LogWarning("Use -Clobber to overwrite");
			}

			state.Modify(x => x.SetLastSyncState(result, updateContext, message));

			if (result != WorkspaceUpdateResult.Success)
			{
				throw new UserErrorException("{Message} (Result: {Result})", message, result);
			}
		}

		static WorkspaceLock? CreateWorkspaceLock(DirectoryReference rootDir)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return new WorkspaceLock(rootDir);
			}
			else
			{
				return null;
			}
		}
	}
}
