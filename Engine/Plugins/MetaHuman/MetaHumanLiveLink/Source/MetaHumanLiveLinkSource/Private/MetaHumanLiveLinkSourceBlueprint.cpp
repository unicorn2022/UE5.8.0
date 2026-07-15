// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLiveLinkSourceBlueprint.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"



void UMetaHumanLiveLinkSourceBlueprint::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		return;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	LiveLinkClient.OnLiveLinkSubjectsChanged().AddUObject(this, &UMetaHumanLiveLinkSourceBlueprint::OnSubjectChanged);

	Subjects = LiveLinkClient.GetSubjects(true, true);
}

void UMetaHumanLiveLinkSourceBlueprint::OnSubjectChanged()
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		return;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	TArray<FLiveLinkSubjectKey> CurrentSubjects = LiveLinkClient.GetSubjects(true, true);

	for (const FLiveLinkSubjectKey& Subject : CurrentSubjects)
	{
		if (!Subjects.Contains(Subject))
		{
			SubjectAdded.Broadcast(Subject);
		}
	}

	Subjects = CurrentSubjects;
}
