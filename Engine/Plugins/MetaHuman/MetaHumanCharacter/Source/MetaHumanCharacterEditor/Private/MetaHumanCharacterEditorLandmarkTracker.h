// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetaHumanCurveDataController.h"
#include "MetaHumanFaceContourTrackerAsset.h"
#include "Pipeline/Pipeline.h"

#include "MetaHumanCharacterEditorLandmarkTracker.generated.h"

struct FFrameTrackingContourData;

UCLASS()
class UMetaHumanCharacterEditorLandmarkTracker : public UObject
{
	GENERATED_BODY()
	
public:
	virtual void PostInitProperties() override;
	
	void LoadTrackers();
	void InitializeContourData(int32 InWidth, int32 InHeight);
	
	FFrameTrackingContourData TrackImage(const TArray<FColor>& InImageData, int32 InWidth, int32 InHeight);
	
	TMap<FString, TArray<FVector2D>> GetTrackingContours(const FFrameTrackingContourData& TrackingContourData) const;
	
	TSharedPtr<FMetaHumanCurveDataController> GetCurveDataController() const { return CurveDataController; }

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanFaceContourTrackerAsset> FaceContourTracker;
	
	UPROPERTY()
	TObjectPtr<UMetaHumanContourData> ContourData;
	
	TSharedPtr<FMetaHumanCurveDataController> CurveDataController;
	TUniquePtr<UE::MetaHuman::Pipeline::FPipeline> TrackPipeline;
	FVector2D TrackingImageSize = FVector2D::ZeroVector;
};
