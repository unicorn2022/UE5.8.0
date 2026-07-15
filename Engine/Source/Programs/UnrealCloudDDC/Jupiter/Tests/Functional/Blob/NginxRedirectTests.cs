// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Net.Mime;
using System.Text;
using System.Threading.Tasks;
using Jupiter.Common;
using Jupiter.Controllers;
using Jupiter.Implementation;
using Jupiter.Tests.Functional;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog;
using Serilog.Core;

namespace Jupiter.FunctionalTests.Storage
{
	[TestClass]
	public class NginxRedirectTests
	{
		protected const string FileContents = "This is some random contents";

		protected BlobId FileContentsHash { get; } = BlobId.FromBlob(Encoding.ASCII.GetBytes(FileContents));

		protected NamespaceId TestNamespaceName { get; } = new NamespaceId("testbucket");

		protected NamespaceId TestNginxRedirectNamespaceName { get; } = new NamespaceId("test-namespace-nginx-redirect");
		protected TestServer? Server { get; set; }

		private HttpClient? _httpClient;
		private string _localTestDir = null!;

		[TestInitialize]
		public void Setup()
		{
			_localTestDir = Path.Combine(Path.GetTempPath(), "NginxRedirectTests", Path.GetRandomFileName());

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

			host.Start();
			Server = host.GetTestServer();
			_httpClient = Server.CreateClient();
		}

		[TestCleanup]
		public void Teardown()
		{
			Directory.Delete(_localTestDir, true);
		}

		protected IEnumerable<KeyValuePair<string, string?>> GetSettings()
		{
			return new[]
			{
				new KeyValuePair<string, string?>("UnrealCloudDDC:StorageImplementations:0", UnrealCloudDDCSettings.StorageBackendImplementations.FileSystem.ToString()),
				new KeyValuePair<string, string?>("UnrealCloudDDC:StorageImplementations:1", UnrealCloudDDCSettings.StorageBackendImplementations.S3.ToString()),
				new KeyValuePair<string, string?>("Filesystem:RootDir", _localTestDir),
				new KeyValuePair<string, string?>("Nginx:UseNginxRedirect", "true"),
				new KeyValuePair<string, string?>("S3:BucketName", $"tests-{TestNamespaceName}")
			};
		}

		[TestMethod]
		public async Task FetchBlobWithNginxRedirectAsync()
		{
			byte[] s3ContentBytes = Encoding.ASCII.GetBytes(FileContents);
			using ByteArrayContent requestContent = new ByteArrayContent(s3ContentBytes);
			requestContent.Headers.ContentType = new MediaTypeHeaderValue(MediaTypeNames.Application.Octet);
			requestContent.Headers.ContentLength = FileContents.Length;
			HttpResponseMessage putResponse = await _httpClient!.PutAsync(new Uri($"api/v1/blobs/{TestNamespaceName}/{FileContentsHash}", UriKind.Relative), requestContent);
			await putResponse.EnsureSuccessStatusCodeWithMessageAsync();
			BlobUploadResponse? response = await putResponse.Content.ReadFromJsonAsync<BlobUploadResponse>();
			Assert.IsNotNull(response);
			Assert.AreEqual(FileContentsHash, response.Identifier);

			using HttpRequestMessage getObjectRequest = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/blobs/{TestNamespaceName}/{FileContentsHash}", UriKind.Relative));
			getObjectRequest.Headers.Add("X-Jupiter-XAccel-Supported", "true");
			HttpResponseMessage getResponse = await _httpClient.SendAsync(getObjectRequest);

			await getResponse.EnsureSuccessStatusCodeWithMessageAsync();
			Assert.IsTrue(getResponse.Headers.Contains("X-Accel-Redirect"));
			string uri = getResponse.Headers.GetValues("X-Accel-Redirect").First();
			string contentType = getResponse.Content.Headers.ContentType?.MediaType ?? "";
			Assert.AreEqual(MediaTypeNames.Application.Octet, contentType);
			Assert.AreEqual($"/nginx-blobs/testbucket/E3/06/{FileContentsHash}", uri);

		}

		[TestMethod]
		public async Task EnsureBlobsSupportNginxRedirectAsync()
		{
			byte[] payload = Encoding.ASCII.GetBytes("Foo bar nginx redirect supported");
			using ByteArrayContent requestContent = new ByteArrayContent(payload);
			requestContent.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
			requestContent.Headers.ContentLength = payload.Length;
			BlobId contentHash = BlobId.FromBlob(payload);
			HttpResponseMessage putResponse = await _httpClient!.PutAsync(new Uri($"api/v1/blobs/{TestNginxRedirectNamespaceName}/{contentHash}", UriKind.Relative), requestContent);
			await putResponse.EnsureSuccessStatusCodeWithMessageAsync();
			BlobUploadResponse? response = await putResponse.Content.ReadFromJsonAsync<BlobUploadResponse>();
			Assert.IsNotNull(response);
			Assert.AreEqual(contentHash, response.Identifier);

			FileSystemStore filesystemStore = Server!.Services.GetService<FileSystemStore>()!;
			AmazonS3Store s3Store = Server.Services.GetService<AmazonS3Store>()!;

			// verify that it got uploaded to s3 but not to the filesystem cache as this is a bypass cache operation
			Assert.IsTrue(await s3Store.ExistsAsync(TestNginxRedirectNamespaceName, contentHash, forceCheck: true));
			Assert.IsFalse(await filesystemStore.ExistsAsync(TestNginxRedirectNamespaceName, contentHash, forceCheck: true));

			HttpResponseMessage getResponse = await _httpClient!.GetAsync(new Uri($"api/v1/blobs/{TestNginxRedirectNamespaceName}/{contentHash}", UriKind.Relative));
			await getResponse.EnsureSuccessStatusCodeWithMessageAsync();
			CollectionAssert.AreEqual(payload, actual: await getResponse.Content.ReadAsByteArrayAsync());

			// we have now done a get of the blob and it should be present in all layers
			Assert.IsTrue(await s3Store.ExistsAsync(TestNginxRedirectNamespaceName, contentHash, forceCheck: true));
			Assert.IsTrue(await filesystemStore.ExistsAsync(TestNginxRedirectNamespaceName, contentHash, forceCheck: true));

			{
				using HttpRequestMessage getObjectRequestNginx = new HttpRequestMessage(HttpMethod.Get, new Uri($"api/v1/blobs/{TestNginxRedirectNamespaceName}/{contentHash}", UriKind.Relative));
				getObjectRequestNginx.Headers.Add("X-Jupiter-XAccel-Supported", "true");
				HttpResponseMessage getResponseNginx = await _httpClient!.SendAsync(getObjectRequestNginx);

				await getResponseNginx.EnsureSuccessStatusCodeWithMessageAsync();
				Assert.IsTrue(getResponseNginx.Headers.Contains("X-Accel-Redirect"));
				string uri = getResponseNginx.Headers.GetValues("X-Accel-Redirect").First();
				string contentType = getResponseNginx.Content.Headers.ContentType?.MediaType ?? "";
				Assert.AreEqual(MediaTypeNames.Application.Octet, contentType);
				Assert.AreEqual($"/nginx-blobs/{TestNginxRedirectNamespaceName}/E7/12/{contentHash}", uri);
			}
		}
	}
}
