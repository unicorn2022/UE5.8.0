// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanVideoBaseLiveLinkSubjectSettings.h"

#include "MetaHumanVideoBaseLiveLinkSubject.h"
#include "MetaHumanVideoLiveLinkSettings.h"



UMetaHumanVideoBaseLiveLinkSubjectSettings::UMetaHumanVideoBaseLiveLinkSubjectSettings()
{
	const UMetaHumanVideoLiveLinkSettings* DefaultSettings = GetDefault<UMetaHumanVideoLiveLinkSettings>();

	bHeadOrientation = DefaultSettings->bHeadOrientation;
	bHeadTranslation = DefaultSettings->bHeadTranslation;
	MonitorImage = DefaultSettings->MonitorImage;

	// Width of the image monitor widget is somewhat arbitrary since it is always
	// placed in a layout which fills the horizontal space available to it.
	// That "fill to width" layout takes precedence over the desired width of the
	// widget we are setting here. If the image monitor widget were to be placed 
	// in a horizontally scrolling layout this would no longer work.
	MonitorImageSize = FVector2D(1, DefaultSettings->MonitorImageHeight); 
}

#if WITH_EDITOR
void UMetaHumanVideoBaseLiveLinkSubjectSettings::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		const FName PropertyName = *Property->GetName();

		const bool bHeadOrientationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bHeadOrientation);
		const bool bHeadTranslationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bHeadTranslation);
		const bool bHeadStabilizationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bHeadStabilization);
		const bool bMonitorImageChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MonitorImage);
		const bool bRotationChanged = PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Rotation);

		FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;

		if (bHeadOrientationChanged)
		{
			VideoSubject->SetHeadOrientation(bHeadOrientation);
		}
		else if (bHeadTranslationChanged)
		{
			VideoSubject->SetHeadTranslation(bHeadTranslation);
		}
		else if (bHeadStabilizationChanged)
		{
			VideoSubject->SetHeadStabilization(bHeadStabilization);
		}
		else if (bMonitorImageChanged)
		{
			VideoSubject->SetMonitorImage(MonitorImage);
		}
		else if (bRotationChanged)
		{
			VideoSubject->SetRotation(Rotation);
		}
	}
}
#endif

void UMetaHumanVideoBaseLiveLinkSubjectSettings::CaptureNeutralHeadPose()
{
	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->MarkNeutralFrame();

	Super::CaptureNeutralHeadPose();
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetHeadOrientation(bool bInHeadOrientation)
{
	bHeadOrientation = bInHeadOrientation;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetHeadOrientation(bHeadOrientation);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetHeadOrientation(bool& bOutHeadOrientation) const
{
	bOutHeadOrientation = bHeadOrientation;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetHeadTranslation(bool bInHeadTranslation)
{
	bHeadTranslation = bInHeadTranslation;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetHeadTranslation(bHeadTranslation);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetHeadTranslation(bool& bOutHeadTranslation) const
{
	bOutHeadTranslation = bHeadTranslation;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetHeadStabilization(bool bInHeadStabilization)
{
	bHeadStabilization = bInHeadStabilization;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetHeadStabilization(bHeadStabilization);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetHeadStabilization(bool& bOutHeadStabilization) const
{
	bOutHeadStabilization = bHeadStabilization;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetMonitorImage(EHyprsenseRealtimeNodeDebugImage InMonitorImage)
{
	MonitorImage = InMonitorImage;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetMonitorImage(MonitorImage);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetMonitorImage(EHyprsenseRealtimeNodeDebugImage& OutMonitorImage) const
{
	OutMonitorImage = MonitorImage;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::SetRotation(EMetaHumanVideoRotation InRotation)
{
	Rotation = InRotation;

	FMetaHumanVideoBaseLiveLinkSubject* VideoSubject = (FMetaHumanVideoBaseLiveLinkSubject*) Subject;
	VideoSubject->SetRotation(Rotation);
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::GetRotation(EMetaHumanVideoRotation& OutRotation) const
{
	OutRotation = Rotation;
}

void UMetaHumanVideoBaseLiveLinkSubjectSettings::OnUpdate(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
{
	check(IsInGameThread());

	Super::OnUpdate(InPipelineData);

	if (Monitoring[EMetaHumanLocalLiveLinkSubjectMonitoring::Basic] > 0)
	{
		const UE::MetaHuman::Pipeline::EPipelineExitStatus ExitStatus = InPipelineData->GetExitStatus();
		if (ExitStatus == UE::MetaHuman::Pipeline::EPipelineExitStatus::Unknown)
		{
			const FString ConfidencePin = TEXT("RealtimeMonoSolver.Confidence Out");
			const FString StatePin = TEXT("RealtimeMonoSolver.State Out");
			const FString DroppedFramePin = TEXT("MediaPlayer.Dropped Frame Count Out");
			const FString ResPin = TEXT("Rotate.UE Image Out");
			const FString ImageSampleTimePin = TEXT("MediaPlayer.UE Image Sample Time Out");

			EHyprsenseRealtimeNodeState PipelineState = static_cast<EHyprsenseRealtimeNodeState>(InPipelineData->GetData<int32>(StatePin));
			switch (PipelineState)
			{
			case EHyprsenseRealtimeNodeState::OK:
				StateEnum = EMetaHumanLocalLiveLinkSubjectState::OK;
				State = "OK";
				StateLED = FColor::Green;
				break;

			case EHyprsenseRealtimeNodeState::NoFace:
				StateEnum = EMetaHumanLocalLiveLinkSubjectState::DeviceSpecific;
				VideoStateEnum = EMetaHumanVideoBaseLiveLinkSubjectState::NoFaceDetected;
				State = "No face detected";
				StateLED = FColor::Orange;
				break;

			case EHyprsenseRealtimeNodeState::SubjectTooFar:
				StateEnum = EMetaHumanLocalLiveLinkSubjectState::DeviceSpecific;
				VideoStateEnum = EMetaHumanVideoBaseLiveLinkSubjectState::SubjectTooFar;
				State = "Subject too far from camera";
				StateLED = FColor::Yellow;
				break;

			default:
				StateEnum = EMetaHumanLocalLiveLinkSubjectState::Unknown;
				State = "Unknown";
				StateLED = FColor::Red;
				break;
			}

			ConfidenceValue = InPipelineData->GetData<float>(ConfidencePin);
			Confidence = FString::Printf(TEXT("%.1f"), ConfidenceValue);

			const UE::MetaHuman::Pipeline::FUEImageDataType& Res = InPipelineData->GetData<UE::MetaHuman::Pipeline::FUEImageDataType>(ResPin);
			if (Res.Width > 0 && Res.Height > 0)
			{
				Resolution = FString::Printf(TEXT("%i x %i"), Res.Width, Res.Height);
				ImageResolution = FIntPoint(Res.Width, Res.Height);
			}
			else
			{
				Resolution = TEXT("Unknown");
				ImageResolution = FIntPoint(-1, -1);
			}

			double Now = FPlatformTime::Seconds();

			if (DroppedCount == -1)
			{
				DroppedStart = Now;
				DroppedCount = 0;
			}

			DroppedCount += InPipelineData->GetData<int32>(DroppedFramePin);

			if (Now - DroppedStart > 2)
			{
				Dropped = DroppedCount;
				DroppedCount = -1;
			}

			if (Dropped == -1)
			{
				Dropping = TEXT("Calculating...");
			}
			else if (Dropped == 0)
			{
				Dropping = TEXT("No");
			}
			else
			{
				Dropping = FString::Printf(TEXT("Yes (%i frame%s)"), Dropped, Dropped == 1 ? TEXT("") : TEXT("s"));
			}

			FTimecode PipelineTimecode = InPipelineData->GetData<FQualifiedFrameTime>(ImageSampleTimePin).ToTimecode();
			PipelineTimecode.Subframe = 0; // For the purpose of display, ignore subframe - just looks wrong
			Timecode = PipelineTimecode.ToString();
		}
	}
}
