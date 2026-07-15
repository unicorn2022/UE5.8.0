// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Plugins;

namespace HordeServer
{
	/// <summary>
	/// Configuration for the ExamplePlugin plugin
	/// </summary>
	public class ExamplePluginConfig : IPluginConfig
	{
		/// <summary>
		/// The Greeting provided by the example plugin
		/// </summary>
		public string GreetingMessage { get; set; } = "Default Greeting";

		/// <inheritdoc/>
		public void PostLoad(PluginConfigOptions configOptions)
		{			
		}
	}
}
