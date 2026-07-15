// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Immutable;

namespace HordeServer.RoboMerge
{
	/// <summary>
	/// A merge graph represents one RoboMerge bot's branch topology
	/// </summary>
	public class MergeGraph
	{
		/// <summary>Bot name derived from branchmap filename</summary>
		public string BotName { get; init; } = string.Empty;

		/// <summary>Default Perforce stream depot (e.g. "Fortnite", "UE5")</summary>
		public string DefaultStreamDepot { get; init; } = string.Empty;

		/// <summary>Whether this is the default bot for its depot</summary>
		public bool IsDefaultBot { get; init; }

		/// <summary>Alternative names for this bot</summary>
		public IReadOnlyList<string> Aliases { get; init; } = Array.Empty<string>();

		/// <summary>Visibility/access tags (e.g. ["fn", "fte"])</summary>
		public IReadOnlyList<string> Visibility { get; init; } = Array.Empty<string>();

		/// <summary>Whether this bot reports to Horde's build health system</summary>
		public bool ReportToBuildHealth { get; init; }

		/// <summary>Authors excluded from merge operations</summary>
		public IReadOnlyList<string> ExcludeAuthors { get; init; } = Array.Empty<string>();

		/// <summary>Slack channel for this bot's notifications</summary>
		public string? SlackChannel { get; init; }

		/// <summary>Branches (nodes) in the merge graph</summary>
		public IReadOnlyList<MergeBranch> Branches { get; init; } = Array.Empty<MergeBranch>();

		/// <summary>Edges with special merge behavior</summary>
		public IReadOnlyList<MergeEdge> Edges { get; init; } = Array.Empty<MergeEdge>();

		/// <summary>Named macro definitions — keys are macro names, values are integration command templates (e.g. "#robomerge[bot1] Main")</summary>
		public IReadOnlyDictionary<string, IReadOnlyList<string>> Macros { get; init; }
			= ImmutableDictionary<string, IReadOnlyList<string>>.Empty;

		/// <summary>Perforce head changelist of the branchmap file (for staleness detection)</summary>
		public int HeadChange { get; init; }

		/// <summary>When this graph was last read from Perforce</summary>
		public DateTime LastRefreshedUtc { get; init; }
	}

	/// <summary>
	/// A branch (node) in the merge graph
	/// </summary>
	public class MergeBranch
	{
		/// <summary>Unique branch identifier within the bot</summary>
		public string Name { get; init; } = string.Empty;

		/// <summary>Alternative names for this branch</summary>
		public IReadOnlyList<string> Aliases { get; init; } = Array.Empty<string>();

		/// <summary>Perforce stream name (may differ from Name)</summary>
		public string? StreamName { get; init; }

		/// <summary>Perforce stream depot override</summary>
		public string? StreamDepot { get; init; }

		/// <summary>All merge targets</summary>
		public IReadOnlyList<string> FlowsTo { get; init; } = Array.Empty<string>();

		/// <summary>Mandatory merge targets (the release spine)</summary>
		public IReadOnlyList<string> ForceFlowTo { get; init; } = Array.Empty<string>();

		/// <summary>Default merge targets (subset of FlowsTo that are merged by default)</summary>
		public IReadOnlyList<string> DefaultFlow { get; init; } = Array.Empty<string>();

		/// <summary>Targets where asset flow is blocked</summary>
		public IReadOnlyList<string> BlockAssetFlow { get; init; } = Array.Empty<string>();

		/// <summary>Whether deadend merges are disallowed</summary>
		public bool DisallowDeadend { get; init; }

		/// <summary>Whether skip merges are disallowed</summary>
		public bool DisallowSkip { get; init; }

		/// <summary>Whether this branch is disabled from integrations</summary>
		public bool Disabled { get; init; }

		/// <summary>Default resolver for merge conflicts</summary>
		public string? Resolver { get; init; }

		/// <summary>Badge project association</summary>
		public string? BadgeProject { get; init; }

		/// <summary>Color hint for dashboard graph visualization (e.g. "#FF0000")</summary>
		public string? GraphNodeColor { get; init; }

		/// <summary>Whether this branch is hidden from normal views</summary>
		public bool IncognitoMode { get; init; }

		/// <summary>Branch-level visibility/access tags (overrides graph-level visibility if set)</summary>
		public IReadOnlyList<string>? Visibility { get; init; }

		/// <summary>Branch-level macro definitions (overrides/extends graph-level macros for this branch)</summary>
		public IReadOnlyDictionary<string, IReadOnlyList<string>>? Macros { get; init; }

		/// <summary>
		/// Compute the full Perforce stream path for this branch (e.g. "//Fortnite/Main").
		/// Uses the branch's StreamDepot if set, otherwise falls back to the graph's DefaultStreamDepot.
		/// Uses the branch's StreamName if set, otherwise falls back to the branch Name.
		/// </summary>
		public string GetStreamPath(string defaultStreamDepot)
			=> $"//{StreamDepot ?? defaultStreamDepot}/{StreamName ?? Name}";
	}

	/// <summary>
	/// An edge with special merge behavior between two branches
	/// </summary>
	public class MergeEdge
	{
		/// <summary>Source branch name</summary>
		public string From { get; init; } = string.Empty;

		/// <summary>Target branch name</summary>
		public string To { get; init; } = string.Empty;

		/// <summary>Approval gate on this edge, if any</summary>
		public MergeApprovalGate? Approval { get; init; }

		/// <summary>Integration method override</summary>
		public string? IntegrationMethod { get; init; }

		/// <summary>Whether skip merges are disallowed on this edge</summary>
		public bool DisallowSkip { get; init; }

		/// <summary>Edge-specific conflict resolver (overrides branch-level resolver)</summary>
		public string? Resolver { get; init; }

		/// <summary>Whether this is a terminal edge — changes don't flow beyond this edge</summary>
		public bool Terminal { get; init; }

		/// <summary>Implicit commands applied to merges on this edge (e.g. "#robomerge[all]")</summary>
		public IReadOnlyList<string> ImplicitCommands { get; init; } = Array.Empty<string>();

		/// <summary>Custom branchspec for this edge, if any</summary>
		public string? Branchspec { get; init; }
	}

	/// <summary>
	/// Approval gate on a merge edge
	/// </summary>
	public class MergeApprovalGate
	{
		/// <summary>Human-readable description of the gate</summary>
		public string? Description { get; init; }

		/// <summary>Whether the gate blocks merges until approved</summary>
		public bool Block { get; init; }
	}

	/// <summary>
	/// Reference to a branch within a specific bot's graph
	/// </summary>
	public class MergeGraphBranchRef
	{
		/// <summary>Bot that owns this branch</summary>
		public string BotName { get; init; } = string.Empty;

		/// <summary>The branch</summary>
		public MergeBranch Branch { get; init; } = null!;

		/// <summary>Pre-computed full Perforce stream path (e.g. "//Fortnite/Main")</summary>
		public string StreamPath { get; init; } = string.Empty;
	}

	/// <summary>
	/// An ordered merge chain (the "release spine" of a bot)
	/// </summary>
	public class MergeChain
	{
		/// <summary>Bot name</summary>
		public string BotName { get; init; } = string.Empty;

		/// <summary>Chain entries ordered: oldest release at [0], Main at [N-1]</summary>
		public IReadOnlyList<MergeChainEntry> Entries { get; init; } = Array.Empty<MergeChainEntry>();
	}

	/// <summary>
	/// A single entry in a merge chain
	/// </summary>
	public class MergeChainEntry
	{
		/// <summary>The branch at this position in the chain</summary>
		public MergeBranch Branch { get; init; } = null!;

		/// <summary>Pre-computed full Perforce stream path (e.g. "//Fortnite/Main"), avoids extra graph lookup on read path</summary>
		public string StreamPath { get; init; } = string.Empty;

		/// <summary>Whether there's an approval gate between the previous entry and this one</summary>
		public bool HasApprovalGateBefore { get; init; }
	}
}
