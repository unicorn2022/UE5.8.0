// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Issues;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Users;

namespace HordeServer.Jobs
{
	/// <summary>
	/// Aggregated label details for the hover card — all data needed in a single response
	/// </summary>
	public class GetLabelDetailsResponse
	{
		/// <summary>Label display name</summary>
		public string? DashboardName { get; set; }

		/// <summary>Label category</summary>
		public string? DashboardCategory { get; set; }

		/// <summary>Current label state</summary>
		public LabelState State { get; set; }

		/// <summary>Current label outcome</summary>
		public LabelOutcome Outcome { get; set; }

		/// <summary>
		/// Steps in this label. When outcome is Failure or Warning, only failing/warning steps are included.
		/// When outcome is Success or Running, all steps are included (for the step count).
		/// Capped at 20 steps to bound response size.
		/// </summary>
		public List<GetLabelStepResponse> Steps { get; set; } = new();

		/// <summary>
		/// Log events for failing steps, keyed by logId string.
		/// Pre-fetched server-side in a single batch call.
		/// Only populated for steps with Failure outcome.
		/// </summary>
		public Dictionary<string, GetLabelLogEventsResponse> LogEvents { get; set; } = new();

		/// <summary>Total number of steps in the label (may exceed Steps.Count due to cap)</summary>
		public int TotalStepCount { get; set; }

		/// <summary>Build health issues affecting this label</summary>
		public List<GetLabelIssueResponse> Issues { get; set; } = new();

		/// <summary>
		/// Previous build: the matching label from the most recent earlier job
		/// in the same stream with the same template. Null if no previous job found.
		/// </summary>
		public GetLabelPreviousBuildResponse? PreviousBuild { get; set; }

		/// <summary>
		/// Cross-stream merge chain with label status per stream.
		/// Entries are pre-ordered by RoboMerge merge flow (oldest release at [0], Main at [N-1]).
		/// Null when IMergeGraphService has no chain for this stream.
		/// </summary>
		public GetLabelCrossStreamChainResponse? CrossStreamChain { get; set; }
	}

	/// <summary>Step summary within a label</summary>
	public class GetLabelStepResponse
	{
		/// <summary>Step ID</summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>Step name</summary>
		public string Name { get; set; } = String.Empty;

		/// <summary>Step state</summary>
		public JobStepState State { get; set; }

		/// <summary>Step outcome</summary>
		public JobStepOutcome Outcome { get; set; }

		/// <summary>Log ID (for linking to log viewer)</summary>
		public string? LogId { get; set; }

		/// <summary>Step start time</summary>
		public DateTime? StartTime { get; set; }

		/// <summary>Step finish time</summary>
		public DateTime? FinishTime { get; set; }

		/// <summary>
		/// Step diagnostic message extracted from step metadata (ReportStepMessage).
		/// Pre-extracted server-side so the frontend doesn't need getStepRefMessageInfo().
		/// Null if no diagnostic message is present.
		/// </summary>
		public string? DiagnosticMessage { get; set; }
	}

	/// <summary>Log events for a single step's log</summary>
	public class GetLabelLogEventsResponse
	{
		/// <summary>Total number of events matching the severity filter</summary>
		public int Total { get; set; }

		/// <summary>Event entries (capped at 20 per log), using the existing GetLogEventResponse DTO</summary>
		public List<GetLogEventResponse> Events { get; set; } = new();
	}

	/// <summary>Build health issue summary</summary>
	public class GetLabelIssueResponse
	{
		/// <summary>Issue ID</summary>
		public int Id { get; set; }

		/// <summary>Issue summary text</summary>
		public string Summary { get; set; } = String.Empty;

		/// <summary>Severity (Error, Warning, etc.)</summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>Current owner</summary>
		public GetThinUserInfoResponse? Owner { get; set; }

		/// <summary>Whether the issue has been acknowledged</summary>
		public bool Acknowledged { get; set; }

		/// <summary>Whether the issue is resolved in this stream</summary>
		public bool ResolvedInStream { get; set; }

		/// <summary>External issue key (e.g. Jira ticket like "FORT-123456"), null if not linked</summary>
		public string? ExternalIssueKey { get; set; }

		/// <summary>Workflow thread URL (Slack thread), null if not created</summary>
		public Uri? WorkflowThreadUrl { get; set; }
	}

	/// <summary>Previous build's label status</summary>
	public class GetLabelPreviousBuildResponse
	{
		/// <summary>Previous job ID</summary>
		public string JobId { get; set; } = String.Empty;

		/// <summary>Previous job's CL number</summary>
		public int Change { get; set; }

		/// <summary>The matching label's index in the previous job's graph</summary>
		public int LabelIndex { get; set; }

		/// <summary>The matching label's state in the previous job</summary>
		public LabelState State { get; set; }

		/// <summary>The matching label's outcome in the previous job</summary>
		public LabelOutcome Outcome { get; set; }
	}

	/// <summary>
	/// Cross-stream label status ordered by merge chain.
	/// Assembled server-side using IMergeGraphService (in-memory, zero I/O for topology).
	/// </summary>
	public class GetLabelCrossStreamChainResponse
	{
		/// <summary>Bot name from IMergeGraphService (e.g. "fortnite")</summary>
		public string BotName { get; set; } = String.Empty;

		/// <summary>
		/// Chain entries in merge-flow display order: oldest release at [0], Main at [N-1].
		/// Only includes streams that have the matching templateId.
		/// The current stream is included inline with IsCurrent=true.
		/// </summary>
		public List<GetLabelCrossStreamEntry> Entries { get; set; } = new();
	}

	/// <summary>Single entry in the cross-stream merge chain</summary>
	public class GetLabelCrossStreamEntry
	{
		/// <summary>Horde stream ID</summary>
		public string StreamId { get; set; } = String.Empty;

		/// <summary>Branch name from the merge chain (e.g. "Main", "Dev-FN-40")</summary>
		public string BranchName { get; set; } = String.Empty;

		/// <summary>Job ID of the latest job with matching template, null if none found</summary>
		public string? JobId { get; set; }

		/// <summary>Label state in the sibling stream's latest job, null if no matching label</summary>
		public LabelState? State { get; set; }

		/// <summary>Label outcome in the sibling stream's latest job</summary>
		public LabelOutcome? Outcome { get; set; }

		/// <summary>Whether there's an approval gate between the previous chain entry and this one</summary>
		public bool HasApprovalGateBefore { get; set; }

		/// <summary>Whether this is the current stream (the job being hovered)</summary>
		public bool IsCurrent { get; set; }
	}
}
