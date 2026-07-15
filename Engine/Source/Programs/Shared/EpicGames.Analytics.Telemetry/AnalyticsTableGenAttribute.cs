// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.Analytics.Telemetry
{
	/// <summary>
	/// Attribute to annoatate that table generation should run on the type.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public sealed class AnalyticsTableGenAttribute : System.Attribute { }
}