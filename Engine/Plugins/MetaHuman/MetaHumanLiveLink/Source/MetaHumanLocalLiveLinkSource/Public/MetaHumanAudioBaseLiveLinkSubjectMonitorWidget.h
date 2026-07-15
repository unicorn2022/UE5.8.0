// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanAudioBaseLiveLinkSubjectSettings.h"

#include "Widgets/SCompoundWidget.h"



class METAHUMANLOCALLIVELINKSOURCE_API SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget : public SCompoundWidget, public FGCObject
{
public:

	virtual ~SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget() override;

	SLATE_BEGIN_ARGS(SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMetaHumanAudioBaseLiveLinkSubjectSettings* InSettings);

	//~Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	virtual FString GetReferencerName() const override;
	//~End FGCObject interface

private:

	TObjectPtr<UMetaHumanAudioBaseLiveLinkSubjectSettings> Settings = nullptr;
};
