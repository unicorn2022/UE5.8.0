// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanToolkitBase.h"
#include "FrameRange.h"

#include "UI/MetaHumanPerformanceControlRigViewportManager.h"
#include "MetaHumanBodyTrackerInterface.h"
#include "ControlRigAssetReference.h"

#include "Styling/SlateBrush.h"
#include "Pipeline/PipelineData.h"
#include "Curves/RealCurve.h"

#include "SMetaHumanOverlayWidget.h"
#include "SMetaHumanImageViewer.h"

enum class EDataInputType : uint8;
enum class EPerformanceExportRange : uint8;
enum class EPerformanceHeadMovementMode : uint8;
enum class EABImageViewMode;
enum class EMovieSceneDataChangeType;

class FMetaHumanPerformanceEditorToolkit
	: public FMetaHumanToolkitBase
{
public:

	FMetaHumanPerformanceEditorToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FMetaHumanPerformanceEditorToolkit();

	//~Begin FMetaHumanToolkitBase interface
	virtual void NotifyPostChange(const FPropertyChangedEvent& InPropertyChangedEvent, FProperty* InPropertyThatChanged) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void HandleUndoOrRedoTransaction(const class FTransaction* InTransaction) override;
	//~End FMetaHumanToolkitBase interface

	//~Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	virtual FString GetReferencerName() const override;
	//~End FGCObject interface

protected:

	//~Begin FMetaHumanToolkitBase interface
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;
	virtual void BindCommands() override;
	virtual void PostInitAssetEditor() override;
	virtual void HandleGetViewABMenuContents(EABImageViewMode InViewMode, class FMenuBuilder& InMenuBuilder) override;
	virtual void HandleSequencerMovieSceneDataChanged(EMovieSceneDataChangeType InDataChangeType) override;
	virtual void HandleSequencerGlobalTimeChanged() override;
	virtual void HandleFootageDepthDataChanged(float InNear, float InFar) override;
	//~End FMetaHumanToolkitBase interface

	//~ Begin FAssetEditorToolkit interface
	virtual void InitToolMenuContext(struct FToolMenuContext& InMenuContext) override;
	//~ End FAssetEditorToolkit interface

private:
	/** The object being edited by this toolkit */
	TObjectPtr<class UMetaHumanPerformance> Performance = nullptr;

	// Tabs
	static const FName ImageReviewTabId;
	static const FName ControlRigTabId;

	TSharedRef<SDockTab> SpawnImageReviewTab(const FSpawnTabArgs& InArgs);
	TSharedRef<SDockTab> SpawnControlRigTab(const FSpawnTabArgs& InArgs);

	// Initialization
	void InitPerformerViewport();
	void ExtendToolBar();
	void ExtendMenu();

	/** Get the viewport client as a FMetaHumanPerformerViewportClient */
	TSharedRef<class FMetaHumanPerformanceViewportClient> GetMetaHumanPerformerViewportClient() const;

	// 2D Image review window
	TSharedPtr<SMetaHumanOverlayWidget<SMetaHumanImageViewer>> ImageViewer;
	FSlateBrush ImageViewerBrush;

	FGuid PerformerActorBindingId;
	FGuid PerformerFaceBindingId;
	FGuid PerformerBodyBindingId;
	FVector VisualizationObjectOffset = FVector::ZeroVector;

	void HandleDataInputTypeChanged(EDataInputType InDataInputType);
	void HandleSourceDataChanged(class UFootageCaptureData* InFootageCaptureData, class USoundWave* InAudio, bool bInResetRanges);
	void HandleIdentityChanged(class UMetaHumanIdentity* InIdentity);
	void HandleVisualizeObjectChanged(class UObject* InVisualizeObject);
	void HandleSkeletonParamsChanged();
	void HandleControlRigAssetReferenceChanged(const FControlRigAssetStrongReference& InControlRigAssetReference);
	void HandleHeadMovementModeChanged(EPerformanceHeadMovementMode InHeadMovementMode);
	void HandleHeadMovementReferenceFrameChanged(bool bInAutoChooseHeadMovementReferenceFrame, uint32 InHeadMovementReferenceFrame);
	void HandleNeutralPoseCalibrationChanged();
	void HandleFrameRangeChanged(int32 InStartFrame, int32 InEndFrame);
	void HandleRealtimeAudioChanged(bool bInRealtimeAudio);
	void HandleFrameProcessed(int32 InFrameNumber);
	void HandleStageProcessingFinished(int32 InStageCompleted);
	void HandleProcessingFinished(TSharedPtr<const UE::MetaHuman::Pipeline::FPipelineData> InPipelineData);
	void HandleBodyTrackerModeChanged(EMetaHumanBodyTrackerMode InBodyTrackerMode);

	void DeleteSequencerKeysInProcessingRange();
	void UpdateSequencerAnimationData(int32 InFrameNumber);
	void UpdateControlRigHeadPose();
	void UpdatePostProcessAnimBP();

	FString GetPossessableNameFace() const;
	FString GetPossessableNameEffectiveBody() const;
	FString GetPossessableNameRealBody() const;
	FString GetPossessableNameActor() const;

	void UpdateVisualizationObject(UObject* InVisualizeObject);
	bool HasPropertyChanged(const class FTransaction* InTransaction, const FName& InPropertyName) const;
	void RebindSequencerPossessableObjects();

	// Movie info
	FFrameRate GetFrameRate() const;
	FFrameNumber GetCurrentFrameNumber() const;
	FFrameNumber GetCurrentAnimationFrameNumber() const;

	void SetControlsEnabled(bool bIsEnabled);

	// Handle events from widgets in the editor
	bool CanProcess() const;
	bool CanCancel() const;
	bool CanExportAnimation() const;
	bool CanExportLevelSequence() const;
	void HandleProcessButtonClicked();
	void HandleCancelButtonClicked();
	void HandleExportAnimationClicked();
	void HandleExportLevelSequenceClicked();
	void HandleViewSetupClicked(int32 InSlotIndexIndex, bool bInStore);
	void HandleImageReviewFocus();
	void HandleShowFramesAsTheyAreProcessed();

	/** Creates a dialog warning if either bInDeviceModelIsSet, bHasDepthData, or bInConsistentRGBAndDepthCameras are false, returns whether the user agrees to continue */
	bool DisplayWarningsBeforeProcessing(bool bInDeviceModelIsSet, bool bHasDepthData, bool bInConsistentRGBAndDepthCameras);

	/** Shows a dialog to pick a calibration asset when depth generation needs one. Returns true if user picked one and it was assigned. */
	bool ShowCalibrationPickerForDepthGeneration();

	/** The ControlRig component to be displayed in the AB viewport */
	TObjectPtr<class UMetaHumanPerformanceControlRigComponent> ControlRigComponent;

	/** The component used to visualize the animation results (face skel mesh, full MetaHuman) */
	TObjectPtr<class USceneComponent> VisualizationObjectComponent;

	/** Body animation is applied to the BodyDriverActor from where it is retargetted onto the VisualizationObjectComponent */
	/* This actor also displays debugging info like intermediate animation states plus groundplane etc */
	TObjectPtr<class AMetaHumanBodyDriverActorInterface> BodyDriverActor;

	/** The component used to display the footage in the AB views */
	TObjectPtr<class UMetaHumanFootageComponent> FootageComponent;

	/** The object that contains the low-level representation for the landmarks that are drawn on screen */
	TSharedPtr<class FMetaHumanCurveDataController> CurveDataController;

	/** The contour data being displayed on screen */
	TObjectPtr<class UMetaHumanContourData> DisplayContourData;

	/**
	 * A helper class to manage the control rig viewport tab
	 * FBaseAssetToolkit only supports one viewport so any other tab that displays
	 */
	FMetaHumanPerformanceControlRigViewportManager ControlRigManager;

	/** The ControlRig instance to be used when recording keys in Sequencer from animation data on the face */
	TObjectPtr<class UControlRig> RecordControlRigFace;

	/** The ControlRig instance to be used when recording keys in Sequencer from animation data on the body */
	TObjectPtr<class UControlRig> RecordControlRigBody;

	/** A delegate for dynamic Start Processing Shot button tooltip */
	FText GetStartProcessingShotButtonTooltipText() const;

	/** A delegate for dynamic Cancel Processing Shot button tooltip */
	FText GetCancelProcessingShotButtonTooltipText() const;

	bool bShowFramesAsTheyAreProcessed = false;

	/** True if the toolkit is being initialized. This will set to false at the end of PostInitAssetEditor */
	bool bIsToolkitInitializing = true;

	void SendTelemetryForPerformanceExportRequest(bool InIsAnimationSequence, bool InIsWholeSequence);

	void GetExcludedFrameInfo(FFrameRate& OutSourceRate, FFrameRangeMap& OutExcludedFramesMap, int32& OutMediaStartFrame, TRange<FFrameNumber>& OutProcessingLimit) const;
	ERichCurveInterpMode GetInterpolationMode(int32 InFrameNumber) const;

	USkeletalMeshComponent* GetFaceSkeletalMeshComponent() const;
	USkeletalMeshComponent* GetBodySkeletalMeshComponent() const;
	class USkeletalMesh* GetFaceSkeletalMesh() const;

	TSharedPtr<class FMetaHumanPerformanceNotifier> ActiveNotifier;
};
