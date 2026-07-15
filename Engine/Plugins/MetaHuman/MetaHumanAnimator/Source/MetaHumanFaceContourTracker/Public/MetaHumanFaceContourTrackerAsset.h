// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeCPU.h"

#include "MetaHumanFaceContourTrackerAsset.generated.h"

#define UE_API METAHUMANFACECONTOURTRACKER_API

/** Face Contour Tracker Asset
* 
*   Contains trackers for different facial features
*   Used in MetaHuman Identity and Performance assets
* 
**/
UCLASS(MinimalAPI, BlueprintType)
class UMetaHumanFaceContourTrackerAsset : public UObject
{
	GENERATED_BODY()

public:
	UE_API static TObjectPtr<UMetaHumanFaceContourTrackerAsset> LoadDefaultTracker();

	//~Begin UObject interface
#if WITH_EDITOR
	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent) override;
	UE_API virtual void PostTransacted(const FTransactionObjectEvent& InTransactionEvent) override;
#endif
	UE_API virtual void PostLoad() override;
	//~End UObject interface

	UE_DEPRECATED(5.8, "FaceDetector is deprecated. Use FaceDetectorModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> FaceDetector;
	UE_DEPRECATED(5.8, "FullFaceTracker is deprecated. Use FullFaceTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> FullFaceTracker;
	UE_DEPRECATED(5.8, "BrowsDenseTracker is deprecated. Use BrowsDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> BrowsDenseTracker;
	UE_DEPRECATED(5.8, "EyesDenseTracker is deprecated. Use EyesDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> EyesDenseTracker;
	UE_DEPRECATED(5.8, "NasioLabialsDenseTracker is deprecated. Use NasioLabialsDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> NasioLabialsDenseTracker;
	UE_DEPRECATED(5.8, "MouthDenseTracker is deprecated. Use MouthDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> MouthDenseTracker;
	UE_DEPRECATED(5.8, "LipzipDenseTracker is deprecated. Use LipzipDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> LipzipDenseTracker;
	UE_DEPRECATED(5.8, "ChinDenseTracker is deprecated. Use ChinDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> ChinDenseTracker;
	UE_DEPRECATED(5.8, "TeethDenseTracker is deprecated. Use TeethDenseTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> TeethDenseTracker;
	UE_DEPRECATED(5.8, "TeethConfidenceTracker is deprecated. Use TeethConfidenceTrackerModel instead.")
	TSharedPtr<UE::NNE::IModelInstanceGPU> TeethConfidenceTracker;

	TSharedPtr<UE::NNE::IModelInstanceRunSync> FaceDetectorModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> FullFaceTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> BrowsDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> EyesDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> NasioLabialsDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> MouthDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> LipzipDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> ChinDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> TeethDenseTrackerModel;
	TSharedPtr<UE::NNE::IModelInstanceRunSync> TeethConfidenceTrackerModel;

	UE_API void SetNNEBackend(const FString& InNNEBackend);
	UE_API FString GetNNEBackend(void) const;

public:

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> FaceDetectorModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> FullFaceTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> BrowsDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> EyesDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> NasioLabialsDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> MouthDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> LipzipDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> ChinDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> TeethDenseTrackerModelData;

	UPROPERTY(EditAnywhere, Category = TrackerModels)
	TSoftObjectPtr<UNNEModelData> TeethConfidenceTrackerModelData;

public:

	UE_API bool CanProcess() const;

	UE_API void LoadTrackers(bool bInShowProgressNotification, TFunction<void(bool)>&& Callback);

	UE_API void CancelLoadTrackers();

	UE_API bool LoadTrackersSynchronous();

	UE_API bool IsLoadingTrackers() const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNNEModelData>> LoadedTrackerModelData;

	TArray<TSharedPtr<UE::NNE::IModelInstanceRunSync>> LoadedTrackerModels;

	FString NNEBackend;

	TWeakPtr<class SNotificationItem> LoadNotification;
	TSharedPtr<struct FStreamableHandle> TrackersLoadHandle;

	UE_API TArray<TSoftObjectPtr<UNNEModelData>> GetTrackerModelData() const;
	UE_API TArray<TSharedPtr<UE::NNE::IModelInstanceRunSync>> GetTrackerModels() const;
	UE_API bool SetTrackerModels();

	UE_API TArray<FSoftObjectPath> GetTrackerModelDataAsSoftObjectPaths() const;
	UE_API bool AreTrackerModelsLoaded() const;

	UE_API bool CreateTrackerModels();
};

#undef UE_API
