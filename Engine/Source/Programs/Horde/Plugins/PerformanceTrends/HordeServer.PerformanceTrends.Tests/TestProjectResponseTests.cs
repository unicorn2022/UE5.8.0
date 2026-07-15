// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.PerformanceTrends.Responses;

namespace HordeServer.Epic.EpicSandbox.Tests.PerformanceTrends
{
	[TestClass]
	public class TestProjectResponseTests
	{
		#region -- TestProjectResponse Tests --

		[TestMethod]
		[DataRow("KeyStats", "TestProject1", "TestIdentity1", "TestType1", "++Fortnite+Main", DisplayName = "AllProperties")]
		[DataRow("KeyStats", null, "TestIdentity1", "TestType1", "", DisplayName = "NullTestName")]
		[DataRow("KeyStats", "TestProject1", null, null, null, DisplayName = "NullTestIdentity")]
		[DataRow("KeyStats", null, null, null, null, DisplayName = "AllNullableFieldsNull")]
		public void TestProjectResponse_Constructor_SetsProperties(
			string summaryType,
			string? testName,
			string? testIdentity,
			string? testType,
			string computedStream)
		{
			// Act
			TestProjectResponse response = new TestProjectResponse(summaryType, testName, testIdentity, testType, computedStream);

			// Assert
			Assert.AreEqual(summaryType, response.SummaryType);
			Assert.AreEqual(testName, response.TestName);
			Assert.AreEqual(testIdentity, response.TestIdentity);
			Assert.AreEqual(testType, response.TestType);
			Assert.AreEqual(computedStream, response.ComputedStream);
		}

		#endregion -- TestProjectResponse Tests --

		#region -- TestProjectPlatformResponse Tests --

		[TestMethod]
		public void TestProjectPlatformResponse_Constructor_SetsProperties_AllProperties()
		{
			// Arrange
			string[] platforms = new[] { "Windows", "Linux" };

			// Act
			TestProjectPlatformResponse response = new TestProjectPlatformResponse("KeyStats", "TestProject1", "TestIdentity1", "TestType1", "TestStream", platforms);

			// Assert
			Assert.IsNotNull(response.OwningTestProject);
			Assert.IsInstanceOfType(response.OwningTestProject, typeof(TestProjectResponse));
			Assert.AreEqual("KeyStats", response.OwningTestProject.SummaryType);
			Assert.AreEqual("TestProject1", response.OwningTestProject.TestName);
			Assert.AreEqual("TestIdentity1", response.OwningTestProject.TestIdentity);
			Assert.AreEqual("TestStream", response.OwningTestProject.ComputedStream);
			CollectionAssert.AreEquivalent(platforms, response.Platforms);
		}

		[TestMethod]
		public void TestProjectPlatformResponse_Constructor_SetsProperties_EmptyPlatforms()
		{
			// Act
			TestProjectPlatformResponse response = new TestProjectPlatformResponse("KeyStats", "TestProject1", "TestIdentity1", "TestType1", "TestStream", Array.Empty<string>());

			// Assert
			Assert.IsNotNull(response.OwningTestProject);
			Assert.IsTrue(!response.Platforms.Any());
		}

		[TestMethod]
		public void TestProjectPlatformResponse_Constructor_SetsProperties_AllNullableFieldsNull()
		{
			// Act
			TestProjectPlatformResponse response = new TestProjectPlatformResponse("KeyStats", null, null, null, null, []);

			// Assert
			Assert.IsNotNull(response.OwningTestProject);
			Assert.AreEqual("KeyStats", response.OwningTestProject.SummaryType);
			Assert.IsNull(response.OwningTestProject.TestName);
			Assert.IsNull(response.OwningTestProject.TestIdentity);
			Assert.IsTrue(!response.Platforms.Any());
		}

		#endregion -- TestProjectPlatformResponse Tests --

		#region -- TestProjectCommitResponse Tests --

		[TestMethod]
		public void TestProjectCommitResponse_Constructor_SetsProperties_AllProperties()
		{
			// Arrange
			int[] commitIds = new[] { 12345, 12346, 12347 };

			// Act
			TestProjectCommitResponse response = new TestProjectCommitResponse("KeyStats", "TestProject1", "TestIdentity1", "TestType1", "TestStream", commitIds);

			// Assert
			Assert.IsNotNull(response.OwningTestProject);
			Assert.IsInstanceOfType(response.OwningTestProject, typeof(TestProjectResponse));
			Assert.AreEqual("KeyStats", response.OwningTestProject.SummaryType);
			Assert.AreEqual("TestProject1", response.OwningTestProject.TestName);
			Assert.AreEqual("TestIdentity1", response.OwningTestProject.TestIdentity);
			CollectionAssert.AreEquivalent(commitIds, response.CommitIds);
		}

		[TestMethod]
		public void TestProjectCommitResponse_Constructor_SetsProperties_SingleCommit()
		{
			// Arrange
			int[] commitIds = new[] { 12345 };

			// Act
			TestProjectCommitResponse response = new TestProjectCommitResponse("KeyStats", "TestProject1", "TestIdentity1", "TestType1", "TestStream", commitIds);

			// Assert
			Assert.IsTrue(response.CommitIds.Any());
			Assert.AreEqual(12345, response.CommitIds[0]);
		}

		[TestMethod]
		public void TestProjectCommitResponse_Constructor_SetsProperties_EmptyCommits()
		{
			// Act
			TestProjectCommitResponse response = new TestProjectCommitResponse("KeyStats", "TestProject1", "TestIdentity1", "TestType1", "TestStream", Array.Empty<int>());

			// Assert
			Assert.IsNotNull(response.CommitIds);
			Assert.IsTrue(!response.CommitIds.Any());
		}

		[TestMethod]
		public void TestProjectCommitResponse_Constructor_SetsProperties_AllNullableFieldsNull()
		{
			// Act
			TestProjectCommitResponse response = new TestProjectCommitResponse("KeyStats", null, null, null, null, new[] { 12345 });

			// Assert
			Assert.IsNotNull(response.OwningTestProject);
			Assert.AreEqual("KeyStats", response.OwningTestProject.SummaryType);
			Assert.IsNull(response.OwningTestProject.TestName);
			Assert.IsNull(response.OwningTestProject.TestIdentity);
			CollectionAssert.Contains(response.CommitIds, 12345);
		}

		#endregion -- TestProjectCommitResponse Tests --

		#region -- Response Hierarchy Tests --

		[TestMethod]
		[DataRow("Summary", "Test", "Source", "TestStream", 100)]
		[DataRow("UniqueSummaryType", "Test", "Source", "TestStream", 100)]
		public void ResponseHierarchy_DataFlowsThroughCorrectly(
			string summaryType,
			string testName,
			string testIdentity,
			string stream,
			int commitId)
		{
			// Act
			TestProjectCommitResponse response = new TestProjectCommitResponse(summaryType, testName, testIdentity, null, stream, new[] { commitId });

			// Assert
			Assert.IsNotNull(response.OwningTestProject);
			Assert.AreEqual(summaryType, response.OwningTestProject.SummaryType);
			Assert.AreEqual(testName, response.OwningTestProject.TestName);
			Assert.AreEqual(testIdentity, response.OwningTestProject.TestIdentity);
			CollectionAssert.Contains(response.CommitIds, commitId);
		}

		#endregion -- Response Hierarchy Tests --
	}
}
