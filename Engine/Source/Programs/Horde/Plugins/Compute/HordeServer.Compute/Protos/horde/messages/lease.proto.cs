// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Agents.Leases;

namespace HordeCommon.Rpc.Messages
{
	/// <summary>
	/// Additional methods for RpcLease
	/// </summary>
	partial class RpcLease
	{
		/// <summary>
		/// Typed accessor for the <see cref="RpcLease.IdString"/> field.
		/// </summary>
		public LeaseId Id
		{
			get => LeaseId.Parse(IdString);
			set => IdString = value.ToString();
		}

		/// <summary>
		/// Convert outcome reason(s) from Protobuf-format to C# enum
		/// </summary>
		public LeaseOutcomeReason ToNativeOutcomeReason()
		{
			return OutcomeReasons
				.Select(r => (LeaseOutcomeReason)(long)r)
				.Aggregate(LeaseOutcomeReason.None, (current, reason) => current | reason);
		}
		
		/// <summary>
		/// Convert C# enum with outcome reason(s) to Protobuf-format
		/// </summary>
		public static List<RpcLeaseOutcomeReason> ToProtobufOutcomeReasons(LeaseOutcomeReason reasons)
		{
			List<RpcLeaseOutcomeReason> result = [];
			foreach (RpcLeaseOutcomeReason rpcReason in Enum.GetValues(typeof(RpcLeaseOutcomeReason)))
			{
				if (rpcReason != RpcLeaseOutcomeReason.None && ((long)reasons & (long)rpcReason) != 0)
				{
					result.Add(rpcReason);
				}
			}
    
			return result;
		}
	}
}
