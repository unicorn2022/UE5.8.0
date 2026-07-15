// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanLiveLinkSubjectSettings.h"
#include "MetaHumanMediaSourceCreateParams.h"

#include "Pipeline/PipelineData.h"

#include "MetaHumanLocalLiveLinkSubjectSettings.generated.h"



UENUM(BlueprintType)
enum class EMetaHumanLocalLiveLinkSubjectMonitoring : uint8
{
	Basic = 0, // Quick to calculate, eg fps counter
	Advanced,  // Slow to calculate, eg video preview texture
};

UENUM(BlueprintType)
enum class EMetaHumanLocalLiveLinkSubjectState : uint8
{
	Unknown = 0,
	OK,
	Completed,
	Error,
	DeviceSpecific,
};



UCLASS(BlueprintType)
class METAHUMANLOCALLIVELINKSOURCE_API UMetaHumanLocalLiveLinkSubjectSettings : public UMetaHumanLiveLinkSubjectSettings
{
public:

	GENERATED_BODY()

	virtual void Setup();
	virtual void SetSubject(class FMetaHumanLocalLiveLinkSubject* InSubject);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnUpdate, TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);
	FOnUpdate UpdateDelegate;

	class FMetaHumanLocalLiveLinkSubject* Subject = nullptr;

	/* The state of the processing. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "10"))
	FString State;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Information")
	EMetaHumanLocalLiveLinkSubjectState StateEnum = EMetaHumanLocalLiveLinkSubjectState::Unknown;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Information")
	FString StateError;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "15"))
	FColor StateLED;

	/* Frame number being processed. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "100"))
	FString Frame;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Information")
	int32 FrameNumber = -1;

	/* Processing frame rate. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "110"))
	FString FPS;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Information")
	float ProcessingRate = -1;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Information")
	float CaptureRate = -1;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Information", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides, DisplayPriority = "120"))
	FString Timecode;

	UPROPERTY(Transient, VisibleAnywhere, Category = "nocategory", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	FString Remove;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void ReloadSubject();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void RemoveSubject();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	void SetMonitoring(EMetaHumanLocalLiveLinkSubjectMonitoring InMonitoringLevel, bool bInIsMonitoring);

protected:

	TMap<EMetaHumanLocalLiveLinkSubjectMonitoring, int32> Monitoring;

	virtual void OnUpdate(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);

private:

	int32 FPSCount = 0;
	double FPSStart = 0;
};