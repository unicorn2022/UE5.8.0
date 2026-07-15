// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Tests;
using Microsoft.Extensions.DependencyInjection;
namespace HordeServer.Example.Plugin.Tests
{
	/// <summary>
	/// Handles set up of collections, services, fixtures etc during testing
	/// </summary>
	public class ExamplePluginTestSetup : BuildTestSetup
	{
		public ExamplePluginTestSetup()
		{
			AddPlugin<ExamplePlugin>();
		}

		protected override void ConfigureServices(IServiceCollection services)
		{
			base.ConfigureServices(services);

		}
	}
}