// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.PerformanceTrends;

namespace HordeServer.Epic.EpicSandbox.Tests.PerformanceTrends
{
	[TestClass]
	public class KeyStatsPerformanceSummaryHandlerTests
	{
		[TestMethod]
		public void PerformanceSummaryType_ReturnsKeyStats()
		{
			// Arrange
			KeyStatsPerformanceSummaryHandler handler = new KeyStatsPerformanceSummaryHandler();

			// Act
			string summaryType = handler.PerformanceSummaryType;

			// Assert
			Assert.AreEqual("KeyStats", summaryType);
		}
	}
}
