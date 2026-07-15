// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLocalLiveLinkSubjectSettings.h"
#include "MetaHumanLocalLiveLinkSubject.h"

#include "Roles/LiveLinkBasicRole.h"
#include "InterpolationProcessor/LiveLinkBasicFrameInterpolateProcessor.h"



void UMetaHumanLocalLiveLinkSubjectSettings::Setup()
{
	Role = ULiveLinkBasicRole::StaticClass();
	InterpolationProcessor = NewObject<ULiveLinkBasicFrameInterpolationProcessor>(this);
}

void UMetaHumanLocalLiveLinkSubjectSettings::SetSubject(FMetaHumanLocalLiveLinkSubject* InSubject)
{
	Subject = InSubject;
	bIsLiveProcessing = true;

	Monitoring.Add(EMetaHumanLocalLiveLinkSubjectMonitoring::Basic, 0);
	Monitoring.Add(EMetaHumanLocalLiveLinkSubjectMonitoring::Advanced, 0);

	UpdateDelegate.AddUObject(this, &UMetaHumanLocalLiveLinkSubjectSettings::OnUpdate);
}

void UMetaHumanLocalLiveLinkSubjectSettings::ReloadSubject()
{
	if (Subject)
	{
		Subject->ReloadSubject();
	}
}

void UMetaHumanLocalLiveLinkSubjectSettings::RemoveSubject()
{
	if (Subject)
	{
		Subject->RemoveSubject();
	}
}

void UMetaHumanLocalLiveLinkSubjectSettings::SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring InMonitoringLevel, bool bInIsMonitoring)
{
	if (!Monitoring.Contains(InMonitoringLevel)) // should never happen, but keeps bughawk quiet.
	{
		Monitoring.Add(InMonitoringLevel, 0);
	}

	if (bInIsMonitoring)
	{
		Monitoring[InMonitoringLevel]++;
	}
	else if (Monitoring[InMonitoringLevel] > 0)
	{
		Monitoring[InMonitoringLevel]--;
	}

	if (bInIsMonitoring && Subject)
	{
		Subject->SendLatestUpdate();
	}
}

void UMetaHumanLocalLiveLinkSubjectSettings::OnUpdate(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
{
	if (Monitoring[EMetaHumanLocalLiveLinkSubjectMonitoring::Basic] > 0)
	{
		// No need to ensure the update happens on EditorTick like in MHA since the problem that works around only 
		// effects in UEFN, and this code will never run in UEFN.

		check(IsInGameThread());

		const UE::MetaHuman::Pipeline::EPipelineExitStatus ExitStatus = InPipelineData->GetExitStatus();
		if (ExitStatus != UE::MetaHuman::Pipeline::EPipelineExitStatus::Unknown)
		{
			if (ExitStatus == UE::MetaHuman::Pipeline::EPipelineExitStatus::Ok || ExitStatus == UE::MetaHuman::Pipeline::EPipelineExitStatus::Aborted)
			{
				StateEnum = EMetaHumanLocalLiveLinkSubjectState::Completed;
				State = "Completed";
				StateLED = FColor::Green;
			}
			else
			{
				StateEnum = EMetaHumanLocalLiveLinkSubjectState::Error;
				StateError = InPipelineData->GetErrorNodeMessage();
				State = FString::Printf(TEXT("Error (%s)"), *StateError);
				StateLED = FColor::Red;
			}
		}
		else
		{
			FrameNumber = InPipelineData->GetFrameNumber();
			Frame = FString::Printf(TEXT("%05d"), FrameNumber);

			double Now = FPlatformTime::Seconds();

			if (FPSCount == 0)
			{
				FPSStart = Now;
			}

			FPSCount++;

			if (Now - FPSStart > 2)
			{
				ProcessingRate = (FPSCount - 1) / (Now - FPSStart);
				FPSCount = 0;
			}

			if (ProcessingRate > 0)
			{
				FPS = FString::Printf(TEXT("%0.2f"), ProcessingRate);

				CaptureRate = InPipelineData->GetData<float>("MediaPlayer.Capture FPS");

				if (CaptureRate > 0 && FMath::Abs(ProcessingRate - CaptureRate) > 2)
				{
					FPS += FString::Printf(TEXT(" (Capture FPS %0.2f)"), CaptureRate);
				}
			}
			else
			{
				FPS = TEXT("Calculating...");
			}
		}
	}
}
