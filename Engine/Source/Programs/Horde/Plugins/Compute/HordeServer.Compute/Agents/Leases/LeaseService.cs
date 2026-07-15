// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Telemetry;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace HordeServer.Agents.Leases
{
	/// <summary>
	/// Service that emits telemetry when leases complete.
	/// </summary>
	public class LeaseService(
		LeaseCollection leaseCollection,
		ITelemetryWriter telemetryWriter,
		IOptionsMonitor<ComputeConfig> computeConfig,
		ILogger<LeaseService> logger)
		: IHostedService
	{
		private readonly ILogger _logger = logger;

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken)
		{
			leaseCollection.OnLeaseComplete += OnLeaseComplete;
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task StopAsync(CancellationToken cancellationToken)
		{
			leaseCollection.OnLeaseComplete -= OnLeaseComplete;
			return Task.CompletedTask;
		}

		private void OnLeaseComplete(ILease lease)
		{
			if (!telemetryWriter.Enabled)
			{
				return;
			}

			EpicGames.Horde.Telemetry.TelemetryStoreId telemetryStoreId = computeConfig.CurrentValue.TelemetryStoreId;
			if (telemetryStoreId.IsEmpty)
			{
				return;
			}

			try
			{
				telemetryWriter.WriteEvent(telemetryStoreId, new LeaseCompleteTelemetry(lease));
			}
			catch (Exception ex)
			{
				_logger.LogError(ex, "Failed to write lease completion telemetry for lease {LeaseId}", lease.Id);
			}
		}
	}
}
