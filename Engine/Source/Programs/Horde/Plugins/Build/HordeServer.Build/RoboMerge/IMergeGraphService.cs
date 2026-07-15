// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.RoboMerge
{
	/// <summary>
	/// Abstraction for merge topology — not coupled to RoboMerge specifics.
	/// Other Horde services (e.g. dashboard cross-stream status) use this interface.
	/// All methods are synchronous reads against an in-memory snapshot.
	/// </summary>
	public interface IMergeGraphService
	{
		/// <summary>
		/// Get all available merge graphs (one per bot)
		/// </summary>
		IReadOnlyList<MergeGraph> GetMergeGraphs();

		/// <summary>
		/// Get a specific bot's merge graph by name or alias (case-insensitive).
		/// Checks the primary bot name first, then falls back to alias lookup.
		/// </summary>
		MergeGraph? GetMergeGraph(string botName);

		/// <summary>
		/// Find all branches across all bots that map to a given Perforce stream path
		/// (e.g. "//Fortnite/Main"). Useful for finding which bots manage a given Horde stream.
		/// </summary>
		IReadOnlyList<MergeGraphBranchRef> FindBranchesForStreamPath(string streamPath);

		/// <summary>
		/// Find all branches across all bots that map to a given stream depot + stream name.
		/// Useful for finding which bots manage a given Horde stream.
		/// </summary>
		IReadOnlyList<MergeGraphBranchRef> FindBranchesForStream(string streamDepot, string streamName);

		/// <summary>
		/// Get the ordered merge chain (release spine) for a bot.
		/// Returns branches ordered from oldest release at [0] to Main at [N-1], following forceFlowTo.
		/// Returns null if the bot doesn't exist or has no valid spine.
		/// </summary>
		MergeChain? GetMergeChain(string botName);

		/// <summary>
		/// Get all computed merge chains across all bots.
		/// Returns only bots that have a valid release spine.
		/// </summary>
		IReadOnlyList<MergeChain> GetMergeChains();

		/// <summary>
		/// Get the set of branches reachable from a given branch by following flowsTo transitively.
		/// Useful for answering "where will a commit on this branch eventually flow?"
		/// Uses the pre-computed reachability index for O(1) lookup.
		/// Returns empty if the bot or branch doesn't exist.
		/// </summary>
		IReadOnlySet<string> GetReachableBranches(string botName, string branchName);

		/// <summary>
		/// Get the edge definition between two branches in a bot, if one exists.
		/// Uses the pre-built edge index for O(1) lookup.
		/// </summary>
		MergeEdge? GetEdge(string botName, string fromBranch, string toBranch);

		/// <summary>
		/// When the last successful tick completed. Null if no tick has completed yet.
		/// Useful for operational diagnostics — distinguishes "never ticked" from "ticked but all files failed".
		/// </summary>
		DateTime? LastTickUtc { get; }
	}
}
