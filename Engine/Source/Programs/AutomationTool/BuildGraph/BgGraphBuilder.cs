// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationTool
{
	/// <summary>
	/// Base class for any user defined graphs
	/// </summary>
	public abstract class BgGraphBuilder
	{
		/// <summary>
		/// Accessor for default logger instance
		/// </summary>
		protected static ILogger Logger => Log.Logger;

		/// <summary>
		/// Callback used to instantiate the graph
		/// </summary>
		public abstract BgGraph CreateGraph();
	}
}
