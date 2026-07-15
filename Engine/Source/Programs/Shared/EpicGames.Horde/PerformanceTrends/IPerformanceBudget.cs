// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Horde.Users;

namespace EpicGames.Horde.PerformanceTrends
{
	/// <summary>
	/// Describes a metric threshold within a performance budget.
	/// </summary>
	public interface IMetricThreshold
	{
		/// <summary>
		/// The test type this threshold applies to (e.g., "Perf test", "GPU Perf").
		/// </summary>
		string TestType { get; }

		/// <summary>
		/// The name of the metric this threshold is for (e.g., "gpuTimeAvg", "frametimeAvg").
		/// </summary>
		string MetricName { get; }

		/// <summary>
		/// The threshold value for this metric.
		/// </summary>
		double ThresholdValue { get; }

		/// <summary>
		/// Indicates whether a larger value is worse for this metric.
		/// True for metrics like frame time (higher = worse), false for metrics like FPS (lower = worse).
		/// </summary>
		bool LargerIsWorse { get; }
	}

	/// <summary>
	/// Describes a performance budget group containing multiple metric thresholds.
	/// </summary>
	public interface IPerformanceBudget
	{
		/// <summary>
		/// The unique identifier for this budget.
		/// </summary>
		PerformanceBudgetId Id { get; }

		/// <summary>
		/// The name of this budget group.
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Optional description of this budget group.
		/// </summary>
		string? Description { get; }

		/// <summary>
		/// Owner of the budget.
		/// </summary>
		UserId? Owner { get; }

		/// <summary>
		/// The computed stream this budget belongs to.
		/// This is typically sourced from the performance data's ComputedStream field (e.g., "++Fortnite+Main").
		/// </summary>
		string ComputedStream { get; }

		/// <summary>
		/// The test project name this budget applies to.
		/// This corresponds to the "testName" field in the performance trends data.
		/// </summary>
		string TestProject { get; }

		/// <summary>
		/// The platforms this budget applies to.
		/// Null or empty means applicable to all platforms.
		/// </summary>
		IReadOnlyList<string>? Platforms { get; }

		/// <summary>
		/// The metric thresholds defined in this budget group.
		/// </summary>
		IReadOnlyList<IMetricThreshold> Thresholds { get; }

		/// <summary>
		/// The last time this budget was updated (UTC).
		/// </summary>
		DateTime UpdateTimeUtc { get; }
	}
}
