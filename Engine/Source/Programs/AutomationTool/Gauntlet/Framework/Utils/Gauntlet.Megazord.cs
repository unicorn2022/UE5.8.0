// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.Json.Serialization;

#nullable enable

namespace Gauntlet
{
	/// <summary>
	/// Helper function library for the device farm Megazord API.
	/// This is a partial class with platform-specific and restricted implementations in:
	/// - Engine/Platforms/<PlatformName>/Source/Programs/AutomationTool/Gauntlet/Framework/Utils/Gauntlet.Megazord.cs
	/// </summary>
	public sealed partial class MegazordService
	{
		/// <summary>
		/// Wrapper for the response from the Megazord users requests
		/// </summary>
		public class MegazordUserResponse
		{
			/// <summary>
			/// Identifier of the local user account
			/// </summary>
			[JsonPropertyName("user_id")]
			public string? UserID { get; set; }
		}
	}
}
