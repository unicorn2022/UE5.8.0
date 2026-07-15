// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkTypes.h"

#include "MetaHumanLiveLinkSourceBlueprint.generated.h"



UCLASS(Blueprintable)
class METAHUMANLIVELINKSOURCE_API UMetaHumanLiveLinkSourceBlueprint : public UObject
{

public:

	GENERATED_BODY()

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubjectAdded, FLiveLinkSubjectKey, Subject);

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "MetaHuman Live Link")
	FOnSubjectAdded SubjectAdded;

	/** PostInitProperties override. */
	virtual void PostInitProperties() override;

private:

	void OnSubjectChanged();

	TArray<FLiveLinkSubjectKey> Subjects;
};
