// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer
{
	/// <summary>
	/// Configuration for the PerformanceTrends plugin
	/// </summary>
	public class PerformanceTrendsConfig : StructuredAnalyticsConfig
	{
		/// <summary>
		/// The minimum Performance Trend Schema Version to allow.
		/// </summary>
		public int? MinQuerySchemaVersion { get; set; }

		/// <summary>
		/// Whether to hide external results. A result is considered external if the source data did not originate from a Horde job.
		/// </summary>
		public bool HideExternalResults { get; set; }
	}
}
