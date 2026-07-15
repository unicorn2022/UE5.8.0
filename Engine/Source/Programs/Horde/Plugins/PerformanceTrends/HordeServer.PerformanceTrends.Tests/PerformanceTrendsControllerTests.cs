// Copyright Epic Games, Inc. All Rights Reserved.

using System.Security.Claims;
using EpicGames.Analytics;
using EpicGames.Analytics.PerformanceTrends;
using EpicGames.Horde.PerformanceTrends;
using HordeServer.PerformanceTrends;
using HordeServer.PerformanceTrends.Responses;
using HordeServer.Streams;
using HordeServer.Users;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using Moq;

namespace HordeServer.Epic.EpicSandbox.Tests.PerformanceTrends
{
	[TestClass]
	public class PerformanceTrendsControllerTests
	{
		private Mock<IPerformanceTrendsService> _mockService = null!;
		private Mock<IPerformanceBudgetCollection> _mockBudgetCollection = null!;
		private Mock<IUserCollection> _mockUserCollection = null!;
		private Mock<IOptionsSnapshot<PerformanceTrendsConfig>> _mockPerfConfig = null!;
		private Mock<IOptionsSnapshot<BuildConfig>> _mockBuildConfig = null!;
		private PerformanceTrendsController _controller = null!;

		[TestInitialize]
		public void Setup()
		{
			_mockService = new Mock<IPerformanceTrendsService>();
			_mockBudgetCollection = new Mock<IPerformanceBudgetCollection>();
			_mockUserCollection = new Mock<IUserCollection>();
			_mockPerfConfig = new Mock<IOptionsSnapshot<PerformanceTrendsConfig>>();
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig());
			_mockBuildConfig = new Mock<IOptionsSnapshot<BuildConfig>>();
			_mockBuildConfig.Setup(x => x.Value).Returns(new BuildConfig());

			_controller = new PerformanceTrendsController(
				_mockService.Object,
				_mockBudgetCollection.Object,
				_mockUserCollection.Object,
				_mockPerfConfig.Object,
				_mockBuildConfig.Object);

			// Set up a default user context for controller
			SetupControllerWithDefaultUser();
		}

		private void SetupControllerWithDefaultUser()
		{
			ClaimsIdentity identity = new ClaimsIdentity([new Claim("test-claim", "test-role")]);
			ClaimsPrincipal user = new ClaimsPrincipal(identity);

			_controller.ControllerContext = new ControllerContext
			{
				HttpContext = new DefaultHttpContext { User = user }
			};
		}

		#region -- GetPerformanceMetricsAsync Tests --

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_ReturnsOkWithTelemetryAsync()
		{
			// Arrange
			List<PerformanceTrendTelemetry> expectedTelemetry = new List<PerformanceTrendTelemetry>();
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.Is<string>(t => t == "KeyStats"),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(expectedTelemetry);

			// Act - Non-Horde streams (++prefix) are allowed through without ACL check
			IActionResult result = await _controller.GetPerformanceMetricsAsync("KeyStats", streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsInstanceOfType(result, typeof(OkObjectResult));
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_PassesCorrectFilterToServiceAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main" },
				count: 500,
				testProject: "TestProject1",
				testIdentity: "TestIdentity1",
				platforms: new[] { "Windows" },
				startCommitIdOrdered: 10000,
				endCommitIdOrdered: 20000);

			// Assert
			Assert.AreEqual(500, capturedFilter.RecordCount);
			Assert.AreEqual("TestProject1", capturedFilter.TestProject);
			Assert.AreEqual("TestIdentity1", capturedFilter.TestIdentity);
			Assert.AreEqual("++Fortnite+Main", capturedFilter.ComputedStreams![0]);
			Assert.AreEqual("Windows", capturedFilter.Platforms![0]);
			Assert.AreEqual(10000, capturedFilter.MinChangelist);
			Assert.AreEqual(20000, capturedFilter.MaxChangelist);
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_WithNullStreams_ReturnsBadRequestAsync()
		{
			// Act
			IActionResult result = await _controller.GetPerformanceMetricsAsync(type: "KeyStats", streams: null!);

			// Assert
			Assert.IsInstanceOfType(result, typeof(BadRequestObjectResult));
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_WithEmptyStreams_ReturnsBadRequestAsync()
		{
			// Act
			IActionResult result = await _controller.GetPerformanceMetricsAsync(type: "KeyStats", streams: Array.Empty<string>());

			// Assert
			Assert.IsInstanceOfType(result, typeof(BadRequestObjectResult));
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_WithEmptyPlatformArray_DoesNotSetPlatformAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPerformanceMetricsAsync(type: "KeyStats", streams: new[] { "++Fortnite+Main" }, platforms: Array.Empty<string>());

			// Assert
			Assert.IsNull(capturedFilter.Platforms);
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_WithMultiplePlatforms_PassesAllPlatformsAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPerformanceMetricsAsync(type: "KeyStats", streams: new[] { "++Fortnite+Main" }, platforms: new[] { "Windows", "Linux" });

			// Assert
			Assert.AreEqual("Windows", capturedFilter.Platforms?[0]);
			Assert.AreEqual("Linux", capturedFilter.Platforms?[1]);
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_NonHordeStreams_AllowedWithoutAclCheckAsync()
		{
			// Arrange - Non-Horde streams (++prefix) should be allowed through
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			IActionResult result = await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main", "++Fortnite+Release" });

			// Assert
			Assert.IsInstanceOfType(result, typeof(OkObjectResult));
		}

		#endregion -- GetPerformanceMetricsAsync Tests --

		#region -- GetPerformanceTestProjectsAsync Tests --

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_ReturnsTestProjectResponsesAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync();

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(0, result.Value.Count);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_PassesExcludeOrphanedFlagAsync()
		{
			// Arrange
			bool capturedExcludeFlag = false;
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.Callback<bool, CancellationToken>((exclude, ct) => capturedExcludeFlag = exclude)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPerformanceTestProjectsAsync(excludeOrphanedSummaryTypes: false);

			// Assert
			Assert.IsFalse(capturedExcludeFlag);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_DefaultsToExcludeOrphanedAsync()
		{
			// Arrange
			bool capturedExcludeFlag = false;
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.Callback<bool, CancellationToken>((exclude, ct) => capturedExcludeFlag = exclude)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPerformanceTestProjectsAsync();

			// Assert
			Assert.IsTrue(capturedExcludeFlag);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_WithStreamsFilter_FiltersResultsAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Release" },
					new KeyStatTelemetryRecord { TestName = "Project3", ComputedStream = "++Fortnite+Main" }
				});

			// Act - Filter to only ++Fortnite+Main
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync(
				streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count);
			Assert.IsTrue(result.Value.All(r => r.ComputedStream == "++Fortnite+Main"));
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_WithoutStreamsFilter_FiltersBasedOnAuthorizationAsync()
		{
			// Arrange - Non-Horde streams (++prefix) should all be allowed through
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Release" }
				});

			// Act - No explicit filter, but authorization still applies
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync();

			// Assert - Non-Horde streams should all pass through
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_WithNullComputedStream_IncludedInResultsAsync()
		{
			// Arrange - Results with null ComputedStream should be included
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = null },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Main" }
				});

			// Act
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync();

			// Assert - One should be included (null stream = just in time restrictions)
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(1, result.Value.Count);
		}

		#endregion -- GetPerformanceTestProjectsAsync Tests --

		#region -- GetPlatformsAsync Tests --

		[TestMethod]
		public async Task GetPlatformsAsync_WithValidSummaryType_ReturnsPlatformsAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryPlatformsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { Platform = "Windows", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { Platform = "Linux", ComputedStream = "++Fortnite+Main" }
				});

			// Act
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync("KeyStats", streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(1, result.Value.Count); // Grouped by stream
			CollectionAssert.AreEquivalent(new[] { "Windows", "Linux" }, result.Value[0].Platforms);
		}

		[TestMethod]
		public async Task GetPlatformsAsync_WithValidSummaryType_AndMultiStream_ReturnsPlatformsAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryPlatformsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { Platform = "Windows", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { Platform = "Windows", ComputedStream = "++Fortnite+Release" },
					new KeyStatTelemetryRecord { Platform = "Linux", ComputedStream = "++Fortnite+Release" }
				});

			// Act
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync("KeyStats", streams: new[] { "++Fortnite+Main", "++Fortnite+Release" });

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count); // Grouped by stream
			CollectionAssert.AreEquivalent(new[] { "Windows" }, result.Value[0].Platforms);
			Assert.IsTrue(result.Value[1].Platforms.Contains("Windows") && result.Value[1].Platforms.Contains("Linux"));

		}

		[TestMethod]
		public async Task GetPlatformsAsync_WithEmptySummaryType_ReturnsBadRequestAsync()
		{
			// Act
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync("", streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsInstanceOfType(result.Result, typeof(BadRequestObjectResult));
		}

		[TestMethod]
		public async Task GetPlatformsAsync_WithEmptyStreams_ReturnsBadRequestAsync()
		{
			// Act
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync("KeyStats", streams: Array.Empty<string>());

			// Assert
			Assert.IsInstanceOfType(result.Result, typeof(BadRequestObjectResult));
		}

		[TestMethod]
		public async Task GetPlatformsAsync_PassesCorrectFilterToServiceAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryPlatformsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, CancellationToken>((f, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPlatformsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" },
				testProject: "TestProject1",
				testIdentity: "TestIdentity1",
				testTypes: new[] { "PerfTest" });

			// Assert
			Assert.AreEqual("KeyStats", capturedFilter.SummaryName);
			Assert.AreEqual("TestProject1", capturedFilter.TestProject);
			Assert.AreEqual("TestIdentity1", capturedFilter.TestIdentity);
			Assert.AreEqual("PerfTest", capturedFilter.TestTypes![0]);
			Assert.AreEqual("++Fortnite+Main", capturedFilter.ComputedStreams![0]);
		}

		[TestMethod]
		public async Task GetPlatformsAsync_ResponseContainsCorrectOwningTestProjectAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryPlatformsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { Platform = "Windows", ComputedStream = "++Fortnite+Main" }
				});

			// Act
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" },
				testProject: "TestProject1",
				testIdentity: "TestIdentity1");

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(1, result.Value.Count);
			Assert.AreEqual("KeyStats", result.Value[0].OwningTestProject.SummaryType);
			Assert.AreEqual("TestProject1", result.Value[0].OwningTestProject.TestName);
			Assert.AreEqual("TestIdentity1", result.Value[0].OwningTestProject.TestIdentity);
			CollectionAssert.Contains(result.Value[0].Platforms, "Windows");
		}

		#endregion -- GetPlatformsAsync Tests --

		#region -- GetCommitsAsync Tests --

		[TestMethod]
		public async Task GetCommitsAsync_WithValidSummaryType_ReturnsCommitsAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryCommitsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { CommitIdOrdered = 10000, ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { CommitIdOrdered = 10001, ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { CommitIdOrdered = 10002, ComputedStream = "++Fortnite+Main" }
				});

			// Act
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync("KeyStats", streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(1, result.Value.Count); // Grouped by stream
			CollectionAssert.AreEquivalent(new[] { 10000, 10001, 10002 }, result.Value[0].CommitIds);
		}

		[TestMethod]
		public async Task GetCommitsAsync_WithValidSummaryType_AndMultiStream_ReturnsCommitsAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryCommitsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { CommitIdOrdered = 10000, ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { CommitIdOrdered = 10001, ComputedStream = "++Fortnite+Release" },
					new KeyStatTelemetryRecord { CommitIdOrdered = 10002, ComputedStream = "++Fortnite+Release" }
				});

			// Act
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync("KeyStats", streams: new[] { "++Fortnite+Main", "++Fortnite+Release" });

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count); // Grouped by stream
			CollectionAssert.AreEquivalent(new[] { 10000 }, result.Value[0].CommitIds);
			CollectionAssert.AreEquivalent(new[] { 10001, 10002 }, result.Value[1].CommitIds);
		}

		[TestMethod]
		public async Task GetCommitsAsync_WithEmptySummaryType_ReturnsBadRequestAsync()
		{
			// Act
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync("", streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsInstanceOfType(result.Result, typeof(BadRequestObjectResult));
		}

		[TestMethod]
		public async Task GetCommitsAsync_WithEmptyStreams_ReturnsBadRequestAsync()
		{
			// Act
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync("KeyStats", streams: Array.Empty<string>());

			// Assert
			Assert.IsInstanceOfType(result.Result, typeof(BadRequestObjectResult));
		}

		[TestMethod]
		public async Task GetCommitsAsync_PassesCorrectFilterToServiceAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryCommitsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, CancellationToken>((f, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetCommitsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" },
				testProject: "TestProject1",
				testIdentity: "TestIdentity1",
				platforms: ["Windows"]);

			// Assert
			Assert.AreEqual("KeyStats", capturedFilter.SummaryName);
			Assert.AreEqual("TestProject1", capturedFilter.TestProject);
			Assert.AreEqual("TestIdentity1", capturedFilter.TestIdentity);
			Assert.AreEqual("Windows", capturedFilter.Platforms![0]);
			Assert.AreEqual("++Fortnite+Main", capturedFilter.ComputedStreams![0]);
		}

		[TestMethod]
		public async Task GetCommitsAsync_ResponseContainsCorrectOwningTestProjectPlatformAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryCommitsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { CommitIdOrdered = 12345, ComputedStream = "++Fortnite+Main" }
				});

			// Act
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" },
				testProject: "TestProject1",
				testIdentity: "TestIdentity1",
				platforms: ["Windows"]);

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(1, result.Value.Count);
			TestProjectCommitResponse response = result.Value[0];
			Assert.AreEqual("KeyStats", response.OwningTestProject.SummaryType);
			Assert.AreEqual("TestProject1", response.OwningTestProject.TestName);
			Assert.AreEqual("TestIdentity1", response.OwningTestProject.TestIdentity);
			CollectionAssert.Contains(response.CommitIds, 12345);
		}

		#endregion -- GetCommitsAsync Tests --

		#region -- GetPerformanceTrendTypes Tests --

		[TestMethod]
		public void GetPerformanceTrendTypes_ReturnsTypes()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceTrendTypes())
				.Returns(new List<string> { "KeyStats", "FrameTime" });

			// Act
			ActionResult<List<string>> result = _controller.GetPerformanceTrendTypes();

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count);
			Assert.AreEqual("KeyStats", result.Value[0]);
			Assert.AreEqual("FrameTime", result.Value[1]);
		}

		[TestMethod]
		public void GetPerformanceTrendTypes_WhenNoTypes_ReturnsEmptyList()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceTrendTypes())
				.Returns(new List<string>());

			// Act
			ActionResult<List<string>> result = _controller.GetPerformanceTrendTypes();

			// Assert
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(0, result.Value.Count);
		}

		#endregion -- GetPerformanceTrendTypes Tests --

		#region -- Stream Authorization Tests --

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_NonHordeStreams_AllowedThroughAsync()
		{
			// Arrange - Non-Horde streams (++prefix) should pass through without ACL checks
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act - Using non-Horde stream format
			IActionResult result = await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main" });

			// Assert - Should succeed without ACL check
			Assert.IsInstanceOfType(result, typeof(OkObjectResult));
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_MultipleNonHordeStreams_AllFilteredToAuthorizedAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act - Multiple non-Horde streams should all pass through
			await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main", "++Fortnite+Release", "++Fortnite+QA" });

			// Assert - All streams should be in the filter (all allowed)
			Assert.IsNotNull(capturedFilter.ComputedStreams);
			Assert.AreEqual(3, capturedFilter.ComputedStreams.Length);
			CollectionAssert.Contains(capturedFilter.ComputedStreams, "++Fortnite+Main");
			CollectionAssert.Contains(capturedFilter.ComputedStreams, "++Fortnite+Release");
			CollectionAssert.Contains(capturedFilter.ComputedStreams, "++Fortnite+QA");
		}

		[TestMethod]
		public async Task GetPlatformsAsync_NonHordeStreams_AllowedThroughAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryPlatformsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsNotNull(result.Value);
		}

		[TestMethod]
		public async Task GetCommitsAsync_NonHordeStreams_AllowedThroughAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceSummaryCommitsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" });

			// Assert
			Assert.IsNotNull(result.Value);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_WithNonHordeStreamFilter_FiltersResultsAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Release" },
					new KeyStatTelemetryRecord { TestName = "Project3", ComputedStream = "++Fortnite+Main" }
				});

			// Act - Filter with non-Horde streams (should be allowed)
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync(
				streams: new[] { "++Fortnite+Main" });

			// Assert - Only projects matching the filter should be returned
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count);
			Assert.IsTrue(result.Value.All(r => r.ComputedStream == "++Fortnite+Main"));
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_WithMultipleStreams_FiltersCorrectlyAsync()
		{
			// Arrange
			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Release" },
					new KeyStatTelemetryRecord { TestName = "Project3", ComputedStream = "++Fortnite+QA" }
				});

			// Act - Filter with multiple streams
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync(
				streams: new[] { "++Fortnite+Main", "++Fortnite+QA" });

			// Assert - Only projects matching any of the filter streams should be returned
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(2, result.Value.Count);
			Assert.IsTrue(result.Value.Any(r => r.ComputedStream == "++Fortnite+Main"));
			Assert.IsTrue(result.Value.Any(r => r.ComputedStream == "++Fortnite+QA"));
			Assert.IsFalse(result.Value.Any(r => r.ComputedStream == "++Fortnite+Release"));
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_StreamsWithEmptyValues_FiltersOutEmptyStringsAsync()
		{
			// Arrange
			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act - Mix of valid and empty stream values
			await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main", "", "++Fortnite+Release" });

			// Assert - Empty strings should be filtered out
			Assert.IsNotNull(capturedFilter.ComputedStreams);
			Assert.AreEqual(2, capturedFilter.ComputedStreams.Length);
			CollectionAssert.Contains(capturedFilter.ComputedStreams, "++Fortnite+Main");
			CollectionAssert.Contains(capturedFilter.ComputedStreams, "++Fortnite+Release");
		}

		#endregion -- Stream Authorization Tests --

		#region -- DisableStreamBasedAuth Config Tests --

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_DisableStreamBasedAuth_AllStreamsAllowedAsync()
		{
			// Arrange - Enable DisableStreamBasedAuth killswitch
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { DisableStreamBasedAuth = true });

			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act - Mix of Horde and non-Horde streams - all should be allowed when killswitch is on
			IActionResult result = await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main", "/fortnite/main", "some-random-stream" });

			// Assert - All streams should pass through
			Assert.IsInstanceOfType(result, typeof(OkObjectResult));
			Assert.IsNotNull(capturedFilter.ComputedStreams);
			Assert.AreEqual(3, capturedFilter.ComputedStreams.Length);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_DisableStreamBasedAuth_AllResultsReturnedAsync()
		{
			// Arrange - Enable DisableStreamBasedAuth killswitch
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { DisableStreamBasedAuth = true });

			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "/fortnite/main" },
					new KeyStatTelemetryRecord { TestName = "Project3", ComputedStream = "unauthorized-stream" },
					new KeyStatTelemetryRecord { TestName = "Project4", ComputedStream = null }
				});

			// Act - No filter, all results should be returned when killswitch is on
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync();

			// Assert - All results should be returned regardless of stream authorization
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(4, result.Value.Count);
		}

		[TestMethod]
		public async Task GetPlatformsAsync_DisableStreamBasedAuth_AllStreamsAllowedAsync()
		{
			// Arrange - Enable DisableStreamBasedAuth killswitch
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { DisableStreamBasedAuth = true });

			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryPlatformsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, CancellationToken>((f, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetPlatformsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "/fortnite/main", "unauthorized-stream" });

			// Assert - All streams should pass through
			Assert.IsNotNull(capturedFilter.ComputedStreams);
			Assert.AreEqual(2, capturedFilter.ComputedStreams.Length);
		}

		[TestMethod]
		public async Task GetCommitsAsync_DisableStreamBasedAuth_AllStreamsAllowedAsync()
		{
			// Arrange - Enable DisableStreamBasedAuth killswitch
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { DisableStreamBasedAuth = true });

			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryCommitsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, CancellationToken>((f, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act
			await _controller.GetCommitsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "/fortnite/main", "unauthorized-stream" });

			// Assert - All streams should pass through
			Assert.IsNotNull(capturedFilter.ComputedStreams);
			Assert.AreEqual(2, capturedFilter.ComputedStreams.Length);
		}

		#endregion -- DisableStreamBasedAuth Config Tests --

		#region -- HideExternalResults Config Tests --

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_HideExternalResults_NonHordeStreamsFilteredOutAsync()
		{
			// Arrange - Enable HideExternalResults to block non-Horde streams
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { HideExternalResults = true });

			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act - Only non-Horde streams (++prefix) - should all be filtered out
			IActionResult result = await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main", "++Fortnite+Release" });

			// Assert - All streams filtered out, should return error (not OK)
			Assert.IsNotInstanceOfType(result, typeof(OkObjectResult), "Should not return OK when all streams are unauthorized");
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_HideExternalResults_NonHordeResultsFilteredAsync()
		{
			// Arrange - Enable HideExternalResults to block non-Horde streams
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { HideExternalResults = true });

			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = "++Fortnite+Main" },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Release" }
				});

			// Act - No filter, results with ++prefix streams should be filtered out
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync();

			// Assert - All non-Horde results should be filtered out
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(0, result.Value.Count);
		}

		[TestMethod]
		public async Task GetPerformanceTestProjectsAsync_HideExternalResults_NullStreamResultsFilteredAsync()
		{
			// Arrange - Enable HideExternalResults
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { HideExternalResults = true });

			_mockService.Setup(s => s.GetPerformanceTrendTestProjectsAsync(
				It.IsAny<bool>(),
				It.IsAny<CancellationToken>()))
				.ReturnsAsync(new List<PerformanceTrendTelemetry>
				{
					new KeyStatTelemetryRecord { TestName = "Project1", ComputedStream = null },
					new KeyStatTelemetryRecord { TestName = "Project2", ComputedStream = "++Fortnite+Main" }
				});

			// Act
			ActionResult<List<TestProjectResponse>> result = await _controller.GetPerformanceTestProjectsAsync();

			// Assert - Both null and non-Horde streams should be filtered out
			Assert.IsNotNull(result.Value);
			Assert.AreEqual(0, result.Value.Count);
		}

		[TestMethod]
		public async Task GetPlatformsAsync_HideExternalResults_NonHordeStreamsReturnsErrorAsync()
		{
			// Arrange - Enable HideExternalResults
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { HideExternalResults = true });

			// Act - Only non-Horde streams
			ActionResult<List<TestProjectPlatformResponse>> result = await _controller.GetPlatformsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" });

			// Assert - Should return error (Forbid) since all streams filtered out
			Assert.IsNotNull(result.Result, "Expected an error result when all streams are filtered out");
			Assert.IsNull(result.Value, "Should not return a value when authorization fails");
		}

		[TestMethod]
		public async Task GetCommitsAsync_HideExternalResults_NonHordeStreamsReturnsErrorAsync()
		{
			// Arrange - Enable HideExternalResults
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig { HideExternalResults = true });

			// Act - Only non-Horde streams
			ActionResult<List<TestProjectCommitResponse>> result = await _controller.GetCommitsAsync(
				metricSummaryType: "KeyStats",
				streams: new[] { "++Fortnite+Main" });

			// Assert - Should return error (Forbid) since all streams filtered out
			Assert.IsNotNull(result.Result, "Expected an error result when all streams are filtered out");
			Assert.IsNull(result.Value, "Should not return a value when authorization fails");
		}

		[TestMethod]
		public async Task GetPerformanceMetricsAsync_HideExternalResults_DisableStreamBasedAuth_BothConfigsInteractAsync()
		{
			// Arrange - Enable both configs: DisableStreamBasedAuth should take precedence
			_mockPerfConfig.Setup(x => x.Value).Returns(new PerformanceTrendsConfig
			{
				DisableStreamBasedAuth = true,
				HideExternalResults = true
			});

			PerformanceTrendFilter capturedFilter = default;
			_mockService.Setup(s => s.GetPerformanceSummaryRecordsAsync(
				It.IsAny<PerformanceTrendFilter>(),
				It.IsAny<string>(),
				It.IsAny<CancellationToken>()))
				.Callback<PerformanceTrendFilter, string, CancellationToken>((f, t, c) => capturedFilter = f)
				.ReturnsAsync(new List<PerformanceTrendTelemetry>());

			// Act - DisableStreamBasedAuth should bypass HideExternalResults
			IActionResult result = await _controller.GetPerformanceMetricsAsync(
				type: "KeyStats",
				streams: new[] { "++Fortnite+Main" });

			// Assert - DisableStreamBasedAuth takes precedence, stream should be allowed
			Assert.IsInstanceOfType(result, typeof(OkObjectResult));
			Assert.IsNotNull(capturedFilter.ComputedStreams);
			Assert.AreEqual(1, capturedFilter.ComputedStreams.Length);
		}

		#endregion -- HideExternalResults Config Tests --
	}
}
