// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Net.Mime;
using System.Threading.Tasks;
using EpicGames.AspNet;
using EpicGames.Compression;
using EpicGames.Core;
using EpicGames.Serialization;
using Jupiter.Common;
using Jupiter.Controllers;
using Jupiter.Implementation;
using Jupiter.Tests.Functional;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Logger = Serilog.Core.Logger;

namespace Jupiter.FunctionalTests.Symbols
{
	public abstract class SymbolTests
	{
		protected SymbolTests(string namespaceSuffix)
		{
			_testNamespaceName = new NamespaceId($"test-symbols-{namespaceSuffix}");
		}

		private TestServer? _server;
		private HttpClient? _httpClient;
		private readonly NamespaceId _testNamespaceName;

		[TestInitialize]
		public async Task SetupAsync()
		{
			IConfigurationRoot configuration = new ConfigurationBuilder()
				// we are not reading the base appSettings here as we want exact control over what runs in the tests
				.AddJsonFile("appsettings.Testing.json", true)
				.AddInMemoryCollection(GetSettings())
				.AddEnvironmentVariables()
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
			_httpClient = _server.CreateClient();

			// Seed storage
			await Seed(_server.Services);
		}

		protected abstract IEnumerable<KeyValuePair<string, string?>> GetSettings();

		protected abstract Task Seed(IServiceProvider services);
		protected abstract Task Teardown(IServiceProvider services);

		[TestCleanup]
		public async Task MyTeardownAsync()
		{
			await Teardown(_server!.Services);
		}

		[TestMethod]
		public async Task PutCompressedBufferReturnsNotFoundAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");

			using MemoryStream compressedStream = new MemoryStream();
			CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Mermaid, OodleCompressionLevel.VeryFast, pdbPayload);

			byte[] compressedBufferPayload = compressedStream.ToArray();

			string moduleName = "EpicGames.Serialization.pdb";

			{
				using ByteArrayContent requestContent = new ByteArrayContent(compressedBufferPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response =
					await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				
				Assert.AreEqual(HttpStatusCode.NotFound, response.StatusCode);
			}
		}

		[TestMethod]
		public async Task PutSymbolsAsOctetReturnsEmptyNeedsListAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");

			string moduleName = "EpicGames.Serialization.pdb";
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			string pdbAge = "1";
			string fileName = "EpicGames.Serialization.pdb";

			{
				using ByteArrayContent requestContent = new ByteArrayContent(pdbPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}

			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(MediaTypeNames.Application.Octet));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CollectionAssert.AreEqual(pdbPayload, downloadedPayload);
			}
		}

		[TestMethod]
		public async Task PutSymbolsAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.OIDC.pdb");

			string moduleName = "EpicGames.OIDC.pdb";

			{
				using ByteArrayContent requestContent = new ByteArrayContent(pdbPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
				HttpResponseMessage response =
					await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
		}

		[TestMethod]
		public async Task PutCompactBinaryReturnsNeedsListAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			int pdbAge = 1;
			string fileName = "EpicGames.Serialization.pdb";
			
			byte[] putObjectRequestPayload = CreatePutSymbolsPayload(pdbChunks, moduleName, pdbIdentifier, pdbAge, pdbPayload);

			// First PUT returns Needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(pdbChunks.Count, putObjectResponse.Needs.Length);
			}

			// Compress and upload each of the needed attachments
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Another PUT should return an empty needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
			
			// Download the symbols as a compact binary
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompactBinary));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CbObject cb = new CbObject(downloadedPayload);
				Assert.AreEqual(moduleName, cb["moduleName"].AsString());
				Assert.AreEqual(pdbIdentifier, cb["pdbIdentifier"].AsString());
				Assert.AreEqual(pdbAge, cb["pdbAge"].AsInt32());
				Assert.AreEqual(pdbPayload.Length, cb["pdbSize"].AsInt64());
				
				CbArray responseChunks = cb["pdbChunks"].AsArray();
				Assert.AreEqual(pdbChunks.Count, responseChunks.Count);

				int i = 0;
				
				foreach (CbField responseChunk in responseChunks)
				{
					BlobId attachmentHash = BlobId.FromBlob(pdbChunks[i++]);
					IoHash expectedIoHash = attachmentHash.AsIoHash();
					
					Assert.AreEqual(expectedIoHash, responseChunk.AsBinaryAttachment().Hash);
				}
			}

			MemoryStream resultStream = new MemoryStream();
			
			// Download and decompress each attachment individually
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				MemoryStream uncompressedStream = new MemoryStream();
				await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(), (ulong)response.Content.Headers.ContentLength!, uncompressedStream);
				byte[] uncompressedContent = uncompressedStream.ToArray();
				CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedContent);
				
				await resultStream.WriteAsync(uncompressedContent, 0, uncompressedContent.Length);
			}
			
			// Validate the combined results
			CollectionAssert.AreEqual(pdbPayload, resultStream.ToArray());
		}

		[TestMethod]
		public async Task PutCompactBinaryWithPreExistingBlobsReturnsPartialNeedsListAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";

			int preUploadedChunkIndex = Random.Shared.Next(0, pdbChunks.Count);
			
			// Compress and upload a chunk before pushing the compact binary
			{
				Memory<byte> pdbChunk = pdbChunks[preUploadedChunkIndex];
				
				BlobId blobId = BlobId.FromBlob(pdbChunk);
			
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
			
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			int pdbAge = 1;
			string fileName = "EpicGames.Serialization.pdb";
			
			byte[] putObjectRequestPayload = CreatePutSymbolsPayload(pdbChunks, moduleName, pdbIdentifier, pdbAge, pdbPayload);

			// First PUT returns partial Needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(pdbChunks.Count - 1, putObjectResponse.Needs.Length);
			}

			// Compress and upload each of the needed attachments, skipping the pre-uploaded blob
			for (int i = 0; i < pdbChunks.Count; i++)
			{
				if (i == preUploadedChunkIndex)
				{
					continue;
				}
				
				Memory<byte> pdbChunk = pdbChunks[i];
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Another PUT should return an empty needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
			
			// Download the symbols as a compact binary
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompactBinary));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CbObject cb = new CbObject(downloadedPayload);
				Assert.AreEqual(moduleName, cb["moduleName"].AsString());
				Assert.AreEqual(pdbIdentifier, cb["pdbIdentifier"].AsString());
				Assert.AreEqual(pdbAge, cb["pdbAge"].AsInt32());
				Assert.AreEqual(pdbPayload.Length, cb["pdbSize"].AsInt64());
				
				CbArray responseChunks = cb["pdbChunks"].AsArray();
				Assert.AreEqual(pdbChunks.Count, responseChunks.Count);

				int i = 0;
				
				foreach (CbField responseChunk in responseChunks)
				{
					BlobId attachmentHash = BlobId.FromBlob(pdbChunks[i++]);
					IoHash expectedIoHash = attachmentHash.AsIoHash();
					
					Assert.AreEqual(expectedIoHash, responseChunk.AsBinaryAttachment().Hash);
				}
			}

			MemoryStream resultStream = new MemoryStream();
			
			// Download and decompress each attachment individually
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				MemoryStream uncompressedStream = new MemoryStream();
				await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(), (ulong)response.Content.Headers.ContentLength!, uncompressedStream);
				byte[] uncompressedContent = uncompressedStream.ToArray();
				CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedContent);
				
				await resultStream.WriteAsync(uncompressedContent, 0, uncompressedContent.Length);
			}
			
			// Validate the combined results
			CollectionAssert.AreEqual(pdbPayload, resultStream.ToArray());
		}

		[TestMethod]
		public async Task PutCompactBinaryWithPreExistingBlobsFromBuildReturnsPartialNeedsListAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";

			int preUploadedChunkIndex = Random.Shared.Next(0, pdbChunks.Count);
			
			// Compress and upload a chunk as part of a build before pushing the compact binary
			{
				Memory<byte> pdbChunk = pdbChunks[preUploadedChunkIndex];
				
				BlobId blobId = BlobId.FromBlob(pdbChunk);
			
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v2/builds/{_testNamespaceName}/test-bucket/test-build-id/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			int pdbAge = 1;
			string fileName = "EpicGames.Serialization.pdb";
			
			byte[] putObjectRequestPayload = CreatePutSymbolsPayload(pdbChunks, moduleName, pdbIdentifier, pdbAge, pdbPayload);

			// First PUT returns partial Needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(pdbChunks.Count - 1, putObjectResponse.Needs.Length);
			}

			// Compress and upload each of the needed attachments, skipping the pre-uploaded blob
			for (int i = 0; i < pdbChunks.Count; i++)
			{
				if (i == preUploadedChunkIndex)
				{
					continue;
				}
				
				Memory<byte> pdbChunk = pdbChunks[i];
				
				BlobId blobId = BlobId.FromBlob(pdbChunk);
			
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Another PUT should return an empty needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
			
			// Download the symbols as a compact binary
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompactBinary));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CbObject cb = new CbObject(downloadedPayload);
				Assert.AreEqual(moduleName, cb["moduleName"].AsString());
				Assert.AreEqual(pdbIdentifier, cb["pdbIdentifier"].AsString());
				Assert.AreEqual(pdbAge, cb["pdbAge"].AsInt32());
				Assert.AreEqual(pdbPayload.Length, cb["pdbSize"].AsInt64());
				
				CbArray responseChunks = cb["pdbChunks"].AsArray();
				Assert.AreEqual(pdbChunks.Count, responseChunks.Count);

				int i = 0;
				
				foreach (CbField responseChunk in responseChunks)
				{
					BlobId attachmentHash = BlobId.FromBlob(pdbChunks[i++]);
					IoHash expectedIoHash = attachmentHash.AsIoHash();
					
					Assert.AreEqual(expectedIoHash, responseChunk.AsBinaryAttachment().Hash);
				}
			}

			MemoryStream resultStream = new MemoryStream();
			
			// Download and decompress each attachment individually
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				MemoryStream uncompressedStream = new MemoryStream();
				await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(), (ulong)response.Content.Headers.ContentLength!, uncompressedStream);
				byte[] uncompressedContent = uncompressedStream.ToArray();
				CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedContent);
				
				await resultStream.WriteAsync(uncompressedContent, 0, uncompressedContent.Length);
			}
			
			// Validate the combined results
			CollectionAssert.AreEqual(pdbPayload, resultStream.ToArray());
		}

		[TestMethod]
		public async Task PutCompactBinaryWithAllPreExistingBlobsReturnsEmptyNeedsListAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";

			// Preemptively compress and upload each attachment
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
			
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			int pdbAge = 1;
			string fileName = "EpicGames.Serialization.pdb";
			
			byte[] putObjectRequestPayload = CreatePutSymbolsPayload(pdbChunks, moduleName, pdbIdentifier, pdbAge, pdbPayload);

			// First PUT returns empty Needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
			
			// Download the symbols as a compact binary
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompactBinary));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CbObject cb = new CbObject(downloadedPayload);
				Assert.AreEqual(moduleName, cb["moduleName"].AsString());
				Assert.AreEqual(pdbIdentifier, cb["pdbIdentifier"].AsString());
				Assert.AreEqual(pdbAge, cb["pdbAge"].AsInt32());
				Assert.AreEqual(pdbPayload.Length, cb["pdbSize"].AsInt64());
				
				CbArray responseChunks = cb["pdbChunks"].AsArray();
				Assert.AreEqual(pdbChunks.Count, responseChunks.Count);

				int i = 0;
				
				foreach (CbField responseChunk in responseChunks)
				{
					BlobId attachmentHash = BlobId.FromBlob(pdbChunks[i++]);
					IoHash expectedIoHash = attachmentHash.AsIoHash();
					
					Assert.AreEqual(expectedIoHash, responseChunk.AsBinaryAttachment().Hash);
				}
			}

			MemoryStream resultStream = new MemoryStream();
			
			// Download and decompress each attachment individually
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				MemoryStream uncompressedStream = new MemoryStream();
				await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(), (ulong)response.Content.Headers.ContentLength!, uncompressedStream);
				byte[] uncompressedContent = uncompressedStream.ToArray();
				CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedContent);
				
				await resultStream.WriteAsync(uncompressedContent, 0, uncompressedContent.Length);
			}
			
			// Validate the combined results
			CollectionAssert.AreEqual(pdbPayload, resultStream.ToArray());
		}

		[TestMethod]
		public async Task PutCompactBinaryWithAllPreExistingBlobsFromBuildReturnsEmptyNeedsListAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";

			// Preemptively compress and upload each attachment as part of a build
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
			
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v2/builds/{_testNamespaceName}/test-bucket/test-build-id/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			int pdbAge = 1;
			string fileName = "EpicGames.Serialization.pdb";
			
			byte[] putObjectRequestPayload = CreatePutSymbolsPayload(pdbChunks, moduleName, pdbIdentifier, pdbAge, pdbPayload);

			// First PUT returns empty Needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
			
			// Download the symbols as a compact binary
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompactBinary));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CbObject cb = new CbObject(downloadedPayload);
				Assert.AreEqual(moduleName, cb["moduleName"].AsString());
				Assert.AreEqual(pdbIdentifier, cb["pdbIdentifier"].AsString());
				Assert.AreEqual(pdbAge, cb["pdbAge"].AsInt32());
				Assert.AreEqual(pdbPayload.Length, cb["pdbSize"].AsInt64());
				
				CbArray responseChunks = cb["pdbChunks"].AsArray();
				Assert.AreEqual(pdbChunks.Count, responseChunks.Count);

				int i = 0;
				
				foreach (CbField responseChunk in responseChunks)
				{
					BlobId attachmentHash = BlobId.FromBlob(pdbChunks[i++]);
					IoHash expectedIoHash = attachmentHash.AsIoHash();
					
					Assert.AreEqual(expectedIoHash, responseChunk.AsBinaryAttachment().Hash);
				}
			}

			MemoryStream resultStream = new MemoryStream();
			
			// Download and decompress each attachment individually
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				MemoryStream uncompressedStream = new MemoryStream();
				await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(), (ulong)response.Content.Headers.ContentLength!, uncompressedStream);
				byte[] uncompressedContent = uncompressedStream.ToArray();
				CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedContent);
				
				await resultStream.WriteAsync(uncompressedContent, 0, uncompressedContent.Length);
			}
			
			// Validate the combined results
			CollectionAssert.AreEqual(pdbPayload, resultStream.ToArray());
		}

		[TestMethod]
		public async Task GetSymbolsAsOctetAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";

			// Preemptively compress and upload each attachment as part of a build
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
			
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v2/builds/{_testNamespaceName}/test-bucket/test-build-id/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			string pdbIdentifier = "6A976472EE4B4BC2BE73BFAC3B2C3EC2";
			int pdbAge = 1;
			string fileName = "EpicGames.Serialization.pdb";
			
			byte[] putObjectRequestPayload = CreatePutSymbolsPayload(pdbChunks, moduleName, pdbIdentifier, pdbAge, pdbPayload);

			// First PUT returns empty Needs list
			{
				using ByteArrayContent requestContent = new ByteArrayContent(putObjectRequestPayload);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompactBinary);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				PutObjectResponse? putObjectResponse = await response.Content.ReadFromJsonAsync<PutObjectResponse>();
				Assert.IsNotNull(putObjectResponse);
				Assert.AreEqual(0, putObjectResponse.Needs.Length);
			}
			
			// Download the combined symbols as an octet stream
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/{pdbIdentifier}{pdbAge}/{fileName}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(MediaTypeNames.Application.Octet));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				byte[] downloadedPayload = await response.Content.ReadAsByteArrayAsync();
				CollectionAssert.AreEqual(pdbPayload, downloadedPayload);
			}
		}

		[TestMethod]
		public async Task GetBlobAsCompressedBufferThatWasUploadedAsOctetReturnsUnsupportedMediaTypeAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";
			
			Memory<byte> pdbChunk = pdbChunks[0];

			// Upload the uncompressed chunk
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				
				using ReadOnlyMemoryContent requestContent = new ReadOnlyMemoryContent(pdbChunk);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Attempt to download blob as an UnrealCompressedBuffer
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				Assert.AreEqual(HttpStatusCode.UnsupportedMediaType, response.StatusCode);
			}
		}

		[TestMethod]
		public async Task GetBlobAsOctetThatWasUploadedAsCompressedBufferReturnsUnsupportedMediaTypeAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";
			
			Memory<byte> pdbChunk = pdbChunks[0];

			// Upload the compressed chunk
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				
				using ReadOnlyMemoryContent requestContent = new ReadOnlyMemoryContent(compressedStream.ToArray());
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Attempt to download blob as an octet stream
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(MediaTypeNames.Application.Octet));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				Assert.AreEqual(HttpStatusCode.UnsupportedMediaType, response.StatusCode);
			}
		}

		[TestMethod]
		public async Task GetBlobThatWasUploadedAsOctetAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";
			
			Memory<byte> pdbChunk = pdbChunks[0];

			// Upload the uncompressed chunk
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				
				using ByteArrayContent requestContent = new ByteArrayContent(pdbChunk.ToArray());
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Download the blob, accepting either content type
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(MediaTypeNames.Application.Octet));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				switch (response.Content.Headers.ContentType!.MediaType)
				{
					case CustomMediaTypeNames.UnrealCompressedBuffer:
						{
							MemoryStream uncompressedStream = new MemoryStream();
							await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(),
								(ulong)response.Content.Headers.ContentLength!, uncompressedStream);
							CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedStream.ToArray());
							break;
						}
					case MediaTypeNames.Application.Octet:
						{
							CollectionAssert.AreEqual(pdbChunk.ToArray(), await response.Content.ReadAsByteArrayAsync());
							break;
						}
					default:
						{
							Assert.Fail();
							break;
						}
				}
			}
		}

		[TestMethod]
		public async Task GetBlobThatWasUploadedAsCompressedBufferAsync()
		{
			byte[] pdbPayload = await File.ReadAllBytesAsync($"Symbols/Payloads/EpicGames.Serialization.pdb");
			List<Memory<byte>> pdbChunks = Chunk(pdbPayload);

			string moduleName = "EpicGames.Serialization.pdb";
			
			Memory<byte> pdbChunk = pdbChunks[0];

			// Upload the compressed chunk
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				
				using MemoryStream compressedStream = new MemoryStream();
				CompressedBuffer.CompressContent(compressedStream, OodleCompressorType.Kraken, OodleCompressionLevel.VeryFast, pdbChunk.ToArray());
				compressedStream.Seek(0, SeekOrigin.Begin);
				
				using StreamContent requestContent = new StreamContent(compressedStream);
				requestContent.Headers.ContentType = new MediaTypeHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer);
				HttpResponseMessage response = await _httpClient!.PutAsync(new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{blobId}", UriKind.Relative), requestContent);
				await response.EnsureSuccessStatusCodeWithMessageAsync();
			}
			
			// Download the blob, accepting either content type
			{
				BlobId blobId = BlobId.FromBlob(pdbChunk);
				ContentId contentId = ContentId.FromBlobIdentifier(blobId);
				
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/symbols/{_testNamespaceName}/{moduleName}/blobs/{contentId}", UriKind.Relative));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(MediaTypeNames.Application.Octet));
				request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue(CustomMediaTypeNames.UnrealCompressedBuffer));
				HttpResponseMessage response = await _httpClient!.SendAsync(request);
				await response.EnsureSuccessStatusCodeWithMessageAsync();

				switch (response.Content.Headers.ContentType!.MediaType)
				{
					case CustomMediaTypeNames.UnrealCompressedBuffer:
						{
							MemoryStream uncompressedStream = new MemoryStream();
							await CompressedBuffer.DecompressContentAsync(await response.Content.ReadAsStreamAsync(),
								(ulong)response.Content.Headers.ContentLength!, uncompressedStream);
							CollectionAssert.AreEqual(pdbChunk.ToArray(), uncompressedStream.ToArray());
							break;
						}
					case MediaTypeNames.Application.Octet:
						{
							CollectionAssert.AreEqual(pdbChunk.ToArray(), await response.Content.ReadAsByteArrayAsync());
							break;
						}
					default:
						{
							Assert.Fail();
							break;
						}
				}
			}
		}

		private static List<Memory<byte>> Chunk(byte[] payload)
		{
			// Artificially small chunk size
			const int FixedChunkSize = 32 * 1024;
			long minLastChunkSize = Math.Min(128 * 1024, FixedChunkSize / 32);
			
			List<Memory<byte>> results = new List<Memory<byte>>();

			int offset = 0;

			while (offset < payload.Length)
			{
				int bytesLeft = payload.Length - offset;
				int chunkSize = Math.Min(bytesLeft, FixedChunkSize);

				if (bytesLeft - chunkSize < minLastChunkSize)
				{
					chunkSize = bytesLeft;
				}
				
				Memory<byte> chunkData = new Memory<byte>(payload, offset, chunkSize);
				results.Add(chunkData);
				offset += chunkSize;
			}

			return results;
		}

		private static byte[] CreatePutSymbolsPayload(List<Memory<byte>> pdbChunks, string moduleName, string pdbIdentifier, int pdbAge, byte[] pdbPayload)
		{
			CbWriter writer = new CbWriter();
			writer.BeginObject();
			
			writer.BeginArray("pdbChunks");
			
			foreach (Memory<byte> pdbChunk in pdbChunks)
			{
				BlobId attachmentHash = BlobId.FromBlob(pdbChunk);
				IoHash attachmentIoHash = attachmentHash.AsIoHash();
				writer.WriteBinaryAttachmentValue(attachmentIoHash);
			}
			
			writer.EndArray();
			
			writer.WriteString("moduleName", moduleName);
			writer.WriteString("pdbIdentifier", pdbIdentifier);
			writer.WriteInteger("pdbAge", pdbAge);
			writer.WriteInteger("pdbSize", pdbPayload.Length);
							
			writer.EndObject();

			byte[] putObjectRequestPayload = writer.ToByteArray();
			return putObjectRequestPayload;
		}
	}

	[TestClass]
	public class MemorySymbolTests : SymbolTests
	{
		public MemorySymbolTests() : base("memory")
		{
		}
		protected override IEnumerable<KeyValuePair<string, string?>> GetSettings()
		{
			return new[] { new KeyValuePair<string, string?>("UnrealCloudDDC:ReferencesDbImplementations", UnrealCloudDDCSettings.ReferencesDbImplementations.Memory.ToString()) };
		}

		protected override Task Seed(IServiceProvider serverServices)
		{
			return Task.CompletedTask;
		}

		protected override Task Teardown(IServiceProvider serverServices)
		{
			return Task.CompletedTask;
		}
	}

	[TestClass]
	public class ScyllaSymbolsTests : SymbolTests
	{
		public ScyllaSymbolsTests() : base("scylla")
		{
		}
		protected override IEnumerable<KeyValuePair<string, string?>> GetSettings()
		{
			return new[] { new KeyValuePair<string, string?>("UnrealCloudDDC:ReferencesDbImplementations", UnrealCloudDDCSettings.ReferencesDbImplementations.Scylla.ToString()) };
		}

		protected override Task Seed(IServiceProvider serverServices)
		{
			return Task.CompletedTask;
		}

		protected override async Task Teardown(IServiceProvider provider)
		{
			await Task.CompletedTask;
		}
	}
}
