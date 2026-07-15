// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Jupiter.DataStore;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Jupiter.Tests.Unit
{
	[TestClass]
	public class CbObjectStreamTests
	{

		[TestMethod]
		public async Task ReadCBObjectStreamAsync()
		{
			string fileName = "CompactBinaryStreams/0000000009d835662da72197fe4de182.bulk";
			FileInfo fi = new FileInfo(fileName);
			await using FileStream fs = fi.OpenRead();
			await using DataStoreObjectStreamReader streamReader = new DataStoreObjectStreamReader(fs, 65536);

			int countOfObjectsFound = 0;
			await foreach ((DataStorePartitionKey key, byte[] b) in streamReader.GetObjectBuffersAsync(CancellationToken.None))
			{
				Assert.AreNotEqual(IoHash.Zero, new IoHash(key.Bytes));
				Assert.AreNotEqual(0, b.Length);

				countOfObjectsFound++;
			}

			Assert.AreEqual(7958, countOfObjectsFound);
		}

		[TestMethod]
		public async Task ReadCBObjectStream2Async()
		{
			string fileName = "CompactBinaryStreams/0000000009d835e72da725effe4de182.bulk";
			FileInfo fi = new FileInfo(fileName);
			await using FileStream fs = fi.OpenRead();
			await using DataStoreObjectStreamReader streamReader = new DataStoreObjectStreamReader(fs, 65536);

			int countOfObjectsFound = 0;
			await foreach ((DataStorePartitionKey key, byte[] b) in streamReader.GetObjectBuffersAsync(CancellationToken.None))
			{
				Assert.AreNotEqual(IoHash.Zero, new IoHash(key.Bytes));
				Assert.AreNotEqual(0, b.Length);

				countOfObjectsFound++;
			}

			Assert.AreEqual(48246, countOfObjectsFound);
		}
	}
}
