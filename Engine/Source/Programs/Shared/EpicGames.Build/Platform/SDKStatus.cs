// Copyright Epic Games, Inc. All Rights Reserved.

namespace EpicGames.Core
{
	/// <summary>
	/// SDK installation status
	/// </summary>
	public enum SDKStatus
	{
		/// <summary>
		/// Desired SDK is installed and set up.
		/// </summary>
		Valid,

		/// <summary>
		/// Could not find the desired SDK, SDK setup failed, etc.
		/// </summary>
		Invalid,
	};
}
