// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using Microsoft.Extensions.Logging;

using EpicGames.Core;

using UnrealGameSync;
using UnrealGameSyncCmd.Utils;

using UserErrorException = UnrealGameSyncCmd.Exceptions.UserErrorException;

namespace UnrealGameSyncCmd.Commands
{
	internal class FilterCommandOptions
	{
		[CommandLine("-Reset")]
		public bool Reset { get; set; } = false;

		[CommandLine("-SetSyncBaseContent", Value = "true")]
		[CommandLine("-UnsetSyncBaseContent", Value = "false")]
		public bool? SyncBaseContent { get; set; } = null;

		[CommandLine("-Include=")]
		public List<string> Include { get; set; } = new List<string>();

		[CommandLine("-Exclude=")]
		public List<string> Exclude { get; set; } = new List<string>();

		[CommandLine("-View=", ListSeparator = ';')]
		public List<string>? View { get; set; }

		[CommandLine("-AddView=", ListSeparator = ';')]
		public List<string> AddView { get; set; } = new List<string>();

		[CommandLine("-RemoveView=", ListSeparator = ';')]
		public List<string> RemoveView { get; set; } = new List<string>();

		[CommandLine("-AllProjects", Value = "true")]
		[CommandLine("-OnlyCurrent", Value = "false")]
		public bool? AllProjects { get; set; } = null;

		[CommandLine("-LocalProjects", Value = "true")]
		[CommandLine("-NoExtraLocalProjects", Value = "false")]
		public bool? LocalProjects { get; set; } = null;

		[CommandLine("-GpfAllProjects", Value = "true")]
		[CommandLine("-GpfOnlyCurrent", Value = "false")]
		public bool? AllProjectsInSln { get; set; } = null;

		[CommandLine("-GpfMinimalSln", Value = "true")]
		[CommandLine("-GpfFullSln", Value = "false")]
		public bool? UprojectSpecificSln { get; set; } = null;

		[CommandLine("-Global")]
		public bool Global { get; set; }
	}

	internal class FilterCommand : Command
	{
		public override async Task ExecuteAsync(CommandContext context)
		{
			await Task.Run(() => ExecuteInternal(context));
		}

		private static void ExecuteInternal(CommandContext context)
		{
			ILogger logger = context.Logger!;
			UserWorkspaceSettings workspaceSettings = context.UserWorkspaceSettings!;
			UserProjectSettings userProjectSettings = context.UserProjectSettings!;
			WorkspaceStateWrapper workspaceState = context.WorkspaceState!;
			ConfigFile projectConfig = context.ProjectConfig!;

			Dictionary<Guid, WorkspaceSyncCategory> syncCategories = ConfigUtils.GetSyncCategories(projectConfig);

			FilterSettings globalFilter = context.UserSettings!.Global.Filter;
			FilterSettings workspaceFilter = workspaceSettings.Filter;

			IDictionary<string, Preset> roles = ConfigUtils.GetPresets(projectConfig, workspaceState.Current.ProjectIdentifier);

			roles.TryGetValue(userProjectSettings.Preset, out Preset? role);

			FilterCommandOptions options = context.Arguments!.ApplyTo<FilterCommandOptions>(logger);
			context.Arguments.CheckAllArgumentsUsed(logger);

			if (options.SyncBaseContent != null)
			{
				workspaceSettings.SyncBaseContent = options.SyncBaseContent.Value;
				workspaceSettings.Save(logger);
			}

			if (options.Global)
			{
				ApplyCommandOptions(logger, context, globalFilter, options, syncCategories.Values);
				context.UserSettings.Save(logger);
			}
			else
			{
				ApplyCommandOptions(logger, context, workspaceFilter, options, syncCategories.Values);
				workspaceSettings.Save(logger);
			}

			Dictionary<Guid, bool> globalCategories = globalFilter.GetCategories();
			Dictionary<Guid, bool> workspaceCategories = workspaceFilter.GetCategories();

			logger.LogInformation("Categories:");
			foreach (WorkspaceSyncCategory syncCategory in syncCategories.Values)
			{
				bool enabled;

				string scope = "(Default)";

				if (workspaceCategories.TryGetValue(syncCategory.UniqueId, out enabled))
				{
					scope = "(Workspace)";
				}
				else if (globalCategories.TryGetValue(syncCategory.UniqueId, out enabled))
				{
					scope = "(Global)";
				}
				else
				{
					enabled = syncCategory.Enable;
				}

				if (role != null && role.Categories.TryGetValue(syncCategory.UniqueId, out RoleCategory? roleCategory))
				{
					scope = $"(Preset: {role.Name})";
					enabled = roleCategory.Enabled;
				}

				logger.LogInformation("  {Id,30} {Enabled,3} {Scope,-9} {Name}", syncCategory.UniqueId, enabled ? "Yes" : "No", scope, syncCategory.Name);
			}

			if (globalFilter.View.Count > 0)
			{
				logger.LogInformation("");
				logger.LogInformation("Global View:");
				foreach (string line in globalFilter.View)
				{
					logger.LogInformation("  {Line}", line);
				}
			}

			if (workspaceFilter.View.Count > 0)
			{
				logger.LogInformation("");
				logger.LogInformation("Workspace View:");
				foreach (string line in workspaceFilter.View)
				{
					logger.LogInformation("  {Line}", line);
				}
			}

			if (role != null && role.Views.Count > 0)
			{
				logger.LogInformation("");
				logger.LogInformation("Preset View:");
				foreach (string line in role.Views)
				{
					logger.LogInformation("  {Line}", line);
				}
			}

			if (workspaceSettings.ClientProjectPath.EndsWith(".uefnproject", StringComparison.OrdinalIgnoreCase))
			{
				logger.LogInformation("");
				logger.LogInformation("Base Content:");
				ISet<string> baseContentPaths = ConfigUtils.GetBaseContentPaths(projectConfig, workspaceState.Current.ProjectIdentifier);

				logger.LogInformation("Sync Base Content: {Value}", workspaceSettings.SyncBaseContent ? "True" : "False");

				if (baseContentPaths.Any())
				{
					logger.LogInformation("Base Content paths to sync:");
					foreach (string baseContentPath in baseContentPaths)
					{
						logger.LogInformation("  {BaseContentPath}", baseContentPath);
					}
				}
				else
				{
					logger.LogInformation("No Base Content Available for this project.");
				}
			}
			
			string[] filter = SyncFilterUtils.ReadSyncFilter(workspaceSettings, context.UserProjectSettings!, context.UserSettings, projectConfig, workspaceState.Current.ProjectIdentifier);

			logger.LogInformation("");
			logger.LogInformation("Combined view:");
			foreach (string filterLine in filter)
			{
				logger.LogInformation("  {FilterLine}", filterLine);
			}
		}
		private static void ApplyCommandOptions(
			ILogger logger,
			CommandContext context,
			FilterSettings settings, 
			FilterCommandOptions commandOptions, 
			IEnumerable<WorkspaceSyncCategory> syncCategories)
		{
			if (context.UserWorkspaceSettings!.LocalProjectPath!.HasExtension(".uefnproject") && context.UserWorkspaceSettings!.SyncBaseContent == false)
			{
				logger.LogInformation("Sync Base Content is not active, Categories and Views are readonly");
				return;
			}

			if (commandOptions.Reset)
			{
				logger.LogInformation("Resetting settings...");
				settings.Reset();
			}

			HashSet<Guid> includeCategories = new HashSet<Guid>(commandOptions.Include.Select(x => GetCategoryId(x, syncCategories)));
			HashSet<Guid> excludeCategories = new HashSet<Guid>(commandOptions.Exclude.Select(x => GetCategoryId(x, syncCategories)));

			Guid id = includeCategories.FirstOrDefault(x => excludeCategories.Contains(x));
			if (id != Guid.Empty)
			{
				throw new UserErrorException("Category {Id} cannot be both included and excluded", id);
			}
			
			includeCategories.ExceptWith(settings.IncludeCategories);
			settings.IncludeCategories.AddRange(includeCategories);

			excludeCategories.ExceptWith(settings.ExcludeCategories);
			settings.ExcludeCategories.AddRange(excludeCategories);

			if (commandOptions.View != null)
			{
				settings.View.Clear();
				settings.View.AddRange(commandOptions.View);
			}
			if (commandOptions.RemoveView.Count > 0)
			{
				HashSet<string> viewRemove = new HashSet<string>(commandOptions.RemoveView, StringComparer.OrdinalIgnoreCase);
				settings.View.RemoveAll(x => viewRemove.Contains(x));
			}
			if (commandOptions.AddView.Count > 0)
			{
				HashSet<string> viewLines = new HashSet<string>(settings.View, StringComparer.OrdinalIgnoreCase);
				settings.View.AddRange(commandOptions.AddView.Where(x => !viewLines.Contains(x)));
			}

			settings.AllProjects = commandOptions.AllProjects ?? settings.AllProjects;
			settings.LocalProjects = commandOptions.LocalProjects ?? settings.LocalProjects;
			settings.AllProjectsInSln = commandOptions.AllProjectsInSln ?? settings.AllProjectsInSln;
			settings.UprojectSpecificSln = commandOptions.UprojectSpecificSln ?? settings.UprojectSpecificSln;
		}

		private static Guid GetCategoryId(string text, IEnumerable<WorkspaceSyncCategory> syncCategories)
		{
			Guid id;
			if (Guid.TryParse(text, out id))
			{
				return id;
			}

			WorkspaceSyncCategory? category = syncCategories.FirstOrDefault(x => x.Name.Equals(text, StringComparison.OrdinalIgnoreCase));
			if (category != null)
			{
				return category.UniqueId;
			}

			throw new UserErrorException("Unable to find category '{Category}'", text);
		}
	}
}
