// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractionMechanic.h"
#include "Engine/World.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SplineComponent.h"
#include "MetaHumanCharacterEditorLandmarkTracker.h"
#include "MetaHumanCharacterEditorMeshImportTargetScene.h"
#include "EditorUndoClient.h"

#include "MetaHumanCharacterEditorMeshTargetContourMechanic.generated.h"

UCLASS(MinimalAPI)
class UMeshTargetContourMechanic : public UInteractionMechanic, public FEditorUndoClient
{
	GENERATED_BODY()
	
public:
	virtual void Setup(UInteractiveTool* InParentTool) override;
	virtual void Shutdown() override;
	
	void Initialize(TObjectPtr<UMetaHumanCharacter> InMetaHumanCharacter, TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> InMeshTargetScene, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey);
	TSharedPtr<FMetaHumanCurveDataController> GetCurveDataController() const;
	UTexture* GetTrackingImageTexture() const;
	FIntPoint GetTrackingImageSize() const;
	
	bool TrackFaceInCurrentView(const FVector& InMeshOffset);
	bool TrackFaceWithAutoFraming(const FVector& InMeshOffset);

	bool HasTrackingResults() const;

	void DeleteFaceCurves();
	void Clear();
	
	void UpdateComponentTransform();
	
	virtual void Render(IToolsContextRenderAPI* InRenderAPI) override;

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;

	//~ UObject interface
	virtual void BeginDestroy() override;
	
protected:
	FIntPoint GetImageSize() const;
	bool TrackFaceFromSceneComponent(const FVector& InMeshOffset);

	bool bIsTracking = false;

	/** Guards OnCurveDataControllerUpdated from re-committing to the character asset while we are
	    rebuilding derived state in response to an undo/redo. */
	bool bIsApplyingUndo = false;
	
	void ProjectTo3DPoints();
	void OnCurveDataControllerUpdated();
	void SubscribeToCurveDataController();

	/** Rebuild mechanic-side derived state (controller draw data, TrackingContours map, 3D splines)
	    after UE's transaction system has restored UMetaHumanContourData / UMetaHumanCharacter. */
	void RebuildAfterUndo();

protected:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacter> MetaHumanCharacter;
	
	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld;
	
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> SceneCaptureComponent;
	
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorLandmarkTracker> LandmarkTracker;
	
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> MeshTargetScene;
	
	UPROPERTY()
	TObjectPtr<AInternalToolFrameworkActor> SplinesActor;
	
	UPROPERTY()
	TMap<FString, TObjectPtr<USplineComponent>> ContourSplines;
	
	FMinimalViewInfo CapturedViewInfo;	
	FVector CapturedMeshOffset;
	
	int32 DefaultTrackingImageWidth = 2048;
	int32 DefaultTrackingImageHeight = 2048;
	
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;
	TMap<FString, TArray<FVector2D>> TrackingContours;
	TMap<FString, TArray<FVector>> TrackingContours3D;
	FTransform TargetComponentTransform;

	UPROPERTY()
	TObjectPtr<UTexture2D> RestoredTrackerImage;
	FIntPoint RestoredTrackerImageSize;

	FBox CalculateHeadBounds() const;
};