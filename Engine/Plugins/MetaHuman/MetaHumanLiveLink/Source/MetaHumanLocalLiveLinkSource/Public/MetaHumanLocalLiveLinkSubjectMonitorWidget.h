// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanLocalLiveLinkSubjectSettings.h"

#include "Widgets/SCompoundWidget.h"



class METAHUMANLOCALLIVELINKSOURCE_API SMetaHumanLocalLiveLinkSubjectMonitorWidget : public SCompoundWidget, public FGCObject
{
public:

	virtual ~SMetaHumanLocalLiveLinkSubjectMonitorWidget() override;

	SLATE_BEGIN_ARGS(SMetaHumanLocalLiveLinkSubjectMonitorWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UMetaHumanLocalLiveLinkSubjectSettings* InSettings);

	//~Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	virtual FString GetReferencerName() const override;
	//~End FGCObject interface

private:

	TObjectPtr<UMetaHumanLocalLiveLinkSubjectSettings> Settings = nullptr;
};
