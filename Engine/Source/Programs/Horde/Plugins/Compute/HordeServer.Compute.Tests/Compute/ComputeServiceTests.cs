// Copyright Epic Games, Inc. All Rights Reserved.

using System.Net;

namespace HordeServer.Tests.Compute
{
	[TestClass]
	public class ComputeServiceTests
	{
		[TestMethod]
		[DataRow("192.168.1.1")]
		[DataRow("::ffff:192.168.1.1")]
		public void TryGetNetworkConfigMatchesCidrBlock(string ip)
		{
			ComputeConfig config = new() { Networks = { new NetworkConfig { Id = "test", CidrBlock = "192.168.0.0/16" } } };
			Assert.IsTrue(config.TryGetNetworkConfig(IPAddress.Parse(ip), out _));
		}
	}
}
