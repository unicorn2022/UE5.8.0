// Copyright Epic Games, Inc. All Rights Reserved.

// Includes
#include "Net/RPCDoSDetectionAnalyticsImpl.h"
#include "Net/Core/Connection/RPCDoSDetectionConfig.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "EngineLogs.h"
#include "Engine/World.h"


namespace UE
{
	namespace Net
	{
		/**
		 * For Grafana template variable dropdown, restrict CPU values (including overshoot) to:
		 * 0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 100, 105, 110 etc.
		 *
		 * @param InVal		The CPU Usage value to quantize
		 * @return			The quantized CPU Usage value
		 */
		uint8 GetQuantizedCPUUsage(uint8 InVal)
		{
			if (InVal >= 10 && InVal <= 90)
			{
				return (InVal / 10) * 10;
			}
			else
			{
				return (InVal / 5) * 5;
			}
		}
	}
}


/**
 * FRPCDoSAnalyticsSenderImpl
 */

void FRPCDoSAnalyticsSenderImpl::Init(FGetWorld&& InWorldFunc)
{
	WorldFunc = MoveTemp(InWorldFunc);
}

void FRPCDoSAnalyticsSenderImpl::SendAnalytics(FNetAnalyticsAggregator* Aggregator, FRPCDoSAnalyticsData* Data)
{
	using namespace UE::Net;

	FRPCDoSAnalyticsVars NullVars;
	const TSharedPtr<IAnalyticsProvider>& AnalyticsProvider = Aggregator->GetAnalyticsProvider();

	if (!(*Data == NullVars) && AnalyticsProvider.IsValid())
	{
		URPCDoSDetectionConfig* CurConfigObj = URPCDoSDetectionConfig::Get(Aggregator->GetNetDriverName());
		const bool bOverrideRPCThresholds = (CurConfigObj != nullptr && FMath::FRand() < CurConfigObj->RPCAnalyticsOverrideChance);

		auto WithinAnalyticsConfigThresholds =
			[CurConfigObj](const FRPCAnalytics& RPC) -> bool
			{
				bool bReturnVal = true;

				if (CurConfigObj != nullptr)
				{
					const FRPCAnalyticsThreshold* RPCThresholdConfig = CurConfigObj->RPCAnalyticsThresholds.FindByPredicate(
						[RPC](const FRPCAnalyticsThreshold& CurEntry)
						{
							return CurEntry.RPC == RPC.RPCName;
						});

					if (RPCThresholdConfig != nullptr && (RPCThresholdConfig->CountPerSec != -1 || RPCThresholdConfig->TimePerSec != 0.0))
					{
						const bool bHitCountThreshold = RPCThresholdConfig->CountPerSec != -1 && RPC.MaxCountPerSec > RPCThresholdConfig->CountPerSec;
						const bool bHitTimeThreshold = RPCThresholdConfig->TimePerSec != 0.0 && RPC.MaxTimePerSec > RPCThresholdConfig->TimePerSec;

						bReturnVal = bHitCountThreshold || bHitTimeThreshold;
					}
				}

				return bReturnVal;
			};


		FRPCDoSAnalyticsVars* DataVars = Data->GetVars();

		// Remove RPC's from main analytics, which don't meet minimum thresholds
		TArray<TSharedPtr<FRPCAnalytics>> FilteredRPCs;
		TArray<TSharedPtr<FRPCAnalytics>>& RPCTrackingAnalytics = DataVars->RPCTrackingAnalytics;

		for (int32 RPCIdx=RPCTrackingAnalytics.Num()-1; RPCIdx>=0; RPCIdx--)
		{
			const FRPCAnalytics& CurRPC = *RPCTrackingAnalytics[RPCIdx].Get();

			if (!CurRPC.WithinMinAnalyticsThreshold())
			{
				RPCTrackingAnalytics.RemoveAt(RPCIdx, EAllowShrinking::No);
			}
			else if (!WithinAnalyticsConfigThresholds(CurRPC))
			{
				FilteredRPCs.Add(RPCTrackingAnalytics[RPCIdx]);
				RPCTrackingAnalytics.RemoveAt(RPCIdx, EAllowShrinking::No);
			}
		}

		UE_LOGF(LogNet, Log, "RPCDosDetection Analytics:");

		UE_LOGF(LogNet, Log, " - MaxSeverityIndex: %i", DataVars->MaxSeverityIndex);
		UE_LOGF(LogNet, Log, " - MaxSeverityCategory: %ls", ToCStr(DataVars->MaxSeverityCategory));
		UE_LOGF(LogNet, Log, " - MaxAnalyticsSeverityIndex: %i", DataVars->MaxAnalyticsSeverityIndex);
		UE_LOGF(LogNet, Log, " - MaxAnalyticsSeverityCategory: %ls", ToCStr(DataVars->MaxAnalyticsSeverityCategory));
		UE_LOGF(LogNet, Log, " - RPCs: %i", RPCTrackingAnalytics.Num());

		// NOTE: Game thread CPU must be in analytics, even if GTrackGameThreadCPUUsage == 0, as it's used for filtering.
		for (int32 RPCIdx=0; RPCIdx<RPCTrackingAnalytics.Num(); RPCIdx++)
		{
			const FRPCAnalytics& CurRPC = *RPCTrackingAnalytics[RPCIdx].Get();

			UE_LOGF(LogNet, Log, "  - RPC[%i]:", RPCIdx);
			UE_LOGF(LogNet, Log, "   - Name: %ls", *CurRPC.RPCName.ToString());
			UE_LOGF(LogNet, Log, "   - MaxCountPerSec: %i", CurRPC.MaxCountPerSec);
			UE_LOGF(LogNet, Log, "   - MaxTimePerSec: %f", CurRPC.MaxTimePerSec);
			UE_LOGF(LogNet, Log, "   - MaxTimeGameThreadCPU: %i", CurRPC.MaxTimeGameThreadCPU);
			UE_LOGF(LogNet, Log, "   - MaxSinglePacketRPCTime: %f", CurRPC.MaxSinglePacketRPCTime);
			UE_LOGF(LogNet, Log, "   - SinglePacketRPCCount: %i", CurRPC.SinglePacketRPCCount);
			UE_LOGF(LogNet, Log, "   - SinglePacketGameThreadCPU: %i", CurRPC.SinglePacketGameThreadCPU);
			UE_LOGF(LogNet, Log, "   - BlockedCount: %i", CurRPC.BlockedCount);
			UE_LOGF(LogNet, Log, "   - PlayerIP: %ls", *CurRPC.PlayerIP);
			UE_LOGF(LogNet, Log, "   - PlayerUID: %ls", *CurRPC.PlayerUID);
		}

		UE_LOGF(LogNet, Log, " - FilteredRPCs: %i (bOverrideRPCThresholds: %i)", FilteredRPCs.Num(), (int32)bOverrideRPCThresholds);

		for (int32 RPCIdx=0; RPCIdx<FilteredRPCs.Num(); RPCIdx++)
		{
			const FRPCAnalytics& CurRPC = *FilteredRPCs[RPCIdx].Get();

			UE_LOGF(LogNet, Log, "  - FilteredRPC[%i]:", RPCIdx);
			UE_LOGF(LogNet, Log, "   - Name: (*) %ls", *CurRPC.RPCName.ToString());
			UE_LOGF(LogNet, Log, "   - MaxCountPerSec: %i", CurRPC.MaxCountPerSec);
			UE_LOGF(LogNet, Log, "   - MaxTimePerSec: %f", CurRPC.MaxTimePerSec);
			UE_LOGF(LogNet, Log, "   - MaxTimeGameThreadCPU: %i", CurRPC.MaxTimeGameThreadCPU);
			UE_LOGF(LogNet, Log, "   - MaxSinglePacketRPCTime: %f", CurRPC.MaxSinglePacketRPCTime);
			UE_LOGF(LogNet, Log, "   - SinglePacketRPCCount: %i", CurRPC.SinglePacketRPCCount);
			UE_LOGF(LogNet, Log, "   - SinglePacketGameThreadCPU: %i", CurRPC.SinglePacketGameThreadCPU);
			UE_LOGF(LogNet, Log, "   - BlockedCount: %i", CurRPC.BlockedCount);
			UE_LOGF(LogNet, Log, "   - PlayerIP: %ls", *CurRPC.PlayerIP);
			UE_LOGF(LogNet, Log, "   - PlayerUID: %ls", *CurRPC.PlayerUID);
		}

		TArray<FMaxRPCDoSEscalation>& MaxPlayerSeverity = DataVars->MaxPlayerSeverity;

		UE_LOGF(LogNet, Log, " - MaxPlayerSeverity: %i", MaxPlayerSeverity.Num());

		for (int32 SevIdx=0; SevIdx<MaxPlayerSeverity.Num(); SevIdx++)
		{
			const FMaxRPCDoSEscalation& CurSev = MaxPlayerSeverity[SevIdx];

			UE_LOGF(LogNet, Log, "  - MaxPlayerSeverity[%i]:", SevIdx);
			UE_LOGF(LogNet, Log, "   - PlayerIP: %ls", *CurSev.PlayerIP);
			UE_LOGF(LogNet, Log, "   - PlayerUID: %ls", *CurSev.PlayerUID);
			UE_LOGF(LogNet, Log, "   - MaxSeverityIndex: %i", CurSev.MaxSeverityIndex);
			UE_LOGF(LogNet, Log, "   - MaxSeverityCategory: %ls", *CurSev.MaxSeverityCategory);
			UE_LOGF(LogNet, Log, "   - MaxAnalyticsSeverityIndex: %i", CurSev.MaxAnalyticsSeverityIndex);
			UE_LOGF(LogNet, Log, "   - MaxAnalyticsSeverityCategory: %ls", *CurSev.MaxAnalyticsSeverityCategory);
		}


		static const FString EZEventName								= "Core.ServerRPCDoS";
		static const FString EZAttrib_MaxSeverityIndex					= "MaxSeverityIndex";
		static const FString EZAttrib_MaxSeverityCategory				= "MaxSeverityCategory";
		static const FString EZAttrib_MaxAnalyticsSeverityIndex			= "MaxAnalyticsSeverityIndex";
		static const FString EZAttrib_MaxAnalyticsSeverityCategory		= "MaxAnalyticsSeverityCategory";
		static const FString EZAttrib_RPCs								= "RPCs";
		static const FString EZAttrib_FilteredRPCs						= "FilteredRPCs";
		static const FString EZAttrib_RPCName							= "RPCName";
		static const FString EZAttrib_MaxCountPerSec					= "MaxCountPerSec";
		static const FString EZAttrib_MaxTimePerSec						= "MaxTimePerSec";
		static const FString EZAttrib_MaxTimeGameThreadCPU				= "MaxTimeGameThreadCPU";
		static const FString EZAttrib_MaxSinglePacketRPCTime			= "MaxSinglePacketRPCTime";
		static const FString EZAttrib_SinglePacketRPCCount				= "SinglePacketRPCCount";
		static const FString EZAttrib_SinglePacketGameThreadCPU			= "SinglePacketGameThreadCPU";
		static const FString EZAttrib_BlockedCount						= "BlockedCount";
		static const FString EZAttrib_PlayerIP							= "PlayerIP";
		static const FString EZAttrib_PlayerUID							= "PlayerUID";
		static const FString EZAttrib_MaxPlayerSeverity					= "MaxPlayerSeverity";


		// Json writer subclass to allow us to avoid using a SharedPtr to write basic Json (FN copy paste)
		typedef TCondensedJsonPrintPolicy<TCHAR> FPrintPolicy;
		class FAnalyticsJsonWriter : public TJsonStringWriter<FPrintPolicy>
		{
		public:
			explicit FAnalyticsJsonWriter(FString* Out) : TJsonStringWriter<FPrintPolicy>(Out, 0)
			{
			}
		};

		FString RPCsJsonStr;
		FAnalyticsJsonWriter RPCsJsonWriter(&RPCsJsonStr);

		RPCsJsonWriter.WriteArrayStart();

		for (int32 RPCIdx=0; RPCIdx<RPCTrackingAnalytics.Num(); RPCIdx++)
		{
			const FRPCAnalytics& CurRPC = *RPCTrackingAnalytics[RPCIdx].Get();

			RPCsJsonWriter.WriteObjectStart();

			RPCsJsonWriter.WriteValue(EZAttrib_RPCName, CurRPC.RPCName.ToString());
			RPCsJsonWriter.WriteValue(EZAttrib_MaxCountPerSec, CurRPC.MaxCountPerSec);
			RPCsJsonWriter.WriteValue(EZAttrib_MaxTimePerSec, CurRPC.MaxTimePerSec);
			RPCsJsonWriter.WriteValue(EZAttrib_MaxTimeGameThreadCPU, GetQuantizedCPUUsage(CurRPC.MaxTimeGameThreadCPU));
			RPCsJsonWriter.WriteValue(EZAttrib_MaxSinglePacketRPCTime, CurRPC.MaxSinglePacketRPCTime);
			RPCsJsonWriter.WriteValue(EZAttrib_SinglePacketRPCCount, CurRPC.SinglePacketRPCCount);
			RPCsJsonWriter.WriteValue(EZAttrib_SinglePacketGameThreadCPU, GetQuantizedCPUUsage(CurRPC.SinglePacketGameThreadCPU));
			RPCsJsonWriter.WriteValue(EZAttrib_BlockedCount, CurRPC.BlockedCount);
			RPCsJsonWriter.WriteValue(EZAttrib_PlayerIP, CurRPC.PlayerIP);
			RPCsJsonWriter.WriteValue(EZAttrib_PlayerUID, CurRPC.PlayerUID);

			RPCsJsonWriter.WriteObjectEnd();
		}

		RPCsJsonWriter.WriteArrayEnd();
		RPCsJsonWriter.Close();

		FString FilteredRPCsJsonStr;
		FAnalyticsJsonWriter FilteredRPCsJsonWriter(&FilteredRPCsJsonStr);

		FilteredRPCsJsonWriter.WriteArrayStart();

		if (bOverrideRPCThresholds)
		{
			for (int32 RPCIdx=0; RPCIdx<FilteredRPCs.Num(); RPCIdx++)
			{
				const FRPCAnalytics& CurRPC = *FilteredRPCs[RPCIdx].Get();

				FilteredRPCsJsonWriter.WriteObjectStart();

				TStringBuilder<512> RPCName;

				RPCName.Append(TEXT("(*) "));
				RPCName.Append(ToCStr(CurRPC.RPCName.ToString()));

				FilteredRPCsJsonWriter.WriteValue(EZAttrib_RPCName, RPCName.ToString());
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_MaxCountPerSec, CurRPC.MaxCountPerSec);
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_MaxTimePerSec, CurRPC.MaxTimePerSec);
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_MaxTimeGameThreadCPU, GetQuantizedCPUUsage(CurRPC.MaxTimeGameThreadCPU));
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_MaxSinglePacketRPCTime, CurRPC.MaxSinglePacketRPCTime);
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_SinglePacketRPCCount, CurRPC.SinglePacketRPCCount);
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_SinglePacketGameThreadCPU, GetQuantizedCPUUsage(CurRPC.SinglePacketGameThreadCPU));
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_BlockedCount, CurRPC.BlockedCount);
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_PlayerIP, CurRPC.PlayerIP);
				FilteredRPCsJsonWriter.WriteValue(EZAttrib_PlayerUID, CurRPC.PlayerUID);

				FilteredRPCsJsonWriter.WriteObjectEnd();
			}
		}

		FilteredRPCsJsonWriter.WriteArrayEnd();
		FilteredRPCsJsonWriter.Close();


		FString SevJsonStr;
		FAnalyticsJsonWriter SevJsonWriter(&SevJsonStr);

		SevJsonWriter.WriteArrayStart();

		for (int32 SevIdx=0; SevIdx<MaxPlayerSeverity.Num(); SevIdx++)
		{
			const FMaxRPCDoSEscalation& CurSev = MaxPlayerSeverity[SevIdx];

			SevJsonWriter.WriteObjectStart();

			SevJsonWriter.WriteValue(EZAttrib_PlayerIP, CurSev.PlayerIP);
			SevJsonWriter.WriteValue(EZAttrib_PlayerUID, CurSev.PlayerUID);
			SevJsonWriter.WriteValue(EZAttrib_MaxSeverityIndex, CurSev.MaxSeverityIndex);
			SevJsonWriter.WriteValue(EZAttrib_MaxSeverityCategory, CurSev.MaxSeverityCategory);
			SevJsonWriter.WriteValue(EZAttrib_MaxAnalyticsSeverityIndex, CurSev.MaxAnalyticsSeverityIndex);
			SevJsonWriter.WriteValue(EZAttrib_MaxAnalyticsSeverityCategory, CurSev.MaxAnalyticsSeverityCategory);

			SevJsonWriter.WriteObjectEnd();
		}

		SevJsonWriter.WriteArrayEnd();
		SevJsonWriter.Close();


		UWorld* World = WorldFunc ? WorldFunc() : nullptr;
		TArray<FAnalyticsEventAttribute> RPCDoSAttrs = MakeAnalyticsEventAttributeArray(
				EZAttrib_MaxSeverityIndex, DataVars->MaxSeverityIndex,
				EZAttrib_MaxSeverityCategory, DataVars->MaxSeverityCategory,
				EZAttrib_MaxAnalyticsSeverityIndex, DataVars->MaxAnalyticsSeverityIndex,
				EZAttrib_MaxAnalyticsSeverityCategory, DataVars->MaxAnalyticsSeverityCategory,
				EZAttrib_RPCs, FJsonFragment(MoveTemp(RPCsJsonStr)),
				EZAttrib_FilteredRPCs, FJsonFragment(MoveTemp(FilteredRPCsJsonStr)),
				EZAttrib_MaxPlayerSeverity, FJsonFragment(MoveTemp(SevJsonStr)));

		GModifyRPCDoSAnalytics.Broadcast(World, RPCDoSAttrs);

		AnalyticsProvider->RecordEvent(EZEventName, RPCDoSAttrs);
	}
}

void FRPCDoSAnalyticsSenderImpl::FireEvent_ServerRPCDoSEscalation(FNetAnalyticsAggregator* Aggregator, FRPCDoSAnalyticsData* Data,
																	int32 SeverityIndex, const FString& SeverityCategory,
																	int32 WorstCountPerSec, double WorstTimePerSec,
																	const FString& InPlayerIP, const FString& InPlayerUID,
																	const TArray<FName>& InRPCGroup, double InRPCGroupTime/*=0.0*/)
{
	using namespace UE::Net;

	const TSharedPtr<IAnalyticsProvider>& AnalyticsProvider = Aggregator->GetAnalyticsProvider();

	if (AnalyticsProvider.IsValid())
	{
		static const FString EZEventName							= "Core.ServerRPCDoSEscalation";
		static const FString EZAttrib_AnalyticsSeverityIndex		= "AnalyticsSeverityIndex";
		static const FString EZAttrib_AnalyticsSeverityCategory		= "AnalyticsSeverityCategory";
		static const FString EZAttrib_WorstCountPerSec				= "WorstCountPerSec";
		static const FString EZAttrib_WorstTimePerSec				= "WorstTimePerSec";
		static const FString EZAttrib_PlayerIP						= "PlayerIP";
		static const FString EZAttrib_PlayerUID						= "PlayerUID";
		static const FString EZAttrib_GameThreadCPU					= "GameThreadCPU";
		static const FString EZAttrib_RPCGroup						= "RPCGroup";
		static const FString EZAttrib_RPCGroupTime					= "RPCGroupTime";

		UWorld* World = WorldFunc ? WorldFunc() : nullptr;
		TArray<FAnalyticsEventAttribute> RPCDoSEscalationAttrs = MakeAnalyticsEventAttributeArray(
				EZAttrib_AnalyticsSeverityIndex, SeverityIndex,
				EZAttrib_AnalyticsSeverityCategory, SeverityCategory,
				EZAttrib_WorstCountPerSec, WorstCountPerSec,
				EZAttrib_WorstTimePerSec, WorstTimePerSec,
				EZAttrib_PlayerIP, InPlayerIP,
				EZAttrib_PlayerUID, InPlayerUID,
				// NOTE: Game thread CPU must be in analytics, even if GTrackGameThreadCPUUsage == 0, as it's used for filtering.
				EZAttrib_GameThreadCPU, GetQuantizedCPUUsage(static_cast<uint8>(FPlatformTime::GetThreadCPUTime().CPUTimePctRelative)));

		if (InRPCGroup.Num() > 0)
		{
			TStringBuilder<2048> RPCGroupStr;

			for (const FName& CurRPC : InRPCGroup)
			{
				if (RPCGroupStr.Len() > 0)
				{
					RPCGroupStr.Append(TEXT(", "));
				}

				RPCGroupStr.Append(*CurRPC.ToString());
			}

			TArray<FAnalyticsEventAttribute> RPCGroupAttrs = MakeAnalyticsEventAttributeArray(
					EZAttrib_RPCGroup, RPCGroupStr.ToString(),
					EZAttrib_RPCGroupTime, InRPCGroupTime);

			RPCDoSEscalationAttrs.Append(RPCGroupAttrs);
		}

		GModifyRPCDoSEscalationAnalytics.Broadcast(World, RPCDoSEscalationAttrs);

		AnalyticsProvider->RecordEvent(EZEventName, RPCDoSEscalationAttrs);
	}
}


