// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace P4VUtils
{
	/// <summary>Category used to organize commands in the UI</summary>
	public enum CommandCategory
	{
		/// <summary>
		/// Commands that help with common but simple operations
		/// </summary>
		Root,

		/// <summary>
		/// Commands that help with operations relating to content files
		/// </summary>
		Content,

		/// <summary>
		/// Commands that help with common but simple operations
		/// </summary>
		Toolbox,

		/// <summary>
		/// Complex commands to facilitate integrations
		/// </summary>
		Integrate,

		/// <summary>
		/// Local build and horde preflights
		/// </summary>
		Horde,

		/// <summary>
		/// Commands that open pages in the users browser
		/// </summary>
		Browser
	}

	/// <summary>
	/// Attribute used to define a class as a Command to hook into P4V
	/// 
	/// Containing attributes to define the cli name of the command, the categorization of the command, and the order it appears in the UI.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class CommandAttribute : System.Attribute
	{
		/// <summary>
		/// Command name, use to reference the command via command line args
		/// </summary>
		public string CommandName { get; internal set; } = null!;

		/// <summary>
		/// Category to organize into UI Elements. 
		/// </summary>
		public CommandCategory Category { get; internal set; }

		/// <summary>
		/// Used to preserve previous order.
		/// 
		/// Defaults to int.MaxValue to ensure any new items appear at the end of the sub menus.
		/// </summary>
		public int Order { get; internal set; }

		/// <summary>Initializes a new instance of <see cref="CommandAttribute"/></summary>
		/// <param name="commandName">CLI name of the command</param>
		/// <param name="category">Category the command belongs to</param>
		/// <param name="order">Sort order within the category</param>
		public CommandAttribute(string commandName, CommandCategory category, int order = int.MaxValue)
		{
			CommandName = commandName;
			Category = category;
			Order = order;
		}
	}
}
