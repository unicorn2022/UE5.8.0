// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;

using UnrealGameSync;
using UnrealGameSyncCmd.Options;
using UnrealGameSyncCmd.Utils;

using UserErrorException = UnrealGameSyncCmd.Exceptions.UserErrorException;

namespace UnrealGameSyncCmd.Commands
{
	internal class InitCommand : ICommand
	{
		public static async Task ExecuteAsync(ILogger logger, CommandLineArguments args, ILoggerFactory loggerFactory, UserSettings? settings)
		{
			// Get the positional argument indicating the file to look for
			string? initName;
			args!.TryGetPositionalArgument(out initName);

			// Get the config settings from the command line
			InitCommandOptions options = new InitCommandOptions();
			args.ApplyTo(options);
			args.CheckAllArgumentsUsed();

			// do not use the command context perforce as it relies on the .ugs/settings files to exist
			using IPerforceConnection perforce = await PerforceConnectionUtils.ConnectAsync(options.ServerAndPort, options.UserName, null, loggerFactory);

			// Get the host name
			InfoRecord perforceInfo = await perforce.GetInfoAsync(InfoOptions.ShortOutput);
			string hostName = perforceInfo.ClientHost ?? Dns.GetHostName();

			// Create the perforce connection
			if (initName != null)
			{
				await InitNewClientAsync(perforce, initName, hostName, options, settings, logger);
			}
			else
			{
				await InitExistingClientAsync(perforce, hostName, options, settings, logger);
			}
		}

		private static async Task InitNewClientAsync(IPerforceConnection perforce, string streamName, string hostName, InitCommandOptions options, UserSettings? globalSettings, ILogger logger)
		{
			logger.LogInformation("Checking stream...");

			// Get the given stream
			PerforceResponse<StreamRecord> streamResponse = await perforce.TryGetStreamAsync(streamName, true);
			if (!streamResponse.Succeeded)
			{
				throw new UserErrorException($"Unable to find stream '{streamName}'");
			}
			StreamRecord stream = streamResponse.Data;

			// Get the new directory for the client
			DirectoryReference clientDir = DirectoryReference.FromString(options.ClientRoot) ?? DirectoryReference.Combine(DirectoryReference.GetCurrentDirectory(), stream.Stream.Replace('/', '+'));
			DirectoryReference.CreateDirectory(clientDir);

			// Make up a new client name 
			string clientName = options.ClientName ?? Regex.Replace($"{perforce.Settings.UserName}_{hostName}_{stream.Stream.Trim('/')}", "[^0-9a-zA-Z_.-]", "+");

			ClientRecord? existingClient = null;
			if (!options.IgnoreExistingClients)
			{
				// Check there are no existing clients under the current path
				List<ClientsRecord> clients = await FindExistingClients(perforce, hostName, clientDir);
				if (clients.Count > 0)
				{
					if (clients.Count == 1 && clientName.Equals(clients[0].Name, StringComparison.OrdinalIgnoreCase) && clientDir == TryParseRoot(clients[0].Root))
					{
						logger.LogInformation("Reusing existing client for {ClientDir} ({ClientName})", clientDir, options.ClientName);
						existingClient = await perforce.GetClientAsync(clients[0].Name);
					}
					else
					{
						throw new UserErrorException("Current directory is already within a Perforce workspace ({ClientName})", clients[0].Name);
					}
				}
			}

			// Create the new client
			ClientRecord client = existingClient ?? new ClientRecord(clientName, perforce.Settings.UserName, clientDir.FullName);

			client.Host = hostName;
			client.Stream = stream.Stream;
			client.Options = ClientOptions.Rmdir;
			await perforce.CreateClientAsync(client);

			// Branch root is currently hard-coded at the root
			string branchPath = options.BranchPath ?? String.Empty;
			string projectPath = await ProjectUtils.FindProjectPathAsync(perforce, clientName, branchPath, options.ProjectName);

			PerforceSettings psettings = new PerforceSettings(PerforceSettings.Default);
			psettings.ClientName = clientName;
			psettings.PreferNativeClient = true;
			if (!String.IsNullOrEmpty(options.ServerAndPort))
			{
				psettings.ServerAndPort = options.ServerAndPort;
			}
			if (!String.IsNullOrEmpty(options.UserName))
			{
				psettings.UserName = options.UserName;
			}

			using IPerforceConnection clientPerforce = await PerforceConnection.CreateAsync(psettings, logger);

			// get the client path to the project file
			List<WhereRecord> records = await clientPerforce.WhereAsync($"//{client.Name}{projectPath}").Where(x => !x.Unmap).ToListAsync();
			if (records.Count == 0)
			{
				throw new UserErrorException("Unable to find project file for {ClientName} at {ProjectPath}", client.Name, projectPath);
			}

			// Create the settings object
			UserWorkspaceSettings settings = new UserWorkspaceSettings();
			settings.RootDir = clientDir;
			settings.Init(clientPerforce.Settings.ServerAndPort, clientPerforce.Settings.UserName, clientName, branchPath, projectPath, records[0].Path);
			options.ApplyTo(settings);
			settings.Save(logger);

			await ApplySettingsFromConfig(logger, clientPerforce, settings);

			if (globalSettings != null)
			{
				UserSelectedProjectSettings selectedProjectSettings =
					new UserSelectedProjectSettings(
						null,
						null,
						UserSelectedProjectType.Client,
						$"//{client.Name}{projectPath}",
						$"{client.Root}{projectPath}".Replace("/", @"\", StringComparison.Ordinal));
				globalSettings.OpenProjects.Add(selectedProjectSettings);
				globalSettings.RecentProjects.Add(selectedProjectSettings);
				globalSettings.Save(logger);
			}

			logger.LogInformation("Initialized {ClientName} with root at {RootDir}", clientName, clientDir);
		}

		private static DirectoryReference? TryParseRoot(string root)
		{
			try
			{
				return new DirectoryReference(root);
			}
			catch
			{
				return null;
			}
		}

		private static async Task InitExistingClientAsync(IPerforceConnection perforce, string hostName, InitCommandOptions options, UserSettings? globalSettings, ILogger logger)
		{
			DirectoryReference currentDir = DirectoryReference.GetCurrentDirectory();

			// Make sure the client name is set
			string? clientName = options.ClientName;
			if (clientName == null)
			{
				List<ClientsRecord> clients = await FindExistingClients(perforce, hostName, currentDir);
				if (clients.Count == 0)
				{
					throw new UserErrorException("Unable to find client for {HostName} under {ClientDir}", hostName, currentDir);
				}
				if (clients.Count > 1)
				{
					throw new UserErrorException("Multiple clients found for {HostName} under {ClientDir}: {ClientList}", hostName, currentDir, String.Join(", ", clients.Select(x => x.Name)));
				}

				clientName = clients[0].Name;
				logger.LogInformation("Found client {ClientName}", clientName);
			}

			// Get the client info
			ClientRecord client = await perforce.GetClientAsync(clientName);
			DirectoryReference clientDir = new DirectoryReference(client.Root);

			// If a project path was specified in local syntax, try to convert it to client-relative syntax
			string? projectName = options.ProjectName;
			if (options.ProjectName != null && options.ProjectName.Contains('.', StringComparison.Ordinal))
			{
				options.ProjectName = FileReference.Combine(currentDir, options.ProjectName).MakeRelativeTo(clientDir).Replace('\\', '/');
			}

			// Branch root is currently hard-coded at the root
			string branchPath = options.BranchPath ?? String.Empty;
			string projectPath = await ProjectUtils.FindProjectPathAsync(perforce, clientName, branchPath, projectName);

			PerforceSettings psettings = new PerforceSettings(PerforceSettings.Default);
			psettings.ClientName = clientName;
			psettings.PreferNativeClient = true;
			if (!String.IsNullOrEmpty(options.ServerAndPort))
			{
				psettings.ServerAndPort = options.ServerAndPort;
			}
			if (!String.IsNullOrEmpty(options.UserName))
			{
				psettings.UserName = options.UserName;
			}

			using IPerforceConnection clientPerforce = await PerforceConnection.CreateAsync(psettings, logger);

			// get the client path to the project file
			List<WhereRecord> records = await clientPerforce.WhereAsync($"//{client.Name}{projectPath}").Where(x => !x.Unmap).ToListAsync();
			if (records.Count == 0)
			{
				throw new UserErrorException("Unable to find project file for {ClientName} at {ProjectPath}", client.Name, projectPath);
			}

			// Create the settings object
			UserWorkspaceSettings settings = new UserWorkspaceSettings();
			settings.RootDir = clientDir;
			settings.Init(clientPerforce.Settings.ServerAndPort, clientPerforce.Settings.UserName, clientName, branchPath, projectPath, records[0].Path);
			options.ApplyTo(settings);
			settings.Save(logger);

			await ApplySettingsFromConfig(logger, clientPerforce, settings);

			if (globalSettings != null)
			{
				UserSelectedProjectSettings selectedProjectSettings =
					new UserSelectedProjectSettings(
						null,
						null,
						UserSelectedProjectType.Client,
						$"//{client.Name}{projectPath}",
						$"{client.Root}{projectPath}".Replace("/", @"\", StringComparison.Ordinal));
				globalSettings.OpenProjects.Add(selectedProjectSettings);
				globalSettings.RecentProjects.Add(selectedProjectSettings);
				globalSettings.Save(logger);
			}

			logger.LogInformation("Initialized workspace at {RootDir} for {ClientProject}", clientDir, settings.ClientProjectPath);
		}

		private static async Task<List<ClientsRecord>> FindExistingClients(IPerforceConnection perforce, string hostName, DirectoryReference clientDir)
		{
			List<ClientsRecord> matchingClients = new List<ClientsRecord>();

			List<ClientsRecord> clients = await perforce.GetClientsAsync(ClientsOptions.None, perforce.Settings.UserName);
			foreach (ClientsRecord client in clients)
			{
				if (!String.IsNullOrEmpty(client.Root) && !String.IsNullOrEmpty(client.Host) && String.Equals(hostName, client.Host, StringComparison.OrdinalIgnoreCase))
				{
					DirectoryReference? rootDir;
					try
					{
						rootDir = new DirectoryReference(client.Root);
					}
					catch
					{
						rootDir = null;
					}

					if (rootDir != null && clientDir.IsUnderDirectory(rootDir))
					{
						matchingClients.Add(client);
					}
				}
			}

			return matchingClients;
		}

		private static async Task ApplySettingsFromConfig(ILogger logger, IPerforceConnection clientPerforce, UserWorkspaceSettings settings)
		{
			// Ensure the sync base content setting is correct for UEFN projects. This is required to ensure the correct files are synced for UEFN projects, and to avoid syncing unnecessary files for non-UEFN projects.
			ProjectInfo projectInfo = await ProjectInfo.CreateAsync(clientPerforce, settings, CancellationToken.None);
			ConfigFile projectConfig = await ConfigUtils.ReadProjectConfigFileAsync(clientPerforce, projectInfo, logger, CancellationToken.None);

			if (settings.ClientProjectPath.EndsWith(".uefnproject", StringComparison.OrdinalIgnoreCase) && ConfigUtils.GetForceSyncBaseContent(projectConfig, projectInfo.ProjectIdentifier, out bool forceSyncBaseContent))
			{
				if (forceSyncBaseContent && settings.SyncBaseContent == false)
				{
					settings.SyncBaseContent = forceSyncBaseContent;
					settings.Save(logger);
				}
			}

			// ensure the default preset is selected if required. This is required to ensure the correct files are synced for UEFN projects, and to avoid syncing unnecessary files for non-UEFN projects.
			bool forceDefaultPreset = false;
			if (ConfigUtils.GetForceDefaultPreset(projectConfig, projectInfo.ProjectIdentifier, out bool configForceDefaultPreset))
			{
				forceDefaultPreset = configForceDefaultPreset;
			}

			string defaultPreset = String.Empty;
			if (ConfigUtils.GetDefaultPreset(projectConfig, projectInfo.ProjectIdentifier, out string configDefaultPreset))
			{
				defaultPreset = configDefaultPreset;
			}

			if (forceDefaultPreset && !String.IsNullOrWhiteSpace(defaultPreset))
			{
				if (ConfigUtils.IsPresetAvailable(projectConfig, projectInfo.ProjectIdentifier, defaultPreset))
				{
					settings.Preset = defaultPreset;
					settings.Save(logger);
				}
			}
		}
	}
}
