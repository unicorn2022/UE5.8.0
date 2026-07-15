// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"
#include "UObject/WeakInterfacePtr.h"

class UMetaHumanCharacter;
class IMetaHumanCharacterEditorActorInterface;
enum class EMetaHumanCharacterCameraFrame : uint8;

namespace UE::MetaHuman::ViewportDefaults
{
	// Same FoV used in MetaHuman Creator
	constexpr float DefaultFOV = 18.001738f;

	// Default Perpective Clipping plane distances
	// A value of 0 for the Far Clipping plane means infinity
	constexpr float DefaultNearClippingPlane = 1.0f;
	constexpr float DefaultFarClippingPlane = 0.0f;
}

class FMetaHumanCharacterViewportClient : public FEditorViewportClient
{
public:
	FMetaHumanCharacterViewportClient(
		FEditorModeTools* InModeTools, 
		FPreviewScene* InPreviewScene, 
		TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> InEditingActor,
		TWeakObjectPtr<UMetaHumanCharacter> InCharacter);

	//~Begin FEditorViewportClient interface
	virtual void Tick(float InDeltaSeconds) override;
	virtual bool InputAxis(const FInputKeyEventArgs& InEventArgs) override;
	virtual void OverridePostProcessSettings(FSceneView& View) override;
	virtual void SetupViewForRendering(FSceneViewFamily& InViewFamily, FSceneView& InOutView) override;
	virtual bool InputKey(const FInputKeyEventArgs& InEventArgs) override;
	virtual bool ShouldOrbitCamera() const override;
	virtual bool ShouldScaleCameraSpeedByDistance() const override;
	virtual void Draw(FViewport* InViewport, FCanvas* InCanvas) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override;
	virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override;
	virtual void ProcessAccumulatedPointerInput(FViewport* InViewport) override;
	//~End FEditorViewportClientInterface

	TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> WeakCharacterActor;
	TWeakObjectPtr<UMetaHumanCharacter> WeakCharacter;

	void FocusOnSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate, bool bInInstant = false);
	void SetAutoFocusToSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate);
	void RescheduleFocus();

	void UpdateRenderingQuality(const int32 InActiveProfileIndex);

	void SetViewportWidget(const TWeakPtr<SEditorViewport>& InEditorViewportWidget);

	void ClearShortcuts();
	void SetShortcuts(const TArray<TPair<FText, FText>>& InShortcuts);

	void ShowFaceBonesOnCharacter(const bool InDrawFaceBones);
	
	void ShowBodyBonesOnCharacter(const bool InDrawBodyBones);

	void DrawDebugBones(const USkeletalMeshComponent* MetaHumanSkeletalMeshComponent, bool bIsFaceComponent, FPrimitiveDrawInterface* PDI);

	void ShowFaceNormalsOnCharacter(const bool InShowFaceNormals);
	void ShowBodyNormalsOnCharacter(const bool InShowBodyNormals);

	void ShowFaceTangentsOnCharacter(const bool InShowFaceTangents);
	void ShowBodyTangentsOnCharacter(const bool InShowBodyTangents);

	void ShowFaceBinormalsOnCharacter(const bool InShowFaceBinormals);
	void ShowBodyBinormalsOnCharacter(const bool InShowBodyBinormals);

	void HandleCameraFocusRequest(UMetaHumanCharacter* InCharacter, EMetaHumanCharacterCameraFrame InFrameToFocus);

	/** Sets the selected point for the SelectedPoint camera frame. Does not start orbit. */
	void SetSelectedPoint(const FVector& InPoint);

	/** Called on Ctrl+Alt+LMB to find the orbit focus point. Return true and set OutPoint if hit. If unset, defaults to the MetaHuman mesh. */
	TFunction<bool(const FRay&, FVector&)> SelectPointHitProvider;

	/** Fired when Ctrl+Alt+LMB successfully finds a selection point and orbit begins */
	FSimpleMulticastDelegate OnSelectPointOrbitStarted;

	/** Set by the toolkit after each environment swap. False when the loaded environment has no actor implementing IMetaHumanCharacterEnvironmentLightRig. */
	void SetEnvironmentHasLightRig(bool bInHasLightRig) { bEnvironmentHasLightRig = bInHasLightRig; }

	/** True if the loaded environment exposes a light rig. Drives whether the Light Rig Rotation slider is interactive. */
	bool DoesEnvironmentHaveLightRig() const { return bEnvironmentHasLightRig; }

	/** Set by the toolkit after each environment swap. False when the loaded environment has no actor implementing IMetaHumanCharacterEnvironmentBackground. */
	void SetEnvironmentHasBackground(bool bInHasBackground) { bEnvironmentHasBackground = bInHasBackground; }

	/** True if the loaded environment exposes a background actor. Drives whether the Background Color picker is interactive. */
	bool DoesEnvironmentHaveBackground() const { return bEnvironmentHasBackground; }

private:

	void FocusOnFace(float InDistanceScale, const FVector& InOffset, bool bInInstant);

	void FocusOnBody(float InDistanceScale, const FVector& InOffset, bool bInInstant);

	void FocusOnBodyPartPair(const FName InBodyPartNameR, const FName InBodyPartNameL, const float InDistanceScale, const FVector& InOffset, const bool bInInstant);

	void FocusOnHands(float InDistanceScale, const FVector& InOffset, const bool bInInstant);

	void FocusOnFeet(float InDistanceScale, const FVector& InOffset, const bool bInInstant);

	struct FPostProcessSettings PostProcessSettings;

	void SetTransmissionForAllLights(bool bTransmissionEnabled);

	struct FDrawInfoOptions
	{
		FIntPoint TopCenter;
		bool bTitleLeft = true;
		int32 Padding = 2;
		FLinearColor TitleColor = FLinearColor::White;
		FLinearColor KeyTextColor = FLinearColor::White;
		FLinearColor ValueTextColor = FLinearColor::White;
	};

	void DrawInfos(FCanvas* InCanvas, const FText& Title, const TArray<TPair<FText, FText>>& Infos, const FDrawInfoOptions& InDrawInfoOptions) const;

	/** This function is used to force ViewportClient to tick becease when enabling Normals/Tangents/Binormals we need more than 1 tick for these to show on MH */
	void ForceTicks(uint32 NumberOfTicks);

	/**
	 * Updates the light rig parent actor's yaw so the rig stays camera-relative,
	 * while respecting any externally applied rotation (level default, environment
	 * swap restore). Called from Tick.
	 *
	 * @param bJustFramedThisTick True if the viewport-framing first-focus ran on this
	 *   same tick. In that case we skip tracking for this tick because FocusOnSelectedFrame's
	 *   ToggleOrbitCamera calls rebase the camera rotation through ComputeOrbitMatrix's
	 *   internal 90-degree offset, producing a transient yaw reading.
	 */
	void UpdateLightRigParentRotation(bool bJustFramedThisTick);

private:
	/** Flag whether an initial viewport camera framing has been performed. */
	bool bIsViewportFramed;

	/**
	 * State for applying per-tick world-space camera-yaw deltas to the light rig's parent
	 * actor, keeping the light direction camera-relative as the user orbits. Each tick we
	 * read the mode-aware world-space camera yaw and add the shortest-arc delta to the
	 * parent's yaw. External writes to the parent (toolkit environment-swap restore, level
	 * defaults) are respected: the first observation of a new parent adopts its current
	 * yaw as baseline without writing.
	 */
	TWeakObjectPtr<AActor> WeakLightRigParent;
	double LastCameraYaw = 0.0;
	bool bHasPrevCameraYaw = false;

	/** Flag for showing Face bones in Draw function*/
	bool bDrawFaceBones = false;

	/** Flag for showing Body bones in Draw function*/
	bool bDrawBodyBones = false;

	/** Camera framing for auto framing mode. */
	EMetaHumanCharacterCameraFrame AutoSelectedFrame;

	/** Last selected camera framing in viewport. */
	EMetaHumanCharacterCameraFrame LastSelectedFrame;

	/** Viewport message. */
	FText ViewportMessage;

	/** Shortcuts */
	TArray<TPair<FText, FText>> Shortcuts;

	/** Previous mouse position */
	TOptional<FInt32Point> PreviousMousePosition;
	TOptional<FInt32Point> NextMousePosition;

	/** Target point for the SelectedPoint camera frame, set via SetSelectedPoint */
	TOptional<FVector> SelectedPointFocusTarget;

	/** When set, the next F press focuses on this frame instead of following the normal cycle, then resets to empty */
	TOptional<EMetaHumanCharacterCameraFrame> OverrideNextFocusFrame;

	/** Saved camera state for a numbered bookmark slot */
	struct FCameraBookmark
	{
		FVector Location;
		FVector LookAt;
		FRotator Rotation;
		float FOV;
	};

	/** Camera bookmarks saved with Ctrl+1..4, recalled with 1..4 */
	TOptional<FCameraBookmark> CameraBookmarks[4];

	/** True when the loaded environment contains a light rig. See SetEnvironmentHasLightRig. */
	bool bEnvironmentHasLightRig = true;

	/** True when the loaded environment contains a background actor. See SetEnvironmentHasBackground. */
	bool bEnvironmentHasBackground = true;

	/** Bone drawing information*/
	TArray<FTransform> FaceBonesWorldTransforms;
	TArray<FTransform> BodyBonesWorldTransforms;

	TArray<FLinearColor> FaceBoneColors;
	TArray<FLinearColor> BodyBoneColors;

	TArray<TRefCountPtr<HHitProxy>> HitProxies;
};
