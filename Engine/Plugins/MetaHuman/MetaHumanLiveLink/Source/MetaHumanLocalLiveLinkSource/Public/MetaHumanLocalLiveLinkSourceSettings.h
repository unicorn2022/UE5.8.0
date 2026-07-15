// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanMediaSourceCreateParams.h"

#include "LiveLinkSourceSettings.h"

#include "MetaHumanLocalLiveLinkSourceSettings.generated.h"


UENUM()
enum class EMetaHumanLocalLiveLinkSourceDeviceType : uint8
{
	CaptureDevice = 0	UMETA(DisplayName = "Capture Device"),
	MediaBundle = 1		UMETA(DisplayName = "Media Bundle"),
	MediaSource = 2		UMETA(DisplayName = "Media Source"),
	MediaProfile = 3	UMETA(DisplayName = "Media Profile")
};


UCLASS()
class METAHUMANLOCALLIVELINKSOURCE_API UMetaHumanLocalLiveLinkSourceSettings : public ULiveLinkSourceSettings
{
public:

	GENERATED_BODY()

	void SetSource(class FMetaHumanLocalLiveLinkSource* InSource);
	FLiveLinkSubjectKey RequestSubjectCreation(const FString& InSubjectName, class UMetaHumanLocalLiveLinkSubjectSettings* InMetaHumanLocalLiveLinkSubjectSettings);

	UPROPERTY()
	bool bIsPreset = false;

private:

	class FMetaHumanLocalLiveLinkSource* Source = nullptr;
};