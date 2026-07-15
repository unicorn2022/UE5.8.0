// Copyright Epic Games, Inc. All Rights Reserved.

using System.Data;
using EpicGames.Analytics.Generated;
using HordeServer.Analytics;
using HordeServer.PerformanceTrends;

namespace HordeServer.Epic.EpicSandbox.Tests.PerformanceTrends
{
	[TestClass]
	public class PerformanceTrendFilterTests
	{
		#region -- RecordCount Tests --

		[TestMethod]
		public void RecordCount_WhenSetWithinRange_ReturnsSetValue()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter();

			// Act
			filter.RecordCount = 500;

			// Assert
			Assert.AreEqual(500, filter.RecordCount);
		}

		[TestMethod]
		public void RecordCount_WhenSetAboveMaximum_ClampsToMaximum()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter();

			// Act
			filter.RecordCount = 20000;

			// Assert
			Assert.AreEqual(PerformanceTrendFilter.MaximumRecordCount, filter.RecordCount);
		}

		[TestMethod]
		public void RecordCount_WhenSetBelowZero_ClampsToZero()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter();

			// Act
			filter.RecordCount = -100;

			// Assert
			Assert.AreEqual(0, filter.RecordCount);
		}

		[TestMethod]
		public void RecordCount_WhenSetToMaximum_ReturnsMaximum()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter();

			// Act
			filter.RecordCount = PerformanceTrendFilter.MaximumRecordCount;

			// Assert
			Assert.AreEqual(PerformanceTrendFilter.MaximumRecordCount, filter.RecordCount);
		}

		[TestMethod]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MSTEST0032:Assertion condition is always true", Justification = "")]
		public void MaximumRecordCount_Is10000()
		{
			// Assert
			Assert.AreEqual(10000, PerformanceTrendFilter.MaximumRecordCount);
		}

		#endregion -- RecordCount Tests --

		#region -- BuildWhereClause Tests --

		[TestMethod]
		public void BuildWhereClause_WithNoFilters_ReturnsEmptyClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter();

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.AreEqual(String.Empty, result.Clause);
			Assert.AreEqual(0, result.Parameters.Count);
		}

		[TestMethod]
		public void BuildWhereClause_WithTestProject_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				TestProject = "TestProject1"
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.TestName, StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual("TestProject1", result.Parameters[0].Value);
			Assert.AreEqual(DbType.AnsiString, result.Parameters[0].DbType);
		}

		[TestMethod]
		public void BuildWhereClause_WithSummaryName_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				SummaryName = "KeyStats"
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.SummaryName, StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual("KeyStats", result.Parameters[0].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithDataTestIdentity_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				TestIdentity = "TestIdentity1"
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.TestIdentity, StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual("TestIdentity1", result.Parameters[0].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithPlatform_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				Platforms = ["Windows"]
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.Platform, StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual("Windows", result.Parameters[0].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithComputedStreams_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				ComputedStreams = ["++Fortnite+Main"]
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains("COALESCE", StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual("++Fortnite+Main", result.Parameters[0].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithMinChangelist_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				MinChangelist = 12345
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.CommitIdOrdered, StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(">=", StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual(12345, result.Parameters[0].Value);
			Assert.AreEqual(DbType.Int32, result.Parameters[0].DbType);
		}

		[TestMethod]
		public void BuildWhereClause_WithMaxChangelist_ReturnsCorrectClause()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				MaxChangelist = 99999
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.CommitIdOrdered, StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains("<=", StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual(99999, result.Parameters[0].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithMinDate_ReturnsCorrectClause()
		{
			// Arrange
			DateTime minDate = new DateTime(2025, 1, 1, 0, 0, 0, DateTimeKind.Utc);
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				MinDate = minDate
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.StartTimestamp, StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(">=", StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual(minDate, result.Parameters[0].Value);
			Assert.AreEqual(DbType.DateTime, result.Parameters[0].DbType);
		}

		[TestMethod]
		public void BuildWhereClause_WithMaxDate_ReturnsCorrectClause()
		{
			// Arrange
			DateTime maxDate = new DateTime(2025, 12, 31, 23, 59, 59, DateTimeKind.Utc);
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				MaxDate = maxDate
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(EpicGames_Analytics_PerformanceTrends_PerformanceTrendTelemetryGen.StartTimestamp, StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains("<=", StringComparison.Ordinal));
			Assert.AreEqual(1, result.Parameters.Count);
			Assert.AreEqual(maxDate, result.Parameters[0].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithMultipleFilters_ReturnsAndJoinedClauses()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				TestProject = "TestProject1",
				Platforms = ["Windows"],
				MinChangelist = 10000,
				MaxChangelist = 20000
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.IsTrue(result.Clause.StartsWith("WHERE ", StringComparison.Ordinal));
			Assert.IsTrue(result.Clause.Contains(" AND ", StringComparison.Ordinal));
			Assert.AreEqual(4, result.Parameters.Count);
		}

		[TestMethod]
		public void BuildWhereClause_WithChangelistRange_ReturnsCorrectParameters()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				MinChangelist = 10000,
				MaxChangelist = 20000
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.AreEqual(2, result.Parameters.Count);
			Assert.AreEqual(10000, result.Parameters[0].Value);
			Assert.AreEqual(20000, result.Parameters[1].Value);
		}

		[TestMethod]
		public void BuildWhereClause_WithEmptyStringValues_DoesNotIncludeThem()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				TestProject = "",
				Platforms = null,
				ComputedStreams = [""]
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.AreEqual(String.Empty, result.Clause);
			Assert.AreEqual(0, result.Parameters.Count);
		}

		[TestMethod]
		public void BuildWhereClause_WithNullStringValues_DoesNotIncludeThem()
		{
			// Arrange
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				TestProject = null,
				Platforms = null,
				ComputedStreams = null
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.AreEqual(String.Empty, result.Clause);
			Assert.AreEqual(0, result.Parameters.Count);
		}

		[TestMethod]
		public void BuildWhereClause_ParametersInCorrectOrder()
		{
			// Arrange - parameters should be added in order: TestProject, SummaryName, TestIdentity, Platform, Branch, MinChangelist, MaxChangelist, MinDate, MaxDate
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				TestProject = "Project",
				SummaryName = "Summary",
				TestIdentity = "Source",
				Platforms = ["Windows"]
			};

			// Act
			WhereClauseResult result = filter.BuildWhereClause();

			// Assert
			Assert.AreEqual(4, result.Parameters.Count);
			Assert.AreEqual("Project", result.Parameters[0].Value);
			Assert.AreEqual("Summary", result.Parameters[1].Value);
			Assert.AreEqual("Source", result.Parameters[2].Value);
			Assert.AreEqual("Windows", result.Parameters[3].Value);
		}

		#endregion -- BuildWhereClause Tests --

		#region -- Filter Property Tests --

		[TestMethod]
		public void Filter_AllPropertiesDefaultToNullOrDefault()
		{
			// Arrange & Act
			PerformanceTrendFilter filter = new PerformanceTrendFilter();

			// Assert
			Assert.AreEqual(0, filter.RecordCount);
			Assert.IsNull(filter.SummaryName);
			Assert.IsNull(filter.Platforms);
			Assert.IsNull(filter.MinChangelist);
			Assert.IsNull(filter.MaxChangelist);
			Assert.IsNull(filter.MinDate);
			Assert.IsNull(filter.MaxDate);
			Assert.IsNull(filter.ComputedStreams);
			Assert.IsNull(filter.TestProject);
			Assert.IsNull(filter.TestIdentity);
		}

		[TestMethod]
		public void Filter_CanSetAllProperties()
		{
			// Arrange
			DateTime minDate = DateTime.UtcNow.AddDays(-7);
			DateTime maxDate = DateTime.UtcNow;

			// Act
			PerformanceTrendFilter filter = new PerformanceTrendFilter
			{
				RecordCount = 100,
				SummaryName = "KeyStats",
				Platforms = ["Windows"],
				MinChangelist = 10000,
				MaxChangelist = 20000,
				MinDate = minDate,
				MaxDate = maxDate,
				ComputedStreams = ["++Fortnite+Main"],
				TestProject = "TestProject",
				TestIdentity = "TestIdentity1"
			};

			// Assert
			Assert.AreEqual(100, filter.RecordCount);
			Assert.AreEqual("KeyStats", filter.SummaryName);
			Assert.AreEqual("Windows", filter.Platforms[0]);
			Assert.AreEqual(10000, filter.MinChangelist);
			Assert.AreEqual(20000, filter.MaxChangelist);
			Assert.AreEqual(minDate, filter.MinDate);
			Assert.AreEqual(maxDate, filter.MaxDate);
			Assert.AreEqual("++Fortnite+Main", filter.ComputedStreams[0]);
			Assert.AreEqual("TestProject", filter.TestProject);
			Assert.AreEqual("TestIdentity1", filter.TestIdentity);
		}

		#endregion -- Filter Property Tests --
	}
}
