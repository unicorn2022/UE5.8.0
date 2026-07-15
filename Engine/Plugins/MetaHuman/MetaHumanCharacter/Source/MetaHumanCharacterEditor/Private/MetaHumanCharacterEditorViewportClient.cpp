// Copyright Epic Games, Inc.All Rights Reserved.

#include "MetaHumanCharacterEditorViewportClient.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "CanvasItem.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "ContentStreaming.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Light.h"
#include "Logging/MessageLog.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEnvironmentLightRig.h"
#include "MetaHumanSDKEditor.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PreviewScene.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SkeletalDebugRendering.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorViewportClient"



namespace UE::MetaHuman::Private
{
	static TAutoConsoleVariable<bool> CVarEnableLightRigCameraTracking(
		TEXT("mh.Character.Viewport.LightRigCameraTracking"),
		true,
		TEXT("If true (default), the MetaHuman character editor viewport rotates the light rig's parent actor to follow the camera yaw (camera-relative lighting). Set to 0 to leave the parent at its level-authored rotation."),
		ECVF_Default);

	static const TArray<TPair<FText, FText>> DefaultShortcuts =
	{
		{ LOCTEXT("OrbitSelection_ShortcutKey", "Orbit Selection"), LOCTEXT("OrbitSelection_ShortcutValue", "CTRL + ALT + LMB Click")},
		{ LOCTEXT("OrbitCamera_ShortcutKey", "Orbit"), LOCTEXT("OrbitCamera_ShortcutValue", "LMB Drag")},
		{ LOCTEXT("PanCamera_ShortcutKey", "Pan"), LOCTEXT("PanCamera_ShortcutValue", "MMB")},
		{ LOCTEXT("ZoomCamera_ShortcutKey", "Zoom"), LOCTEXT("ZoomCamera_ShortcutValue", "RMB Drag")}
	};

	// Returns true when the project has the data Lumen needs to render. Mirrors the
	// public-API parts of Lumen::IsLumenFeatureAllowedForView (Renderer/Private/Lumen/Lumen.cpp).
	static bool CanUseLumen()
	{
		return IsRayTracingEnabled() || DoesProjectSupportDistanceFields();
	}
}

FMetaHumanCharacterViewportClient::FMetaHumanCharacterViewportClient(
	FEditorModeTools* InModeTools, 
	FPreviewScene* InPreviewScene,
	TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> InEditingActor,
	TWeakObjectPtr<UMetaHumanCharacter> InCharacter)
	: FEditorViewportClient{ InModeTools, InPreviewScene }
	, WeakCharacterActor{InEditingActor}
	, WeakCharacter{InCharacter}
	, bIsViewportFramed{false}
	, AutoSelectedFrame{EMetaHumanCharacterCameraFrame::Face}
	, LastSelectedFrame{EMetaHumanCharacterCameraFrame::Auto}
{
	// The real time override is required to make sure the world ticks while the viewport is not active
	// or this requires the user to interact with the viewport to get up to date lighting and textures
	AddRealtimeOverride(true, NSLOCTEXT("FMetaHumanCharacterViewportClient", "RealTimeOverride", "MetaHumanCharacterRealTimeOverride"));
	SetRealtime(true);

	// This is done in order to enable Advanced Post Process effects that are disabled in FPreviewScene that we use 
	EngineShowFlags.EnableAdvancedFeatures();

	// TODO: Find a better way to hide icons, probably just setting some of the flags. 
	// I tried this by setting flags like collision, bounds, some lighting, icons but it wasn't successful so I'll get back to tidy this
	SetGameView(true);

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	FEditorViewportClient::SetCameraSpeedSettings(Settings->CameraSpeedSettings);
	
	// Allow closeups of the face without clipping
	OverrideNearClipPlane(UE::MetaHuman::ViewportDefaults::DefaultNearClippingPlane);
	OverrideFarClipPlane(UE::MetaHuman::ViewportDefaults::DefaultFarClippingPlane);

	// Set the default FoV
	ViewFOV = UE::MetaHuman::ViewportDefaults::DefaultFOV;
	FOVAngle = ViewFOV;

	// Enable drawing of viewport axis
	bDrawAxes = true;
	bDrawAxesGame = true;

	// Enable the orbit camera by default. Toggle before setting view state so the
	// rotation we set is not rebased by ComputeOrbitMatrix's 90-degree offset.
	ToggleOrbitCamera(true);

	// Initial camera placement: frame the character head from the front. Doing this
	// in the constructor (rather than from the owning toolkit) keeps the camera state
	// set up before the first Tick and independent of toolkit construction order.
	SetViewLocation(FVector{ 80, 0, 143 });
	SetViewRotation(FRotator{ 0, 180, 0 });
	SetLookAtLocation(FVector{ 0, 0, 143 });

	if (Settings->IsValidRenderingQualityProfileIndex(InCharacter.Get()->ViewportSettings.RenderingQualityProfileIndex))
	{
		UpdateRenderingQuality(InCharacter.Get()->ViewportSettings.RenderingQualityProfileIndex);
	}

	// Bone drawing info initizalization
	const USkeletalMeshComponent* MetaHumanSkeletalMeshFaceComponent = WeakCharacterActor.Get()->GetFaceComponent();
	const USkeletalMeshComponent* MetaHumanSkeletalMeshBodyComponent = WeakCharacterActor.Get()->GetBodyComponent();

	FaceBonesWorldTransforms.AddUninitialized(MetaHumanSkeletalMeshFaceComponent->GetNumComponentSpaceTransforms());
	BodyBonesWorldTransforms.AddUninitialized(MetaHumanSkeletalMeshBodyComponent->GetNumComponentSpaceTransforms());
	FaceBoneColors.AddUninitialized(MetaHumanSkeletalMeshFaceComponent->GetNumComponentSpaceTransforms());
	BodyBoneColors.AddUninitialized(MetaHumanSkeletalMeshBodyComponent->GetNumComponentSpaceTransforms());

	// Apply the saved debug rendering flags so the viewport matches the toolbar state on reopen.
	const FMetaHumanCharacterViewportSettings& InitialViewportSettings = InCharacter.Get()->ViewportSettings;
	ShowFaceBonesOnCharacter(InitialViewportSettings.bShowFaceBones);
	ShowBodyBonesOnCharacter(InitialViewportSettings.bShowBodyBones);
	ShowFaceNormalsOnCharacter(InitialViewportSettings.bShowFaceNormals);
	ShowBodyNormalsOnCharacter(InitialViewportSettings.bShowBodyNormals);
	ShowFaceTangentsOnCharacter(InitialViewportSettings.bShowFaceTangents);
	ShowBodyTangentsOnCharacter(InitialViewportSettings.bShowBodyTangents);
	ShowFaceBinormalsOnCharacter(InitialViewportSettings.bShowFaceBinormals);
	ShowBodyBinormalsOnCharacter(InitialViewportSettings.bShowBodyBinormals);

	ClearShortcuts();
}

void FMetaHumanCharacterViewportClient::Tick(float InDeltaSeconds)
{
	FEditorViewportClient::Tick(InDeltaSeconds);

	if (!GIntraFrameDebuggingGameThread && GetPreviewScene() != nullptr)
	{
		GetPreviewScene()->GetWorld()->Tick(LEVELTICK_ViewportsOnly, InDeltaSeconds);
	}

	// Note whether framing just happened on this tick -- FocusOnSelectedFrame calls
	// ToggleOrbitCamera multiple times which rebases the stored camera rotation
	// through ComputeOrbitMatrix's 90-degree offset. Reading the yaw on the same tick
	// as framing catches it in a transient state; wait one tick for it to settle.
	const bool bJustFramedThisTick = !bIsViewportFramed && Viewport->GetSizeXY().X > 0 && Viewport->GetSizeXY().Y > 0;
	if (bJustFramedThisTick)
	{
		if (UMetaHumanCharacter* Character = WeakCharacter.Get())
		{
			// initial focus on the camera frame as stored in the character
			FocusOnSelectedFrame(Character->ViewportSettings.CameraFrame, /*bInRotate*/ true);
		}
		bIsViewportFramed = true;
	}

	UpdateLightRigParentRotation(bJustFramedThisTick);
}

void FMetaHumanCharacterViewportClient::UpdateLightRigParentRotation(bool bJustFramedThisTick)
{
	// Runtime toggle via `mh.Character.Viewport.LightRigCameraTracking` (default on).
	// When off, the parent is left at whatever rotation the level / toolkit have set.
	// Also skip while the viewport is still initializing: FocusOnSelectedFrame's
	// ToggleOrbitCamera calls rebase the camera rotation through ComputeOrbitMatrix's
	// internal 90-degree offset. Reading the yaw before that settles produces a
	// ~90-degree spurious first delta.
	if (!UE::MetaHuman::Private::CVarEnableLightRigCameraTracking.GetValueOnGameThread()
		|| !bIsViewportFramed
		|| bJustFramedThisTick)
	{
		// Drop tracking state so we re-baseline cleanly once the viewport is settled
		// or the CVar is toggled back on.
		WeakLightRigParent = nullptr;
		bHasPrevCameraYaw = false;
		return;
	}

	// Rotate the light rig's parent actor to follow the camera's yaw, keeping
	// lighting camera-relative as the user orbits. Apply per-tick yaw deltas derived
	// from a mode-aware world-space camera yaw (the stored ViewRotation has different
	// meaning in orbit vs. perspective mode -- this mirrors FEditorViewportClient::DrawAxes).
	// External writes to the parent (e.g. the toolkit's restore across environment swaps)
	// are respected automatically: the first observation of a new parent adopts the
	// current rotation as a baseline without writing, and subsequent ticks only apply deltas.
	//
	// The child rig's own rotation driven by IMetaHumanCharacterEnvironmentLightRig::
	// SetRotation (user slider) is independent and must not be touched here.
	const FPreviewScene* PreviewScenePtr = GetPreviewScene();
	UWorld* World = PreviewScenePtr ? PreviewScenePtr->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// Find the parent of the first actor implementing the light rig interface.
	// Re-scan every tick: the "new parent" case (environment swap) is handled below
	// by baselining without applying a delta.
	AActor* LightRigParent = nullptr;
	for (FActorIterator It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
		{
			LightRigParent = Actor->GetAttachParentActor();
			if (LightRigParent)
			{
				break;
			}
		}
	}

	if (!LightRigParent)
	{
		// No rig available (no environment, or level still streaming). Drop
		// tracking so we re-baseline cleanly when one appears.
		WeakLightRigParent = nullptr;
		bHasPrevCameraYaw = false;
		return;
	}

	// World-space camera rotation. GetViewRotation() has different meaning in orbit
	// vs. perspective mode; the engine's own DrawAxes (EditorViewportClient.cpp
	// ~line 4137) uses this same ternary to recover the rendered world-space rotation.
	const FViewportCameraTransform& ViewTransform = GetViewTransform();
	const FRotator CameraRotation = bUsingOrbitCamera
		? ViewTransform.ComputeOrbitMatrix().InverseFast().Rotator()
		: ViewTransform.GetRotation();

	// Yaw is degenerate near the vertical poles (gimbal lock): yaw/roll swap freely
	// without changing the camera's world direction, so the extracted yaw can jump
	// 180 in a single tick as the camera passes through a top-down view. Drop tracking
	// while near the pole and re-baseline when the camera comes back down.
	constexpr double NearVerticalPitchDegrees = 89.0;
	if (FMath::Abs(CameraRotation.Pitch) > NearVerticalPitchDegrees)
	{
		bHasPrevCameraYaw = false;
		return;
	}

	const double CameraYaw = CameraRotation.Yaw;

	// First observation of this parent (initial viewport open OR post-environment-swap):
	// adopt its current rotation as the baseline and store the current camera yaw.
	// This is what preserves the level-authored default and the toolkit's environment-
	// swap restore -- we never write the parent on a baseline tick.
	const bool bNewParent = (WeakLightRigParent.Get() != LightRigParent);
	if (bNewParent || !bHasPrevCameraYaw)
	{
		WeakLightRigParent = LightRigParent;
		LastCameraYaw = CameraYaw;
		bHasPrevCameraYaw = true;
		return;
	}

	// Shortest-arc signed delta in world yaw. Absorbs the rebasing jumps that
	// ToggleOrbitCamera produces between ticks (those collapse to ~0 because the
	// ternary above yields a consistent world yaw either side of the toggle).
	const double DeltaYaw = FMath::UnwindDegrees(CameraYaw - LastCameraYaw);
	LastCameraYaw = CameraYaw;

	if (FMath::IsNearlyZero(DeltaYaw, UE_KINDA_SMALL_NUMBER))
	{
		return;
	}

	// Apply the camera delta to the parent's yaw. Sign is += because the light must
	// rotate WITH the camera (camera-relative lighting): if the camera yaws by +dY,
	// the rig yaws by +dY so it stays on the same side of the screen.
	FRotator ParentRotation = LightRigParent->GetActorRotation();
	ParentRotation.Yaw = FMath::UnwindDegrees(ParentRotation.Yaw + DeltaYaw);
	LightRigParent->SetActorRotation(ParentRotation);
}

bool FMetaHumanCharacterViewportClient::InputAxis(const FInputKeyEventArgs& Args)
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	FInputKeyEventArgs ModifiedArgs = Args;
	ModifiedArgs.AmountDepressed = Args.AmountDepressed * Settings->MouseSensitivityModifier;

	return FEditorViewportClient::InputAxis(ModifiedArgs);
}

bool FMetaHumanCharacterViewportClient::InputKey(const FInputKeyEventArgs& InEventArgs)
{

	if (InEventArgs.Key == EKeys::F && InEventArgs.Event == IE_Pressed)
	{
		if (UMetaHumanCharacter* Character = WeakCharacter.Get())
		{
			EMetaHumanCharacterCameraFrame NextFrame;
			if (OverrideNextFocusFrame.IsSet())
			{
				NextFrame = OverrideNextFocusFrame.GetValue();
				OverrideNextFocusFrame.Reset();
			}
			else
			{
				switch (Character->ViewportSettings.CameraFrame)
				{
					case EMetaHumanCharacterCameraFrame::Face:
						NextFrame = EMetaHumanCharacterCameraFrame::Body;          break;
					case EMetaHumanCharacterCameraFrame::Body:
						if (SelectedPointFocusTarget.IsSet())
						{
							NextFrame = EMetaHumanCharacterCameraFrame::SelectedPoint;
						}
						else
						{
							NextFrame = EMetaHumanCharacterCameraFrame::Face;
						}
					break;
					case EMetaHumanCharacterCameraFrame::SelectedPoint:
						NextFrame = EMetaHumanCharacterCameraFrame::Face;          break;
					default:
						NextFrame = EMetaHumanCharacterCameraFrame::Face;          break;
				}
			}
			FocusOnSelectedFrame(NextFrame, /*bInRotate*/ true);
			return true;
		}
	}

	if (InEventArgs.Key == EKeys::LeftMouseButton && InEventArgs.Event == IE_Pressed)
	{
		const bool bCtrl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
		const bool bAlt  = Viewport->KeyState(EKeys::LeftAlt)     || Viewport->KeyState(EKeys::RightAlt);
		if (bCtrl && bAlt)
		{
			FViewportCursorLocation CursorLoc = GetCursorWorldLocationFromMousePos();
			const FRay Ray(CursorLoc.GetOrigin(), CursorLoc.GetDirection(), true);

			FVector HitPoint;
			bool bHit = false;

			if (SelectPointHitProvider)
			{
				bHit = SelectPointHitProvider(Ray, HitPoint);
			}
			else if (UMetaHumanCharacter* Character = WeakCharacter.Get())
			{
				if (const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get())
				{
					FVector HitNormal;
					bHit = Subsystem->SelectBodyVertex(Character, Ray, HitPoint, HitNormal) != INDEX_NONE;
				}
			}

			if (bHit)
			{
				SetSelectedPoint(HitPoint);
				GetViewTransform().SetLookAt(HitPoint);
				OnSelectPointOrbitStarted.Broadcast();
			}
			// Don't consume, let base class start orbit tracking for Alt+LMB
		}
	}

	// Camera bookmarks: Ctrl+1..4 saves, 1..4 recalls
	if (InEventArgs.Event == IE_Pressed)
	{
		const FKey NumberKeys[4] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
		for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
		{
			if (InEventArgs.Key == NumberKeys[SlotIndex])
			{
				const bool bCtrl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
				if (bCtrl)
				{
					CameraBookmarks[SlotIndex] = FCameraBookmark{
						GetViewTransform().GetLocation(),
						GetViewTransform().GetLookAt(),
						GetViewRotation(),
						FOVAngle
					};
					return true;
				}
				else if (CameraBookmarks[SlotIndex].IsSet())
				{
					const FCameraBookmark& Bookmark = CameraBookmarks[SlotIndex].GetValue();
					ToggleOrbitCamera(true);
					GetViewTransform().SetLocation(Bookmark.Location);
					GetViewTransform().SetLookAt(Bookmark.LookAt);
					SetViewRotation(Bookmark.Rotation);
					ViewFOV = Bookmark.FOV;
					FOVAngle = Bookmark.FOV;
					Invalidate();
					return true;
				}
				break;
			}
		}
	}

	TUniquePtr<FViewportCameraTransform> PreViewTransform;
	if (InEventArgs.Key == EKeys::MouseScrollUp)
	{
		// make sure orbit camera is used
		ToggleOrbitCamera(true);
		PreViewTransform = MakeUnique<FViewportCameraTransform>(GetViewTransform());
	}
	const bool Success = FEditorViewportClient::InputKey(InEventArgs);
	if (PreViewTransform && bUsingOrbitCamera)
	{
		// ensure mouse wheel scrolling stops at minimum distance
		float MinDist = 35.0f;
		float PreDist = (PreViewTransform->GetLookAt() - PreViewTransform->GetLocation()).Length();
		float PostDist = (GetViewTransform().GetLookAt() - GetViewTransform().GetLocation()).Length();
		if (PostDist > PreDist || PostDist < MinDist)
		{
			GetViewTransform() = *PreViewTransform;
			FVector Offset = GetViewTransform().GetLocation() - GetViewTransform().GetLookAt();
			FVector OffsetNormalized = Offset.GetSafeNormal();
			GetViewTransform().SetLocation(GetViewTransform().GetLookAt() + OffsetNormalized * MinDist);
		}
	}
	return Success;
}

bool FMetaHumanCharacterViewportClient::ShouldOrbitCamera() const
{
	return true;
}

bool FMetaHumanCharacterViewportClient::ShouldScaleCameraSpeedByDistance() const
{
	if (const UMetaHumanCharacter* Character = WeakCharacter.Get())
	{
		return Character->ViewportSettings.bUseDistanceScaledCameraSpeed;
	}
	return FEditorViewportClient::ShouldScaleCameraSpeedByDistance();
}

void FMetaHumanCharacterViewportClient::OverridePostProcessSettings(FSceneView& View)
{
	View.OverridePostProcessSettings(PostProcessSettings, /* Blending Weight */ 1.0f);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::SetupViewForRendering(FSceneViewFamily& InViewFamily, FSceneView& InOutView)
{
	FEditorViewportClient::SetupViewForRendering(InViewFamily, InOutView);

	// Set the streaming boost based on the character editor project settings to allow streaming of textures even with low FoV values
	const int32 StreamingBoost = GetDefault<UMetaHumanCharacterEditorSettings>()->TextureStreamingBoost;
	const float SizeX = InOutView.UnscaledViewRect.Width();
	const float FOVScreenSize = SizeX / FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5f));
	IStreamingManager::Get().AddViewInformation(InOutView.ViewMatrices.GetViewOrigin(), SizeX, FOVScreenSize, StreamingBoost);
}

void FMetaHumanCharacterViewportClient::SetAutoFocusToSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate)
{
	if (SelectedFrame != EMetaHumanCharacterCameraFrame::Auto)
	{
		AutoSelectedFrame = SelectedFrame;

		if (AutoSelectedFrame != LastSelectedFrame)
		{
			if (UMetaHumanCharacter* Character = WeakCharacter.Get())
			{
				if (Character->ViewportSettings.CameraFrame == EMetaHumanCharacterCameraFrame::Auto)
				{
					FocusOnSelectedFrame(Character->ViewportSettings.CameraFrame, bInRotate);
				}
			}
		}
	}
}

void FMetaHumanCharacterViewportClient::RescheduleFocus()
{
	bIsViewportFramed = false;
}

void FMetaHumanCharacterViewportClient::UpdateRenderingQuality(const int32 InActiveProfileIndex)
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	if (ensure(Settings->IsValidRenderingQualityProfileIndex(InActiveProfileIndex)))
	{
		const FMetaHumanCharacterRenderingQualityProfile& ActiveProfile = Settings->GetRenderingQualityProfile(InActiveProfileIndex);
		PostProcessSettings = ActiveProfile.PostProcessSettings;

		const FMetaHumanCharacterViewportRenderingFlags& ViewportRenderingFlags = ActiveProfile.ViewportRenderingFlags;

		SetTransmissionForAllLights(ViewportRenderingFlags.bTransmissionForAllLights);

		EngineShowFlags.SetTonemapper(ViewportRenderingFlags.bTonemapper);
		EngineShowFlags.SetDepthOfField(ViewportRenderingFlags.bDepthOfField);
		EngineShowFlags.SetDynamicShadows(ViewportRenderingFlags.bDynamicShadows);
		EngineShowFlags.SetSubsurfaceScattering(ViewportRenderingFlags.bSubsurfaceScattering);
		EngineShowFlags.SetGlobalIllumination(ViewportRenderingFlags.bGlobalIllumination);
		EngineShowFlags.SetTemporalAA(ViewportRenderingFlags.bTemporalAA);

		// Don't enable Lumen show flags if the project can't actually run Lumen, otherwise
		// the renderer prints an on-screen warning about missing ray tracing data.
		bool bUseLumenGlobalIllumination = ViewportRenderingFlags.bLumenGlobalIllumination;
		bool bUseLumenReflections = ViewportRenderingFlags.bLumenReflections;
		const bool bProfileWantsLumen = bUseLumenGlobalIllumination || bUseLumenReflections;
		if ((bProfileWantsLumen && !UE::MetaHuman::Private::CanUseLumen()))
		{
			FMessageLog MessageLog(UE::MetaHuman::MessageLogName);
			MessageLog.Warning(FText::Format(
				LOCTEXT("LumenDisabledNoTracingData",
					"Lumen is enabled in rendering quality profile '{0}', but has no ray tracing data and won't operate correctly. Disabling Lumen show flags in the MetaHuman Character viewport.\n"
					"Either configure Lumen to use software distance field ray tracing and enable 'Generate Mesh Distancefields' in project settings\n"
					"or configure Lumen to use Hardware Ray Tracing and enable 'Support Hardware Ray Tracing' in project settings."),
				FText::FromString(ActiveProfile.ProfileName)));

			// Only open the message log panel once.
			static bool bOpenedMessageLog = false;
			if (!bOpenedMessageLog)
			{
				MessageLog.Open(EMessageSeverity::Warning, /*bForce=*/ false);
				bOpenedMessageLog = true;
			}

			bUseLumenGlobalIllumination = false;
			bUseLumenReflections = false;
		}

		EngineShowFlags.SetLumenGlobalIllumination(bUseLumenGlobalIllumination);
		EngineShowFlags.SetLumenReflections(bUseLumenReflections);

		SetPreviewingScreenPercentage(ViewportRenderingFlags.bPreviewingScreenPercentage);
		SetPreviewScreenPercentage(ViewportRenderingFlags.PreviewScreenPercentage);
	}
}
void FMetaHumanCharacterViewportClient::FocusOnSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate, bool bInInstant)
{
	if (UMetaHumanCharacter* Character = WeakCharacter.Get())
	{
		if (Character->ViewportSettings.CameraFrame != SelectedFrame)
		{
			Character->ViewportSettings.CameraFrame = SelectedFrame;
			Character->MarkPackageDirty();
		}
	}

	// make sure the viewport is in orbit camera mode
	ToggleOrbitCamera(true);

	if (SelectedFrame == EMetaHumanCharacterCameraFrame::Auto)
	{
		SelectedFrame = AutoSelectedFrame;
	}
	LastSelectedFrame = SelectedFrame;

	switch (SelectedFrame)
	{
	case EMetaHumanCharacterCameraFrame::Face:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnFace(0.4f, FVector{ 0, 0, 0.58f }, /*bInInstant*/ false);
		break;
	case EMetaHumanCharacterCameraFrame::Body:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnBody(0.9f, FVector{ 0, 0, 0 }, /*bInInstant*/ false);
		break;
	case EMetaHumanCharacterCameraFrame::Far:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnBody(1.25f, FVector{ 0, 0, 0 }, /*bInInstant*/ false);
		break;
	case EMetaHumanCharacterCameraFrame::Hands:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnHands(1.f, FVector{ 0, 0, 0 }, /*bInInstant*/ false);
		break;
	case EMetaHumanCharacterCameraFrame::Feet:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ -45, 180, 0 });
		}
		FocusOnFeet(1.f, FVector{ 0, 0, 0 }, /*bInInstant*/ false);
		break;
	case EMetaHumanCharacterCameraFrame::SelectedPoint:
		if (SelectedPointFocusTarget.IsSet())
		{
			if (bInRotate)
			{
				SetViewRotation(FRotator{ 0, 180, 0 });
			}
			constexpr float FocusBoxHalfExtent = 5.0f;
			const FBox PointBox = FBox(SelectedPointFocusTarget.GetValue() - FocusBoxHalfExtent, SelectedPointFocusTarget.GetValue() + FocusBoxHalfExtent);
			FocusViewportOnBox(PointBox, bInInstant);
		}
		break;
	default:
		break;
	}
}


void FMetaHumanCharacterViewportClient::SetTransmissionForAllLights(bool bTransmissionEnabled)
{
	if (UWorld* World = GetPreviewScene()->GetWorld())
	{
		// Only enable transmission if Effects is set to Epic or Cinematic
		static const TConsoleVariableData<int32>* EffectsQualityCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("sg.EffectsQuality"));
		check(EffectsQualityCVar);
		const bool bIsEffectEpicOrHigher = EffectsQualityCVar->GetValueOnAnyThread() >= 3;

		for(TActorIterator<ALight> LightIt(World); LightIt; ++LightIt)
		{
			ALight* Light = *LightIt;
			ULightComponent* LightComp = Light->GetLightComponent();

			if(LightComp)
			{
				LightComp->bTransmission = bTransmissionEnabled && bIsEffectEpicOrHigher;
				LightComp->MarkRenderStateDirty();
			}
		}
	}
}

void FMetaHumanCharacterViewportClient::FocusOnFace(float InDistanceScale, const FVector& InOffset, bool bInInstant)
{
	if (IMetaHumanCharacterEditorActorInterface* CharacterActor = WeakCharacterActor.Get())
	{
		FBoxSphereBounds FaceBounds = CharacterActor->GetFaceComponent()->Bounds;
		FaceBounds.Origin += FaceBounds.BoxExtent * InOffset;
		FaceBounds = FBoxSphereBounds(FaceBounds.Origin, FaceBounds.BoxExtent * InDistanceScale, FaceBounds.SphereRadius * InDistanceScale);
		FocusViewportOnBox(FaceBounds.GetBox(), bInInstant);
	}
}

void FMetaHumanCharacterViewportClient::FocusOnBody(float InDistanceScale, const FVector& InOffset, bool bInInstant)
{
	if (IMetaHumanCharacterEditorActorInterface* CharacterActor = WeakCharacterActor.Get())
	{
		FBoxSphereBounds FaceBounds = CharacterActor->GetFaceComponent()->Bounds;
		FBoxSphereBounds BodyBounds = CharacterActor->GetBodyComponent()->Bounds;
		FBox Bounds = BodyBounds.GetBox() + FaceBounds.GetBox();
		Bounds = Bounds.ShiftBy(Bounds.GetExtent() * InOffset);
		Bounds = Bounds.ExpandBy((InDistanceScale - 1.0f) * BodyBounds.SphereRadius);
		FocusViewportOnBox(Bounds, bInInstant);
	}
}

void FMetaHumanCharacterViewportClient::FocusOnBodyPartPair(const FName InBodyPartNameR, const FName InBodyPartNameL, const  float InDistanceScale, const FVector& InOffset, const bool bInInstant)
{
	if (const IMetaHumanCharacterEditorActorInterface* CharacterActor = WeakCharacterActor.Get())
	{
		const UPhysicsAsset* BodyPhysicsAsset = CharacterActor->GetBodyComponent()->GetPhysicsAsset();
		if (IsValid(BodyPhysicsAsset))
		{
			const int32 BodyPartRIndex = BodyPhysicsAsset->FindBodyIndex(InBodyPartNameR);
			const int32 BodyPartLIndex = BodyPhysicsAsset->FindBodyIndex(InBodyPartNameL);

			if (BodyPhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyPartRIndex) && BodyPhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyPartLIndex))
			{
				if (!BodyPhysicsAsset->SkeletalBodySetups[BodyPartRIndex]->AggGeom.BoxElems.IsEmpty() && !BodyPhysicsAsset->SkeletalBodySetups[BodyPartLIndex]->AggGeom.BoxElems.IsEmpty())
				{
					const FKBoxElem& BodyPartRBox = BodyPhysicsAsset->SkeletalBodySetups[BodyPartRIndex]->AggGeom.BoxElems[0];
					const FKBoxElem& BodyPartLBox = BodyPhysicsAsset->SkeletalBodySetups[BodyPartLIndex]->AggGeom.BoxElems[0];

					const FTransform& BodyPartRTransform = CharacterActor->GetBodyComponent()->GetBoneTransform(InBodyPartNameR);
					const FTransform& BodyPartLTransform = CharacterActor->GetBodyComponent()->GetBoneTransform(InBodyPartNameL);

					FBox Bounds = BodyPartRBox.CalcAABB(BodyPartRTransform, /* Scale */ 1.0f) + BodyPartLBox.CalcAABB(BodyPartLTransform, /* Scale */ 1.0f);
					Bounds = Bounds.ShiftBy(Bounds.GetExtent() * InOffset);
					Bounds = Bounds.ExpandBy((InDistanceScale - 1.0f) * FBoxSphereBounds(Bounds).SphereRadius);

					FocusViewportOnBox(Bounds, bInInstant);
				}
			}
		}
	}
}

void FMetaHumanCharacterViewportClient::FocusOnHands(const float InDistanceScale, const FVector& InOffset, const bool bInInstant)
{
	FocusOnBodyPartPair(FName("hand_r"), FName("hand_l"), InDistanceScale, InOffset, bInInstant);
}

void FMetaHumanCharacterViewportClient::FocusOnFeet(const float InDistanceScale, const FVector& InOffset, const bool bInInstant)
{
	FocusOnBodyPartPair(FName("foot_r"), FName("foot_l"), InDistanceScale, InOffset, bInInstant);
}

void FMetaHumanCharacterViewportClient::SetViewportWidget(const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
{
	EditorViewportWidget = InEditorViewportWidget;
}

void FMetaHumanCharacterViewportClient::DrawInfos(FCanvas* InCanvas, const FText& Title, const TArray<TPair<FText, FText>>& Infos, const FDrawInfoOptions& InDrawInfoOptions) const
{
	const float DPIInvScale = InCanvas->GetDPIScale() > 0 ? (1.0f / InCanvas->GetDPIScale()) : 1.0f;

	const int32 X = InDrawInfoOptions.TopCenter.X;
	int32 Y = InDrawInfoOptions.TopCenter.Y;
	const int32 Padding = InDrawInfoOptions.Padding;

	FCanvasTextItem TextItem(FVector2D(X, Y), Title, GEngine->GetLargeFont(), InDrawInfoOptions.TitleColor);
	if (InDrawInfoOptions.bTitleLeft)
	{
		TextItem.SetColor(FLinearColor::Transparent);
		InCanvas->DrawItem(TextItem);
		TextItem.SetColor(InDrawInfoOptions.TitleColor);
		TextItem.bOutlined = true;
		TextItem.Position.X -= TextItem.DrawnSize.X * DPIInvScale;
		InCanvas->DrawItem(TextItem);
	}
	else
	{
		InCanvas->DrawItem(TextItem);
	}

	Y += TextItem.DrawnSize.Y * DPIInvScale + Padding * 2;

	for (const TPair<FText, FText>& Info : Infos)
	{
		FText Key = FText::Format(LOCTEXT("ViewportInfoKeyFormat", "{0}: "), Info.Get<0>());
		FCanvasTextItem TextItemKey(FVector2D(X, Y), Key, GEngine->GetSmallFont(), InDrawInfoOptions.KeyTextColor);
		
		TextItemKey.SetColor(FLinearColor::Transparent);
		InCanvas->DrawItem(TextItemKey);
		TextItemKey.Position.X -= TextItemKey.DrawnSize.X * DPIInvScale;
		TextItemKey.SetColor(InDrawInfoOptions.KeyTextColor);
		InCanvas->DrawItem(TextItemKey);

		FCanvasTextItem TextItemValue(FVector2D(X, Y), Info.Get<1>(), GEngine->GetSmallFont(), InDrawInfoOptions.ValueTextColor);
		InCanvas->DrawItem(TextItemValue);

		Y += TextItem.DrawnSize.Y * DPIInvScale + Padding;
	}
}

void FMetaHumanCharacterViewportClient::Draw(FViewport* InViewport, FCanvas* InCanvas)
{
	const UMetaHumanCharacter* Character = WeakCharacter.Get();
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
	const bool bShowViewportOverlays = IsValid(Character) && Character->ViewportSettings.bShowViewportOverlays;

	// Gate the axes indicator on the same overlay toggle as the rest of the HUD.
	bDrawAxesGame = bShowViewportOverlays;

	FEditorViewportClient::Draw(InViewport, InCanvas);

	if (InCanvas && Character && Subsystem && bShowViewportOverlays)
	{
		{
			TArray<TPair<FText, FText>> StatusInfos;

			const FInt32Point FaceAlbedoResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor);
			const FInt32Point FaceNormalResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Normal);
			const FInt32Point FaceCavityResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Cavity);
			const FInt32Point FaceAnimMapsResolutions = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor_Animated_CM1);

			const FInt32Point BodyAlbedoResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Basecolor);
			const FInt32Point BodyNormalResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Normal);
			const FInt32Point BodyCavityResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Cavity);
			const FInt32Point BodyMasksResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Underwear_Mask);

			bool bIsUsingTextureOverrides = false;
			
			if (const TSharedRef<FMetaHumanCharacterEditorData>* MetaHumanCharacterEditorData = Subsystem->GetMetaHumanCharacterEditorData(Character))
			{
				if (MetaHumanCharacterEditorData->Get().SkinSettings.IsSet() && MetaHumanCharacterEditorData->Get().SkinSettings.GetValue().TextureMaterialOverrides.bEnableTextureOverrides)
				{
					bIsUsingTextureOverrides = true;
				}
			}
			else if (Character->SkinSettings.TextureMaterialOverrides.bEnableTextureOverrides)
			{
				bIsUsingTextureOverrides = true;
			}

			const FText TextureSourcesInfo = LOCTEXT("TextureSourcesKey", "Texture Sources");

			if (bIsUsingTextureOverrides)
			{
				StatusInfos.Add({ TextureSourcesInfo, LOCTEXT("TextureSourcesOverriden", "Overrides") });
			}
			else
			{
				FText TextureSourceStatus = LOCTEXT("TextureSourceUpToDate", "Up to date");

				if (Character->NeedsToDownloadTextureSources())
				{
					TextureSourceStatus = LOCTEXT("TextureSourcesNeeedsDownload", "Needs download");
				}

				StatusInfos.Add({ TextureSourcesInfo, TextureSourceStatus });

				auto GetTextureResolutionText = [](const FInt32Point& Res) -> FText
				{
					const int32 ResK = Res.X / 1024;
					if (ResK <= 0)
					{
						return LOCTEXT("TextureSourcesUnavailable", "Unavailable");
					}
					return FText::Format(LOCTEXT("TextureResolutionFormat", "{0}k"), ResK);
				};

				const FText FaceTexturesInfo1 = FText::Format(LOCTEXT("FaceTexturesSourcesInfo1", "Albedo: {0} Normal: {1}"),
															 GetTextureResolutionText(FaceAlbedoResolution),
															 GetTextureResolutionText(FaceNormalResolution));

				const FText FaceTexturesInfo2 = FText::Format(LOCTEXT("FaceTexturesSourceInfo2", "Cavity {0}: Anim. Maps: {1}"),
															 GetTextureResolutionText(FaceCavityResolution),
															 GetTextureResolutionText(FaceAnimMapsResolutions));

				const FText BodyTextureInfo1 = FText::Format(LOCTEXT("BodyTexturesSourcesInfo1", "Albedo: {0} Normal: {1}"),
															 GetTextureResolutionText(BodyAlbedoResolution),
															 GetTextureResolutionText(BodyNormalResolution));

				const FText BodyTextureInfo2 = FText::Format(LOCTEXT("BodyTetureSourcesInfo2", "Cavity: {0}, Masks: {1}"),
															 GetTextureResolutionText(BodyCavityResolution),
															 GetTextureResolutionText(BodyMasksResolution));

				StatusInfos.Add({ LOCTEXT("FaceTexturesKey", "Face"), FaceTexturesInfo1});
				StatusInfos.Add({ FText::GetEmpty(), FaceTexturesInfo2 });
				StatusInfos.Add({ LOCTEXT("BodyTexturesKey", "Body"), BodyTextureInfo1 });
				StatusInfos.Add({ FText::GetEmpty(), BodyTextureInfo2 });
			}

			FText RigStateTextValue = LOCTEXT("RigStateUnrigged", "Unrigged");
			if (Character->HasFaceDNA())
			{
				if (Character->HasFaceDNABlendshapes())
				{
					RigStateTextValue = LOCTEXT("RigStateJointsAndBlendshapes", "Joints and Blend Shapes");
				}
				else
				{
					RigStateTextValue = LOCTEXT("RigStateJointsOnly", "Joints Only");
				}
			}
			else if (Subsystem->GetRiggingState(Character) == EMetaHumanCharacterRigState::RigPending)
			{
				RigStateTextValue = LOCTEXT("RigStatePending", "Pending");
			}

			const FText BodyTypeTextValue = (Character->bFixedBodyType) ? LOCTEXT("BodyTypeValueFixed", "Fixed") : LOCTEXT("BodyTypeValueParametric", "Parametric");
			StatusInfos.Add({ LOCTEXT("BodyTypeKey", "Body Type"), BodyTypeTextValue });
			StatusInfos.Add({ LOCTEXT("RigStateKey", "Rig State"), RigStateTextValue });

			FDrawInfoOptions DrawInfoOptions;
			DrawInfoOptions.bTitleLeft = false;
			DrawInfoOptions.TopCenter.X = 140;
			DrawInfoOptions.TopCenter.Y = 20;
			DrawInfoOptions.KeyTextColor = FLinearColor::White;
			DrawInfoOptions.ValueTextColor = FLinearColor::Gray;
			DrawInfos(InCanvas, LOCTEXT("StatusTitle", "Status"), StatusInfos, DrawInfoOptions);
		}

		if (!Shortcuts.IsEmpty())
		{
			FDrawInfoOptions DrawInfoOptions;
			DrawInfoOptions.bTitleLeft = false;
			DrawInfoOptions.TopCenter.X = InViewport->GetSizeXY().X / InCanvas->GetDPIScale()  - 210;
			DrawInfoOptions.TopCenter.Y = 20;
			DrawInfoOptions.KeyTextColor = FLinearColor::White;
			DrawInfoOptions.ValueTextColor = FStyleColors::AccentBlue.GetSpecifiedColor();
			DrawInfos(InCanvas, LOCTEXT("ShortcutsTitle", "Hotkeys"), Shortcuts, DrawInfoOptions);
		}

		if(!FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled())
		{
			FText OptionalContentMissingText = LOCTEXT("OptionalContentMissingViewportMessage", "METAHUMAN CREATOR CORE DATA IS MISSING.");
			FText OptionalContentMissingTextContext = LOCTEXT("OptionalContentMissingViewportContext", "Some features will be unavailable until it's added to your project.");
			FLinearColor TextColor = FLinearColor::Red;
			float FontScale = 1.5f;
			UFont* Font = GEngine->GetMediumFont();
			FVector2D ScreenSize = FVector2D(InViewport->GetSizeXY());
			float X = ScreenSize.X / InCanvas->GetDPIScale() * 0.5f - 200.f;
			float Y = ScreenSize.Y / InCanvas->GetDPIScale() - 50.f;

			FCanvasTextItem OptionalContentTextItem(FVector2D(X, Y), OptionalContentMissingText, Font, TextColor);
			FCanvasTextItem OptionalContentContextTextItem(FVector2D(X - 70.f, Y + 20.f), OptionalContentMissingTextContext, Font, TextColor);

			OptionalContentTextItem.Scale = FVector2D(FontScale, FontScale);
			OptionalContentTextItem.bCentreX = false;
			OptionalContentTextItem.bCentreY = false;
			OptionalContentTextItem.EnableShadow(FLinearColor::Black);
			
			OptionalContentContextTextItem.Scale = FVector2D(FontScale, FontScale);
			OptionalContentContextTextItem.bCentreX = false;
			OptionalContentContextTextItem.bCentreY = false;
			OptionalContentContextTextItem.EnableShadow(FLinearColor::Black);

			InCanvas->DrawItem(OptionalContentTextItem);
			InCanvas->DrawItem(OptionalContentContextTextItem);
		}
	}	
}

void FMetaHumanCharacterViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (bDrawFaceBones)
	{
		if (WeakCharacterActor.Get())
		{
			DrawDebugBones(WeakCharacterActor.Get()->GetFaceComponent(), /*bIsFace*/ true, PDI);	
		}
	}

	if (bDrawBodyBones)
	{
		if (WeakCharacterActor.Get())
		{
			DrawDebugBones(WeakCharacterActor.Get()->GetBodyComponent(), /*bIsFace*/ false, PDI);
		}
	}
}

void FMetaHumanCharacterViewportClient::ClearShortcuts()
{
	using namespace UE::MetaHuman::Private;

	Shortcuts.Reset();
	Shortcuts.Append(DefaultShortcuts);
}

void FMetaHumanCharacterViewportClient::SetShortcuts(const TArray<TPair<FText, FText>>& InShortcuts)
{
	Shortcuts.Append(InShortcuts);
}

void FMetaHumanCharacterViewportClient::ShowFaceBonesOnCharacter(const bool InDrawFaceBones)
{
	bDrawFaceBones = InDrawFaceBones;
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyBonesOnCharacter(const bool InDrawBodyBones)
{
	bDrawBodyBones = InDrawBodyBones;
	Invalidate();
}

void FMetaHumanCharacterViewportClient::DrawDebugBones(const USkeletalMeshComponent* MetaHumanSkeletalMeshComponent, bool bIsFaceComponent, FPrimitiveDrawInterface* PDI)
{
	const UMetaHumanCharacter* Character = WeakCharacter.Get();
	float BoneSize = 1.f;
	if(Character)
	{
		if(bIsFaceComponent)
		{
			BoneSize = Character->ViewportSettings.FaceBoneSize;
		}
		else
		{
			BoneSize = Character->ViewportSettings.BodyBoneSize;
		}
	}

	FSkelDebugDrawConfig DrawConfig;
	DrawConfig.BoneDrawMode = EBoneDrawMode::All;
	DrawConfig.BoneDrawSize = BoneSize;
	DrawConfig.bForceDraw = true;
	DrawConfig.bAddHitProxy = false;
	DrawConfig.bUseMultiColorAsDefaultColor = true;
	DrawConfig.DefaultBoneColor = FLinearColor::Green;
	DrawConfig.AffectedBoneColor = FLinearColor::Red;
	DrawConfig.SelectedBoneColor = FLinearColor::Blue;
	DrawConfig.ParentOfSelectedBoneColor = FLinearColor::Black;

	TArray<int32> SelectedBones;

	if(bIsFaceComponent)
	{
		const TArray<FTransform>& FaceSpaceTransforms = MetaHumanSkeletalMeshComponent->GetComponentSpaceTransforms();

		const TArray<FBoneIndexType>& DrawFaceBoneIndices = MetaHumanSkeletalMeshComponent->RequiredBones;
		for (int32 Index = 0; Index < DrawFaceBoneIndices.Num(); ++Index)
		{
			const int32 BoneIndex = DrawFaceBoneIndices[Index];
			FaceBonesWorldTransforms[BoneIndex] = FaceSpaceTransforms[BoneIndex] * MetaHumanSkeletalMeshComponent->GetComponentTransform();
			FaceBoneColors[BoneIndex] = SkeletalDebugRendering::GetSemiRandomColorForBone(Index);
		}

		SkeletalDebugRendering::DrawBones(
			PDI,
			MetaHumanSkeletalMeshComponent->GetComponentLocation(),
			MetaHumanSkeletalMeshComponent->RequiredBones,
			MetaHumanSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton(),
			FaceBonesWorldTransforms,
			SelectedBones,
			FaceBoneColors,
			HitProxies,
			DrawConfig
		);
	}
	else
	{	
		TArray<FTransform> OverrideDebugBodyTransforms;
		
		if (Character)
		{
			if (const TSharedRef<FMetaHumanCharacterEditorData>* MetaHumanCharacterEditorData = UMetaHumanCharacterEditorSubsystem::Get()->GetMetaHumanCharacterEditorData(Character))
			{
				OverrideDebugBodyTransforms = MetaHumanCharacterEditorData->Get().OverrideDebugBodyBoneTransforms;
			}
		}
				
		const TArray<FTransform>& BodySpaceTransforms = (OverrideDebugBodyTransforms.Num() ==  MetaHumanSkeletalMeshComponent->GetComponentSpaceTransforms().Num()) ? 
			OverrideDebugBodyTransforms : 
			MetaHumanSkeletalMeshComponent->GetComponentSpaceTransforms();

		const TArray<FBoneIndexType>& DrawBodyBoneIndices = MetaHumanSkeletalMeshComponent->RequiredBones;
		for (int32 Index = 0; Index < DrawBodyBoneIndices.Num(); ++Index)
		{
			const int32 BoneIndex = DrawBodyBoneIndices[Index];
			BodyBonesWorldTransforms[BoneIndex] = BodySpaceTransforms[BoneIndex] * MetaHumanSkeletalMeshComponent->GetComponentTransform();
			BodyBoneColors[BoneIndex] = SkeletalDebugRendering::GetSemiRandomColorForBone(Index);
		}

		SkeletalDebugRendering::DrawBones(
			PDI,
			MetaHumanSkeletalMeshComponent->GetComponentLocation(),
			MetaHumanSkeletalMeshComponent->RequiredBones,
			MetaHumanSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton(),
			BodyBonesWorldTransforms,
			SelectedBones,
			BodyBoneColors,
			HitProxies,
			DrawConfig
		);
	}
}

void FMetaHumanCharacterViewportClient::ShowFaceNormalsOnCharacter(const bool InShowFaceNormals)
{
	WeakCharacterActor.Get()->SetShowNormalsOnFace(InShowFaceNormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyNormalsOnCharacter(const bool InShowBodyNormals)
{
	WeakCharacterActor.Get()->SetShowNormalsOnBody(InShowBodyNormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowFaceTangentsOnCharacter(const bool InShowFaceTangents)
{
	WeakCharacterActor.Get()->SetShowTangentsOnFace(InShowFaceTangents);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyTangentsOnCharacter(const bool InShowBodyTangents)
{
	WeakCharacterActor.Get()->SetShowTangentsOnBody(InShowBodyTangents);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowFaceBinormalsOnCharacter(const bool InShowFaceBinormals)
{
	WeakCharacterActor.Get()->SetShowBinormalsOnFace(InShowFaceBinormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyBinormalsOnCharacter(const bool InShowBodyBinormals)
{
	WeakCharacterActor.Get()->SetShowBinormalsOnBody(InShowBodyBinormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::SetSelectedPoint(const FVector& InPoint)
{
	SelectedPointFocusTarget = InPoint;
	OverrideNextFocusFrame = EMetaHumanCharacterCameraFrame::SelectedPoint;
	if (UMetaHumanCharacter* Character = WeakCharacter.Get())
	{
		Character->ViewportSettings.CameraFrame = EMetaHumanCharacterCameraFrame::SelectedPoint;
	}
}

void FMetaHumanCharacterViewportClient::HandleCameraFocusRequest(UMetaHumanCharacter* InCharacter, EMetaHumanCharacterCameraFrame InFrameToFocus)
{
	if (WeakCharacter.Get() != InCharacter)
	{
		return;
	}

	FocusOnSelectedFrame(InFrameToFocus, /*bInRotate*/ false);
}

void FMetaHumanCharacterViewportClient::MouseMove(FViewport* InViewport, int32 X, int32 Y)
{
	FEditorViewportClient::MouseMove(InViewport, X, Y);
	PreviousMousePosition.Reset();
	NextMousePosition.Reset();
}

void FMetaHumanCharacterViewportClient::ProcessAccumulatedPointerInput(FViewport* InViewport)
{
	// Hold L + left-mouse drag rotates the light rig. Skip when no rig is present so we don't silently mutate the cached LightRotation.
	if (bEnvironmentHasLightRig && PreviousMousePosition.IsSet() && NextMousePosition.IsSet() && InViewport->KeyState(EKeys::LeftMouseButton) && InViewport->KeyState(EKeys::L) && InViewport->GetSizeXY().X > 0)
	{
		UMetaHumanCharacter* Character = WeakCharacter.Get();
		const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		if (Character && Subsystem)
		{
			float LightRotation = Character->ViewportSettings.LightRotation;
			int32 Delta = NextMousePosition.GetValue().X - PreviousMousePosition.GetValue().X;
			if (Delta != 0)
			{
				LightRotation += float(Delta) / float(InViewport->GetSizeXY().X) * 360.0f;
				LightRotation = FMath::Clamp(LightRotation, -270.0f, 270.0f);
				Subsystem->UpdateLightRotation(Character, LightRotation);
				Invalidate();
			}
		}
	}
	PreviousMousePosition = NextMousePosition;

	FEditorViewportClient::ProcessAccumulatedPointerInput(InViewport);
}
void FMetaHumanCharacterViewportClient::CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y)
{
	if (InViewport->KeyState(EKeys::LeftMouseButton) && InViewport->KeyState(EKeys::L) && InViewport->GetSizeXY().X > 0)
	{
		if (!PreviousMousePosition.IsSet()) PreviousMousePosition = FInt32Point(X, Y);
		NextMousePosition = FInt32Point(X, Y);
	}
	else
	{
		PreviousMousePosition.Reset();
		NextMousePosition.Reset();
		FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
	}
}

#undef LOCTEXT_NAMESPACE
