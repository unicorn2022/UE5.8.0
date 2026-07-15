// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Serialization;
using Jupiter.Common;
using Jupiter.Implementation;
using Jupiter.Implementation.Blob;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Serilog.Core;

namespace Jupiter.FunctionalTests.GC
{
	[TestClass]
	public class MemoryGCBlobTestsRefs : GCBlobTestsRefs
	{
		protected override string GetImplementation()
		{
			return "Memory";
		}
	}

	[TestClass]
	public class ScyllaGCBlobTestsRefs : GCBlobTestsRefs
	{
		protected override string GetImplementation()
		{
			return "Scylla";
		}
	}

	public abstract class GCBlobTestsRefs
	{
		private TestServer? _server;

		private readonly BlobId object0id = new BlobId("0000000000000000000000000000000000000000");
		private readonly BlobId object1id = new BlobId("1111111111111111111111111111111111111111");
		private readonly BlobId object2id = new BlobId("2222222222222222222222222222222222222222");
		private readonly BlobId object3id = new BlobId("3333333333333333333333333333333333333333");
		private readonly BlobId object4id = new BlobId("4444444444444444444444444444444444444444");
		private readonly BlobId object5id = new BlobId("5555555555555555555555555555555555555555");
		private readonly BlobId object6id = new BlobId("6666666666666666666666666666666666666666");
		private IBlobService? _blobService;

		private readonly NamespaceId TestNamespace = new NamespaceId("test-namespace");

		[TestInitialize]
		public async Task SetupAsync()
		{
			IConfigurationRoot configuration = new ConfigurationBuilder()
				// we are not reading the base appSettings here as we want exact control over what runs in the tests
				.AddJsonFile("appsettings.Testing.json", false)
				.AddEnvironmentVariables()
				.AddInMemoryCollection(new List<KeyValuePair<string, string?>>()
				{
					new KeyValuePair<string, string?>("UnrealCloudDDC:StorageImplementations:0", "Memory"),
					new KeyValuePair<string, string?>("GC:CleanOldBlobs", true.ToString()),
					new KeyValuePair<string, string?>("UnrealCloudDDC:BlobIndexImplementation", GetImplementation()),

				})
				.Build();

			Logger logger = new LoggerConfiguration()
				.ReadFrom.Configuration(configuration)
				.CreateLogger();

			IHost host = new HostBuilder()
				.ConfigureWebHost(webHostBuilder =>
				{
					webHostBuilder.UseTestServer()
						.UseConfiguration(configuration)
						.UseEnvironment("Testing")
						.ConfigureServices(collection => collection.AddSerilog(logger))
						.UseStartup<JupiterStartup>();
				}).Build();
			
			await host.StartAsync();
			_server = host.GetTestServer();

			_blobService = _server.Services.GetService<IBlobService>()!;

			MemoryBlobStore memoryBlobStore = (MemoryBlobStore)((BlobService)_blobService).BlobStore.First();
			byte[] emptyContents = Array.Empty<byte>();
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object0id);
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object1id);// this is not in the index
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object2id);
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object3id);
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object4id); // this is not in the index
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object5id); // this is not in the index
			await memoryBlobStore.PutObjectAsync(TestNamespace, emptyContents, object6id);

			// set all objects to be old, only the orphaned blobs should be deleted
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object0id, DateTime.Now.AddDays(-2));
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object1id, DateTime.Now.AddDays(-2));
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object2id, DateTime.Now.AddDays(-2));
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object3id, DateTime.Now.AddDays(-2));
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object4id, DateTime.Now.AddDays(-2));
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object5id, DateTime.Now.AddDays(-2));
			memoryBlobStore.SetLastModifiedTime(TestNamespace, object6id, DateTime.Now.AddDays(-2));

			BucketId testBucket = new BucketId("test");
			IRefService? refService = _server.Services.GetService<IRefService>()!;
			Assert.IsNotNull(refService);
			(BlobId ob0_hash, CbObject ob0_cb) = GetCBWithAttachment(object0id);
			await refService.PutAsync(TestNamespace, testBucket, RefId.FromName("object0"), ob0_hash, ob0_cb);

			(BlobId ob2_hash, CbObject ob2_cb) = GetCBWithAttachment(object2id);
			await refService.PutAsync(TestNamespace, testBucket, RefId.FromName("object2"), ob2_hash, ob2_cb);

			(BlobId ob3_hash, CbObject ob3_cb) = GetCBWithAttachment(object3id);
			await refService.PutAsync(TestNamespace, testBucket, RefId.FromName("object3"), ob3_hash, ob3_cb);

			(BlobId ob6_hash, CbObject ob6_cb) = GetCBWithAttachment(object6id);
			await refService.PutAsync(TestNamespace, testBucket, RefId.FromName("object6"), ob6_hash, ob6_cb);

			IReferencesStore referenceStore = _server.Services.GetService<IReferencesStore>()!;
			await referenceStore.UpdateLastAccessTimeAsync(TestNamespace, testBucket, RefId.FromName("object0"), DateTime.Now.AddDays(-2));
			await referenceStore.UpdateLastAccessTimeAsync(TestNamespace, testBucket, RefId.FromName("object2"), DateTime.Now.AddDays(-2));
			await referenceStore.UpdateLastAccessTimeAsync(TestNamespace, testBucket, RefId.FromName("object3"), DateTime.Now.AddDays(-2));
			await referenceStore.UpdateLastAccessTimeAsync(TestNamespace, testBucket, RefId.FromName("object6"), DateTime.Now.AddDays(-2));

			IBlobIndex? blobIndex = _server.Services.GetService<IBlobIndex>()!;
			Assert.IsNotNull(blobIndex);
			await blobIndex.AddBlobToIndexAsync(TestNamespace, object0id, (ulong)ob0_cb.GetSize());
			await blobIndex.AddRefToBlobsAsync(TestNamespace, testBucket, RefId.FromName("object0"), new[] { object0id });
			await blobIndex.AddBlobToIndexAsync(TestNamespace, object2id, (ulong)ob2_cb.GetSize());
			await blobIndex.AddRefToBlobsAsync(TestNamespace, testBucket, RefId.FromName("object2"), new[] { object2id });
			await blobIndex.AddBlobToIndexAsync(TestNamespace, object3id, (ulong)ob3_cb.GetSize());
			await blobIndex.AddRefToBlobsAsync(TestNamespace, testBucket, RefId.FromName("object3"), new[] { object3id });
			await blobIndex.AddBlobToIndexAsync(TestNamespace, object6id, (ulong)ob6_cb.GetSize());
			await blobIndex.AddRefToBlobsAsync(TestNamespace, testBucket, RefId.FromName("object6"), new[] { object6id });
		}

		protected abstract string GetImplementation();

		private static (BlobId, CbObject) GetCBWithAttachment(BlobId blobIdentifier)
		{
			CbWriter writer = new CbWriter();
			writer.BeginObject();
			writer.WriteBinaryAttachment("Attachment", blobIdentifier.AsIoHash());
			writer.EndObject();

			byte[] b = writer.ToByteArray();
			return (BlobId.FromBlob(b), new CbObject(b));
		}

		[TestMethod]
		public async Task RunBlobCleanupRefsAsync()
		{
			OrphanBlobCleanupRefs? cleanup = _server!.Services.GetService<OrphanBlobCleanupRefs>();
			Assert.IsNotNull(cleanup);

			using CancellationTokenSource cts = new();
			ulong countOfRemovedBlobs = await cleanup.CleanupAsync(cts.Token);
			Assert.AreEqual(3u, countOfRemovedBlobs);

			foreach (BlobId blob in new BlobId[] { object1id, object4id, object5id })
			{
				Assert.IsFalse(await _blobService!.ExistsAsync(TestNamespace, blob));
			}

			foreach (BlobId blob in new BlobId[] { object2id, object3id, object6id })
			{
				Assert.IsTrue(await _blobService!.ExistsAsync(TestNamespace, blob));
			}
		}
	}
}
