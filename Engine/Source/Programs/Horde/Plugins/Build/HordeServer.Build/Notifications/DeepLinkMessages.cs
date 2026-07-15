// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Notifications
{
	/// <summary>
	/// A direct message link request
	/// </summary>
	public class GetDirectMessageLinkRequest
	{
		/// <summary>
		/// the user ids
		/// </summary>
		public List<string> UserIds { get; set; } = new List<string>();
	}

	/// <summary>
	/// The Direct Message link response
	/// </summary>
	public class GetDirectMessageLinkResponse
	{
		/// <summary>
		/// The result deep link
		/// </summary>
		public string? Url { get; set; }
	}

	/// <summary>
	/// The Channel link response
	/// </summary>
	public class GetChannelLinkResponse
	{
		/// <summary>
		/// The result deep link
		/// </summary>
		public string? Url { get; set; }
	}
}