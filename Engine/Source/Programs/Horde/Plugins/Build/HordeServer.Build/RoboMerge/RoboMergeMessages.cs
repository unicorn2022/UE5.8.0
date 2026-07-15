// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.RoboMerge
{
	/// <summary>
	/// Overall status of the RoboMerge merge graph service
	/// </summary>
	public class GetRoboMergeStatusResponse
	{
		/// <summary>Whether the service is configured with branchmap files</summary>
		public bool IsConfigured { get; set; }

		/// <summary>Whether graph data has been loaded (false between startup and first tick)</summary>
		public bool GraphsLoaded { get; set; }

		/// <summary>Number of merge graphs currently loaded</summary>
		public int GraphCount { get; set; }

		/// <summary>Names of all loaded bots</summary>
		public List<string> BotNames { get; set; } = new List<string>();

		/// <summary>When the last tick completed (null if no tick has completed yet)</summary>
		public DateTime? LastTickUtc { get; set; }
	}

	/// <summary>
	/// Summary of a merge graph (for list endpoint)
	/// </summary>
	public class GetMergeGraphSummaryResponse
	{
		/// <summary>Bot name</summary>
		public string BotName { get; set; } = string.Empty;

		/// <summary>Default Perforce stream depot</summary>
		public string DefaultStreamDepot { get; set; } = string.Empty;

		/// <summary>Number of branches in the graph</summary>
		public int BranchCount { get; set; }

		/// <summary>Number of edges in the graph</summary>
		public int EdgeCount { get; set; }

		/// <summary>Whether this bot reports to build health</summary>
		public bool ReportToBuildHealth { get; set; }

		/// <summary>Perforce head changelist of the branchmap file</summary>
		public int HeadChange { get; set; }

		/// <summary>When this graph was last read from Perforce</summary>
		public DateTime LastRefreshedUtc { get; set; }
	}

	/// <summary>
	/// Full merge graph for a single bot
	/// </summary>
	public class GetMergeGraphResponse
	{
		/// <summary>Bot name</summary>
		public string BotName { get; set; } = string.Empty;

		/// <summary>Default Perforce stream depot</summary>
		public string DefaultStreamDepot { get; set; } = string.Empty;

		/// <summary>Whether this is the default bot for its depot</summary>
		public bool IsDefaultBot { get; set; }

		/// <summary>Alternative bot names</summary>
		public List<string> Aliases { get; set; } = new List<string>();

		/// <summary>Visibility/access tags</summary>
		public List<string> Visibility { get; set; } = new List<string>();

		/// <summary>Whether this bot reports to build health</summary>
		public bool ReportToBuildHealth { get; set; }

		/// <summary>Authors excluded from merge operations</summary>
		public List<string> ExcludeAuthors { get; set; } = new List<string>();

		/// <summary>Slack channel for this bot's notifications</summary>
		public string? SlackChannel { get; set; }

		/// <summary>Perforce head changelist of the branchmap file</summary>
		public int HeadChange { get; set; }

		/// <summary>When this graph was last read from Perforce</summary>
		public DateTime LastRefreshedUtc { get; set; }

		/// <summary>Branches (nodes) in the graph</summary>
		public List<GetMergeBranchResponse> Branches { get; set; } = new List<GetMergeBranchResponse>();

		/// <summary>Edges with special merge behavior</summary>
		public List<GetMergeEdgeResponse> Edges { get; set; } = new List<GetMergeEdgeResponse>();

		/// <summary>Named macro definitions</summary>
		public Dictionary<string, List<string>>? Macros { get; set; }
	}

	/// <summary>
	/// A branch in a merge graph
	/// </summary>
	public class GetMergeBranchResponse
	{
		/// <summary>Branch name</summary>
		public string Name { get; set; } = string.Empty;

		/// <summary>Alternative branch names</summary>
		public List<string> Aliases { get; set; } = new List<string>();

		/// <summary>Perforce stream name</summary>
		public string? StreamName { get; set; }

		/// <summary>Perforce stream depot override</summary>
		public string? StreamDepot { get; set; }

		/// <summary>Full Perforce stream path (e.g. "//Fortnite/Main")</summary>
		public string StreamPath { get; set; } = string.Empty;

		/// <summary>All merge targets</summary>
		public List<string> FlowsTo { get; set; } = new List<string>();

		/// <summary>Mandatory merge targets</summary>
		public List<string> ForceFlowTo { get; set; } = new List<string>();

		/// <summary>Default merge targets</summary>
		public List<string> DefaultFlow { get; set; } = new List<string>();

		/// <summary>Targets where asset flow is blocked</summary>
		public List<string> BlockAssetFlow { get; set; } = new List<string>();

		/// <summary>Whether deadend merges are disallowed</summary>
		public bool DisallowDeadend { get; set; }

		/// <summary>Whether skip merges are disallowed</summary>
		public bool DisallowSkip { get; set; }

		/// <summary>Whether this branch is disabled from integrations</summary>
		public bool Disabled { get; set; }

		/// <summary>Default resolver for merge conflicts</summary>
		public string? Resolver { get; set; }

		/// <summary>Badge project association</summary>
		public string? BadgeProject { get; set; }

		/// <summary>Color hint for dashboard graph visualization</summary>
		public string? GraphNodeColor { get; set; }

		/// <summary>Whether this branch is hidden from normal views</summary>
		public bool IncognitoMode { get; set; }

		/// <summary>Branch-level visibility/access tags (null if inheriting from graph)</summary>
		public List<string>? Visibility { get; set; }

		/// <summary>Branch-level macro definitions (null if no branch-level macros)</summary>
		public Dictionary<string, List<string>>? Macros { get; set; }
	}

	/// <summary>
	/// A merge edge
	/// </summary>
	public class GetMergeEdgeResponse
	{
		/// <summary>Source branch name</summary>
		public string From { get; set; } = string.Empty;

		/// <summary>Target branch name</summary>
		public string To { get; set; } = string.Empty;

		/// <summary>Approval gate, if any</summary>
		public GetMergeApprovalGateResponse? Approval { get; set; }

		/// <summary>Integration method override</summary>
		public string? IntegrationMethod { get; set; }

		/// <summary>Whether skip merges are disallowed</summary>
		public bool DisallowSkip { get; set; }

		/// <summary>Edge-specific conflict resolver</summary>
		public string? Resolver { get; set; }

		/// <summary>Whether this is a terminal edge (changes don't flow beyond)</summary>
		public bool Terminal { get; set; }

		/// <summary>Implicit commands applied to merges on this edge</summary>
		public List<string> ImplicitCommands { get; set; } = new List<string>();

		/// <summary>Custom branchspec for this edge</summary>
		public string? Branchspec { get; set; }
	}

	/// <summary>
	/// Approval gate on a merge edge
	/// </summary>
	public class GetMergeApprovalGateResponse
	{
		/// <summary>Gate description</summary>
		public string? Description { get; set; }

		/// <summary>Whether the gate blocks merges</summary>
		public bool Block { get; set; }
	}

	/// <summary>
	/// Merge chain for a bot
	/// </summary>
	public class GetMergeChainResponse
	{
		/// <summary>Bot name</summary>
		public string BotName { get; set; } = string.Empty;

		/// <summary>Chain entries, oldest release at [0], Main at [N-1]</summary>
		public List<GetMergeChainEntryResponse> Entries { get; set; } = new List<GetMergeChainEntryResponse>();
	}

	/// <summary>
	/// A single entry in a merge chain
	/// </summary>
	public class GetMergeChainEntryResponse
	{
		/// <summary>Branch name</summary>
		public string BranchName { get; set; } = string.Empty;

		/// <summary>Full Perforce stream path (e.g. "//Fortnite/Main")</summary>
		public string StreamPath { get; set; } = string.Empty;

		/// <summary>Whether there's an approval gate between the previous entry and this one</summary>
		public bool HasApprovalGateBefore { get; set; }
	}

	/// <summary>
	/// A branch reference (branch + owning bot name)
	/// </summary>
	public class GetMergeBranchRefResponse
	{
		/// <summary>Bot that owns this branch</summary>
		public string BotName { get; set; } = string.Empty;

		/// <summary>The branch</summary>
		public GetMergeBranchResponse Branch { get; set; } = null!;
	}

	/// <summary>
	/// Branches reachable from a given branch via transitive flowsTo
	/// </summary>
	public class GetReachableBranchesResponse
	{
		/// <summary>Bot name</summary>
		public string BotName { get; set; } = string.Empty;

		/// <summary>Starting branch name</summary>
		public string BranchName { get; set; } = string.Empty;

		/// <summary>Branch names reachable via transitive flowsTo (excludes the starting branch)</summary>
		public List<string> ReachableBranches { get; set; } = new List<string>();
	}
}
