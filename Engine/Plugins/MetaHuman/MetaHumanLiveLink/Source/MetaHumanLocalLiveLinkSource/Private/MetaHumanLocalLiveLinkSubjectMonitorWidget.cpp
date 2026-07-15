// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLocalLiveLinkSubjectMonitorWidget.h"

#define LOCTEXT_NAMESPACE "MetaHumanLocalLiveLinkSubjectMonitorWidget"



SMetaHumanLocalLiveLinkSubjectMonitorWidget::~SMetaHumanLocalLiveLinkSubjectMonitorWidget()
{
	if (Settings)
	{
		Settings->SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring::Basic, false);
	}
}

void SMetaHumanLocalLiveLinkSubjectMonitorWidget::Construct(const FArguments& InArgs, UMetaHumanLocalLiveLinkSubjectSettings* InSettings)
{
	Settings = InSettings;

	if (Settings)
	{
		Settings->SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring::Basic, true);
	}
}

void SMetaHumanLocalLiveLinkSubjectMonitorWidget::AddReferencedObjects(FReferenceCollector& InCollector)
{
	InCollector.AddReferencedObject(Settings);
}

FString SMetaHumanLocalLiveLinkSubjectMonitorWidget::GetReferencerName() const
{
	return TEXT("SMetaHumanLocalLiveLinkSubjectMonitorWidget");
}

#undef LOCTEXT_NAMESPACE
