// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAudioBaseLiveLinkSubjectMonitorWidget.h"

#include "Widgets/Notifications/SProgressBar.h"



// A very simple audio level meter - it aint no VU meter! Just display the maximum PCM amplitude.
// A more useable meter would need to work out db levels and average over time. But this meter is 
// good enough for a simple "microphone working or not" check.

SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget::~SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget()
{
	if (Settings)
	{
		Settings->SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring::Basic, false);
		Settings->SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring::Advanced, false);
	}
}

void SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget::Construct(const FArguments& InArgs, UMetaHumanAudioBaseLiveLinkSubjectSettings* InSettings)
{
	Settings = InSettings;

	if (Settings)
	{
		Settings->SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring::Basic, true);
		Settings->SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring::Advanced, true);
	}

	ChildSlot
	[
		SNew(SProgressBar)
		.Percent_Lambda([this]()
		{
			return Settings ? Settings->Level : 0;
		})
	];
}

void SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget::AddReferencedObjects(FReferenceCollector& InCollector)
{
	InCollector.AddReferencedObject(Settings);
}

FString SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget::GetReferencerName() const
{
	return TEXT("SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget");
}
