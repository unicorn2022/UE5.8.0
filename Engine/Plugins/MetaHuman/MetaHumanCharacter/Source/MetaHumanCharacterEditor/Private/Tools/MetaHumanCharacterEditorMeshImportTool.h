// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "BaseBehaviors/KeyInputBehavior.h"
#include "MetaHumanConformSolverSettings.h"
#include "MetaHumanCharacterEditorSubTools.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorMeshEditingTools.h"
#include "Tools/MetaHumanCharacterEditorMeshTarget3DKeyPointMechanic.h"
#include "Tools/MetaHumanCharacterEditorMeshTargetContourMechanic.h"
#include "MetaHumanCharacterEditorMeshImportTargetScene.h"
#include "Engine/StreamableManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "MetaHumanCharacterEditorMeshImportTool.generated.h"

class AInternalToolFrameworkActor;
class FMetaHumanCharacterViewportClient;
class FMetaHumanCurveDataController;
class UDynamicMeshComponent;
enum class EMetaHumanCharacterSkinPreviewMaterial : uint8;
namespace UE::Geometry{class FDynamicMesh3;}

UENUM()
enum class EMetaHumanCharacterOverlay : uint8
{
	NoOverlay UMETA(ToolTip = "Removes Overlay"),
	OverlayMesh UMETA(ToolTip = "Places the target mesh on top of everything"),
	OverlayMetaHuman UMETA(DisplayName = "Overlay MetaHuman", ToolTip = "Places the MetaHuman mesh on top of everything"),
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterOverlay, EMetaHumanCharacterOverlay::Count);

UENUM()
enum class EMetaHumanMeshImportMode : uint8
{
	Single,
	MeshParts,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanMeshImportMode, EMetaHumanMeshImportMode::Count);

UCLASS()
class UMetaHumanCharacterEditorMeshImportToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual bool CanBuildTool(const FToolBuilderState& InSceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

UCLASS()
class UMetaHumanCharacterEditorMeshImportToolProperties : public UInteractiveToolPropertySet, public FMetaHumanCharacterClothVisibilityBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Import Meshes")
	bool bUseCharacterParts = false;
	
	/* Assign your full body character mesh to this slot. */
	UPROPERTY(EditAnywhere, Category = "Import Meshes", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> CombinedMesh;
	
	UPROPERTY(EditAnywhere, Category = "Import Meshes", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> HeadMesh;
	
	UPROPERTY(EditAnywhere, Category = "Import Meshes", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> BodyMesh;
	
	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bShowKeyPoints = true;
	
	/* Manually nudges the custom mesh position along each axis. Click and drag in the input text field to quickly change the offset value. */
	UPROPERTY(EditAnywhere, Category = "Visualization | Custom Mesh")
	FVector MeshOffset = FVector::ZeroVector;

	/* Toggles a matcap shader on the custom mesh in the viewport. */
	UPROPERTY(EditAnywhere, DisplayName = "Matcap", Category = "Visualization | Custom Mesh")
	bool bUseGrayMaterialOnMesh = false;

	UPROPERTY(EditAnywhere, Category = "Visualization | Facial Tracking")
	bool bShowFacialTracking = false;

	UPROPERTY(EditAnywhere, Category = "Visualization | Facial Tracking")
	bool bEditFacialCurves = true;

	UPROPERTY(EditAnywhere, Category = "Visualization | Facial Tracking")
	FLinearColor TrackingCurvesColor = FLinearColor::Green;

	UPROPERTY(EditAnywhere, Category = "Visualization | Facial Tracking")
	FLinearColor TrackingPointsColor = FLinearColor::Yellow;

	UPROPERTY(EditAnywhere, Category = "Visualization | Facial Tracking", meta = (UIMin = "0.1", UIMax = "10.0", ClampMin = "0.1", ClampMax = "10.0"))
	float TrackingPointsSize = 4.f;

	/* Controls the transparency of the custom mesh overlay in the viewport. Lower values let you see the MetaHuman mesh through it, useful when checking alignment. */
	UPROPERTY(EditAnywhere, Category = "Visualization | Custom Mesh", meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float MeshOpacity = 1.f;

	/* Sets the colour of the matcap shader applied to the custom mesh. */
	UPROPERTY(EditAnywhere, DisplayName = "Matcap Color", Category = "Visualization | Custom Mesh")
	FLinearColor MeshColor = FLinearColor(0.3f, 0.4f, 1.0f, 1.f);

	/* When fitting, the underlying body model won't perfectly match the input. The small difference, or 'vertex delta', is stored and this slider controls its application to the body mesh. */
	UPROPERTY(EditAnywhere, Category = "Body Parameters", meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float BodyDelta = 1.0f;
	
	/* When fitting, the underlying head model won't perfectly match the input. The small difference, or 'vertex delta', is stored and this slider controls its application to the head mesh. */
	UPROPERTY(EditAnywhere, Category = "Head Parameters", meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float HeadDelta = 1.0f;
	
	UPROPERTY(EditAnywhere, Category = "Advanced")
	FBodyConformSolveSettings BodySolveSettings;
	
	UPROPERTY(EditAnywhere, Category = "Advanced", meta=(ShowInnerProperties))
	FRefinementSettings RefinementSettings;

	/* Toggles a matcap shader on the MetaHuman mesh in the viewport. */
	UPROPERTY(EditAnywhere, DisplayName = "Matcap", Category = "Visualization | MetaHuman")
	bool bUseGrayMaterialOnMetaHuman = false;

	/* When enabled, overlays the MetaHuman topology guide texture onto the mesh */
	UPROPERTY(EditAnywhere, DisplayName = "Guides Texture", Category = "Visualization | MetaHuman")
	bool bUseGuidesTextureOnMetaHuman = true;

	/* Sets the colour of the matcap shader on the MetaHuman mesh. */
	UPROPERTY(EditAnywhere, DisplayName = "Matcap Color", Category = "Visualization | MetaHuman")
	FLinearColor MetaHumanMeshColor = FLinearColor(1.f, 0.125f, 0.f, 1.f);

	/* Controls the transparency of the MetaHuman mesh in the viewport. */
	UPROPERTY(EditAnywhere, DisplayName = "Opacity", Category = "Visualization | MetaHuman", meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float MetaHumanOpacity = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Visualization | MetaHuman")
	EMetaHumanCharacterOverlay Overlay = EMetaHumanCharacterOverlay::NoOverlay;
	
	UPROPERTY(EditAnywhere, Category = "Visualization | Debug")
	bool bShowBodyModelMesh = false;
	
	UPROPERTY(EditAnywhere, DisplayName = "Show Body Model A Pose", Category = "Visualization | Debug")
	bool bShowBodyModelAPose = false;
	
	UPROPERTY(EditAnywhere, DisplayName = "Show MetaHuman A Pose", Category = "Visualization | Debug")
	bool bShowMetaHumanAPose = false;

	/* Toggles visibility of any additional key points you have manually added. */
	UPROPERTY(EditAnywhere, Category = "Key Points")
	bool bShowCustomKeyPoints = true;

	/* Toggles visibility of the default MetaHuman preset key points. These are the default landmarks used on the MetaHuman mesh to guide the solve. */
	UPROPERTY(EditAnywhere, Category = "Key Points")
	bool bShowPresetKeyPoints = false;

	/* Sets the display colour of the key points belonging to your imported custom mesh. */
	UPROPERTY(EditAnywhere, DisplayName = "Custom Key Points Color", Category = "Key Points")
	FLinearColor CustomKeyPointsColor = FLinearColor(0.f, 0.2f, 1.f, 1.f);

	/* Sets the display colour of the key points belonging to the MetaHuman mesh. */
	UPROPERTY(EditAnywhere, DisplayName = "MH Key Points Color", Category = "Key Points")
	FLinearColor MHKeyPointsColor = FLinearColor::White;

	/* Scales the size of the key point markers displayed in the viewport. */
	UPROPERTY(EditAnywhere, Category = "Key Points", meta = (UIMin = "0.0", UIMax = "5.0", ClampMin = "0.0", ClampMax = "5.0"))
	float KeyPointsScale = 1.0f;

	/* When enabled, draws lines between corresponding MetaHuman and custom mesh key points */
	UPROPERTY(EditAnywhere, Category = "Key Points")
	bool bShowConnectionLines = true;

	UPROPERTY()
	EMetaHumanMeshImportMode Mode = EMetaHumanMeshImportMode::Single;

public:
	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface

	void StartBodyStepMeshConform();
	void StartFaceStepMeshConform();
	void StartAutoMeshConform();
	void StartAlignHeadMeshConform();
	bool CanProcess() const;
	bool CanReset() const;
	
	void RefineVertices();
	
	void OnAsyncConformComplete(bool bInSuccess, bool bWasCancelled);

	TMap<int32, FVector3f> GetKeyPointCorrespondencesFromCharacter() const;
	FMetaHumanCharacterTargetTrackingResults GetFaceTrackingFromCharacter() const;
	
	FMetaHumanCharacterTargetMeshKey GetTargetMeshKey() const;
	
	void TrackFace();
	bool CanTrackFace() const;
	void ResetBody();
	void ResetHead();


	void UpdateKeyPointVisibility();

private:
	bool GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const;
	void StartMeshConform(bool bIsAutoSolve, bool bDisableBodySolve, bool bDisableFaceSolve);
	FConformTargetMesh GetConformTargetMesh() const;
};

UCLASS()
class UMetaHumanCharacterEditorMeshImportTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	
	DECLARE_DELEGATE_RetVal(FVector2D, OnGetTrackerImageSize);

	//~Begin UMeshSurfacePointTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType InShutdownType) override;
	
	virtual bool HitTest(const FRay& InRay, FHitResult& OutHit) override;
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginDrag(const FRay& InRay) override;
	virtual void OnUpdateDrag(const FRay& InRay) override;
	virtual void OnEndDrag(const FRay& InRay) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }
	//~End UMeshSurfacePointTool interface

	virtual void OnPropertyModified(UObject* InPropertySet, FProperty* InProperty) override;
	
	UMetaHumanCharacterEditorMeshImportToolProperties* GetMeshImportProperties() const { return MeshImportProperties; }
	TSharedPtr<FMetaHumanCurveDataController> GetCurveDataController() const;
	UTexture* GetTrackingImageTexture() const;
	FIntPoint GetTrackingImageSize() const;
	void RemoveAllKeyPoints();
	void DeleteFaceCurves();
	bool HasFaceCurves() const;
	void DeselectAllKeyPoints();
	void SetViewportClient(const TSharedPtr<FMetaHumanCharacterViewportClient>& InViewportClient);
	
	void ExportStateToDNA();

	FSimpleMulticastDelegate OnUpdateFacialTrackingCurvesDelegate;

	OnGetTrackerImageSize OnGetTrackerImageSizeDelegate;
	
protected:
	virtual void OnTick(float InDeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* InRenderAPI) override;
	virtual void DrawHUD(FCanvas* InCanvas, IToolsContextRenderAPI* InRenderAPI) override;
	
private:
	friend class FMeshImportToolInputProcessor;

	void OnTargetMeshChange();
	void SetShowAPose(bool bSetToAPose);
	void OnTargetMeshLoaded();
	void UpdateTargetMeshMaterial();

	void ApplyOverlayState();
	void UpdateMeshLocationFromOffset() const;
	void Initialize3DKeyPoints();
	void UpdateSelectedPointFromMechanic();
	void SetBodyDelta(float InBodyDelta, bool bRebuildDynamicMeshes);
	void SetHeadDelta(float InHeadDelta, bool bRebuildDynamicMeshes);
	void SetNeedsFullUpdate(const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey);
	void OnCharacterStateChanged(bool bRebuildKeyPoints, bool bRebuildDynamicMeshes = true);
	void OnCharacterVerticesChanged(const TArray<FVector3f>& InBodyVertices, const TArray<FVector3f>& InFaceVertices, bool bRebuildKeyPoints, bool bRebuildDynamicMeshes = true);
	void DisplayError(const FText& InErrorMessage) const;

	TSharedPtr<class FMeshImportToolInputProcessor> InputProcessor;
private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorMeshImportToolProperties> MeshImportProperties;
	
	UPROPERTY()
	TObjectPtr<UMeshTarget3DKeyPointMechanic> KeyPointsMechanic3D;
	
	UPROPERTY()
	TObjectPtr<UMeshTargetContourMechanic> ContourMechanic;
	
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> MeshTargetScene;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacter> MetaHumanCharacter = nullptr;

	FDelegateHandle OnAsyncConformIterationDelegateHandle;
	FDelegateHandle OnAsyncConformCompleteDelegateHandle;
	FDelegateHandle OnBodyStateChangedDelegateHandle;
	FDelegateHandle OnTargetMeshKeyPointsChangedDelegateHandle;
	FDelegateHandle OnPreviewMaterialChangedFromGrayDelagateHandle;

	// Re-entrancy guard for the OnTargetMeshKeyPointsChanged lambda — prevents a rebuild from recursively triggering another rebuild if any downstream path commits again.
	bool bIsRebuildingKeyPoints = false;

	TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState> PreConformBodyState;
	TSharedPtr<const FMetaHumanCharacterIdentity::FState> PreConformFaceState;

	// Body and face states captured at Setup() — used to restore state on cancel/non-full-update shutdown.
	TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState> SetupBodyState;
	TSharedPtr<const FMetaHumanCharacterIdentity::FState> SetupFaceState;
	
	FMetaHumanTargetHitResult LastHitResult;

	TOptional<EMetaHumanCharacterSkinPreviewMaterial> LastUsedPreviewMaterial;
	
	bool bHasBeenShutdown = false;
	bool bNeedsFullUpdate = false;
	FMetaHumanCharacterTargetMeshKey ConformedTargetMeshKey;
	bool bTargetMeshChangePending = false;
	bool bIsLoadingTargetMesh = false;
	TWeakPtr<SNotificationItem> TargetMeshLoadNotification;
	TSharedPtr<FStreamableHandle> TargetMeshStreamableHandle;

	bool bRightClickActive = false;

	TWeakPtr<FMetaHumanCharacterViewportClient> ViewportClient = nullptr;

	friend class UMetaHumanCharacterEditorMeshImportToolProperties;
};