// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;

namespace P4VUtils.Perforce
{
	/// <summary>Groups of Perforce file actions used for filtering and categorization</summary>
	public static class P4ActionGroups
	{
		/// <summary>Integrate actions that bring changes from another stream/branch</summary>
		public static readonly IntegrateAction[] IntegrateFromActions =
		{
			IntegrateAction.BranchFrom,
			IntegrateAction.MergeFrom,
			IntegrateAction.MovedFrom,
			IntegrateAction.CopyFrom,
			IntegrateAction.DeleteFrom,
			IntegrateAction.EditFrom,
			IntegrateAction.AddFrom
		};

		/// <summary>Actions representing direct edits to a file (add, edit, delete)</summary>
		public static readonly FileAction[] EditActions =
		{
			FileAction.Add,
			FileAction.Edit,
			FileAction.Delete
		};

		/// <summary>Actions representing file moves</summary>
		public static readonly FileAction[] MoveActions =
		{
			FileAction.MoveAdd,
			FileAction.MoveDelete
		};

		/// <summary>Actions representing integrations from another branch</summary>
		public static readonly FileAction[] IntegrateActions =
		{
			FileAction.Integrate,
			FileAction.Branch,
		};
	}
}
