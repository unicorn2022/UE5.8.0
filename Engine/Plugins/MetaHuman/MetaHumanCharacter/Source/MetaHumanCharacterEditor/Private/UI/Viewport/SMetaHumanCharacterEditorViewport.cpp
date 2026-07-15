// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorViewport.h"

#include "Brushes/SlateNoResource.h"
#include "BufferVisualizationMenuCommands.h"
#include "Components/SceneCaptureComponent2D.h"
#include "EditorModeManager.h"
#include "EditorViewportCommands.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GroomVisualizationMenuCommands.h"
#include "GroomVisualizationData.h"
#include "InteractiveToolManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "LumenVisualizationMenuCommands.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorMode.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "NaniteVisualizationMenuCommands.h"
#include "SEditorViewportToolBarButton.h"
#include "SMetaHumanCharacterEditorViewportToolBar.h"
#include "SMetaHumanOverlayWidget.h"
#include "STrackerImageViewer.h"
#include "SViewportToolBar.h"
#include "SubstrateVisualizationMenuCommands.h"
#include "RenderUtils.h"
#include "ToolMenus.h"
#include "UI/Viewport/SMetaHumanCharacterEditorViewportAnimationBar.h"
#include "UI/Viewport/SMetaHumanCharacterEditor2DViewportOverlay.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTileView.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "VirtualShadowMapVisualizationMenuCommands.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorViewport"

void SMetaHumanCharacterEditorViewport::Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs)
{
	TSharedPtr<FMetaHumanCharacterViewportClient> ViewportClient = StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(InArgs._EditorViewportClient);
	check(ViewportClient.IsValid());

	CurrentViewportBrush = MakeShared<FSlateBrush>();
	TrackerImageViewerBrush = MakeShared<FSlateBrush>();

	// Needs to be created before the call to SetEditorViewportWidget
	SAssignNew(TrackerImageViewer, SMetaHumanOverlayWidget<STrackerImageViewer>)
		.ShouldDrawCurves(true)
		.ShouldDrawPoints(this, &SMetaHumanCharacterEditorViewport::ShouldDrawTrackingPoints);

	const bool bManagedTextures = false;
	TrackerImageViewer->Setup(bManagedTextures);
	TrackerImageViewer->OnInvalidate().AddLambda([this]
		{
			if (Client.IsValid())
			{
				Client->Invalidate();
			}
		});
	TrackerImageViewer->SetNavigationMode(EABImageNavigationMode::TwoD);
	TrackerImageViewer->SetVisibility(EVisibility::Hidden);

	// Give the viewport client a reference to the viewport as we
	// can't pass it in the constructor due to restrictions on FBaseAssetToolkit
	ViewportClient->SetViewportWidget(SharedThis(this));

	SAssetEditorViewport::Construct(SAssetEditorViewport::FArguments()
		.EditorViewportClient(InArgs._EditorViewportClient),
		InViewportConstructionArgs);
}

TSharedRef<FMetaHumanCharacterViewportClient> SMetaHumanCharacterEditorViewport::GetMetaHumanCharacterEditorViewportClient() const
{
	return StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(Client).ToSharedRef();
}

TSharedRef<SMetaHumanOverlayWidget<STrackerImageViewer>> SMetaHumanCharacterEditorViewport::GetTrackerImageViewer() const
{
	return TrackerImageViewer.ToSharedRef();
}

UTextureRenderTarget2D* SMetaHumanCharacterEditorViewport::GetOrCreateTrackingTexture(UObject* WorldContextObject, UTexture* SourceTexture, const FIntPoint& SourceSize, const FIntPoint& TargetSize)
{
	if (!WorldContextObject || !SourceTexture || SourceSize.X <= 0 || SourceSize.Y <= 0 || TargetSize.X <= 0 || TargetSize.Y <= 0)
	{
		return nullptr;
	}

	if (!TrackingRenderTarget)
	{
		TrackingRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		TrackingRenderTarget->RenderTargetFormat = RTF_RGBA8;
		TrackingRenderTarget->ClearColor = FLinearColor::Black;
		TrackingRenderTarget->InitAutoFormat(TargetSize.X, TargetSize.Y);
		TrackingRenderTarget->UpdateResourceImmediate(true);
	}
	else
	{
		if (TrackingRenderTarget->SizeX != TargetSize.X || TrackingRenderTarget->SizeY != TargetSize.Y)
		{
			TrackingRenderTarget->ResizeTarget(TargetSize.X, TargetSize.Y);
		}
	}

	// Clear to black first
	UKismetRenderingLibrary::ClearRenderTarget2D(WorldContextObject, TrackingRenderTarget, FLinearColor::Black);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;

	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(WorldContextObject, TrackingRenderTarget, Canvas, CanvasSize, Context);

	if (Canvas)
	{
		const FBox2D FitRect = ComputeTextureFitRect(SourceSize, TargetSize);
		const FVector2D DrawPosition = FitRect.Min;
		const FVector2D DrawSize = FitRect.Max - FitRect.Min;

		Canvas->K2_DrawTexture(SourceTexture, DrawPosition, DrawSize, FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), FLinearColor::White, BLEND_Opaque, 0.0f, FVector2D::ZeroVector);
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(WorldContextObject, Context);

	return TrackingRenderTarget;
}

void SMetaHumanCharacterEditorViewport::SetTrackerImageTexture(UTexture* InTexture, const FIntPoint& InImageSize)
{
	if (TrackerImageViewer.IsValid() && InTexture)
	{
		const FVector2D ViewportSize = CurrentViewportGeometry.GetLocalSize();
		UpdateTrackerImageBrush(InTexture, FIntPoint(ViewportSize.X, ViewportSize.Y));

		TrackerImageViewer->SetTextures(InTexture, nullptr);
		TrackerImageViewer->SetTrackerImageSize(InImageSize);
	}
}

void SMetaHumanCharacterEditorViewport::Update2DViewportOverlay(bool bEnable2DView)
{
	if (bEnable2DView)
	{
		CaptureCurrentView();
		
		if(UTextureRenderTarget2D* RenderTarget = SceneCaptureComponent ? SceneCaptureComponent->TextureTarget : nullptr)
		{
			const FIntPoint ImageSize = FIntPoint(RenderTarget->SizeX, RenderTarget->SizeY);
			UpdateCurrentViewportImageBrush(RenderTarget, ImageSize);
		}
	}
}

void SMetaHumanCharacterEditorViewport::Tick(const FGeometry& InAllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SAssetEditorViewport::Tick(InAllottedGeometry, InCurrentTime, InDeltaTime);

	if (ViewportWidget.IsValid())
	{
		const FGeometry& ViewportWidgetGeometry = ViewportWidget->GetCachedGeometry();
		if (ViewportWidgetGeometry != CurrentViewportGeometry)
		{
			bool UpdateGeometry = ViewportWidgetGeometry.GetLocalSize() != CurrentViewportGeometry.GetLocalSize();
			CurrentViewportGeometry = ViewportWidgetGeometry;

			const FVector2D& ViewportSize = ViewportWidgetGeometry.GetLocalSize();
			if (UpdateGeometry && ViewportSize != FVector2D::ZeroVector)
			{
				GetTrackerImageViewer()->ResetView();
			}

			OnViewportSizeChangedDelegate.ExecuteIfBound(ViewportSize);
		}
	}
}

void SMetaHumanCharacterEditorViewport::PopulateViewportOverlays(TSharedRef<SOverlay> InOverlay)
{
	InOverlay->AddSlot()
		[
			TrackerImageViewer.ToSharedRef()
		];

	InOverlay->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(32.f)
		[
			SNew(SMetaHumanCharacterEditor2DViewportOverlay)
			.Label(this, &SMetaHumanCharacterEditorViewport::Get2DViewportModeOverlayLabelText)
			.ImageBrush(this, &SMetaHumanCharacterEditorViewport::Get2DViewportModeOverlayImageBrush)
			.OnMouseButtonDown(this, &SMetaHumanCharacterEditorViewport::On2DViewportModeOverlayClicked)
			.Visibility(this, &SMetaHumanCharacterEditorViewport::Get2DViewportModeOverlayVisibility)
		];
}

TSharedPtr<SWidget> SMetaHumanCharacterEditorViewport::BuildViewportToolbar()
{
	// Register the viewport toolbar if another viewport hasn't already (it's shared).
	const FName ViewportToolbarName = "MetaHumanCharacterEditorViewport.ViewportToolbar";

	if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
	{
		UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(
			ViewportToolbarName, NAME_None /* parent */, EMultiBoxType::SlimHorizontalToolBar
		);

		//ViewportToolbarMenu->SetStyleSet(&FLevelEditorStyle::Get());
		ViewportToolbarMenu->StyleName = "ViewportToolbar";
		ViewportToolbarMenu->bSeparateSections = false;

		// Add the left-aligned part of the viewport toolbar.
		{
			FToolMenuSection& LeftSection = ViewportToolbarMenu->AddSection("Left");
			LeftSection.Alignment = EToolMenuSectionAlign::First;
			LeftSection.AddEntry(Create3DViewToggle());
			LeftSection.AddEntry(Create2DViewFacialTracingToggle());
			LeftSection.AddEntry(Create2DViewOverlayVisibilityToggle());

		}

		// Add the right-aligned part of the viewport toolbar.
		{
		// TODO: Switch to AddSubmenu calls because we don't need dynamic entries for no reason
			FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection("Right");
			RightSection.Alignment = EToolMenuSectionAlign::Last;
			RightSection.AddEntry(CreatePreviewMaterialSubmenu());
			RightSection.AddEntry(CreateEnvironmentSubmenu());
			RightSection.AddEntry(CreateCameraSelectionSubmenu());
			RightSection.AddEntry(CreateLODSubmenu());
			RightSection.AddEntry(CreateRenderingQualitySubmenu());
			RightSection.AddEntry(CreateViewModesSubmenu());
			RightSection.AddEntry(CreateDebugSubmenu());
			RightSection.AddEntry(CreateViewportOverlayToggle());
		}
	}

	FToolMenuContext ViewportToolbarContext;
	{
		ViewportToolbarContext.AppendCommandList(GetCommandList());

		// Add the UnrealEd viewport toolbar context.
		{
			UUnrealEdViewportToolbarContext* const ContextObject = NewObject<UUnrealEdViewportToolbarContext>();
			ContextObject->Viewport = SharedThis(this);
			ViewportToolbarContext.AddObject(ContextObject);
		}
	}

	return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SMetaHumanCharacterEditorViewport::BindCommands()
{
	const FMetaHumanCharacterEditorViewportToolbarCommands& Commands = FMetaHumanCharacterEditorViewportToolbarCommands::Get();

	CommandList->MapAction(
		Commands.FocusFace,
		FExecuteAction::CreateSP(this, &SMetaHumanCharacterEditorViewport::FocusOnFrame, EMetaHumanCharacterCameraFrame::Face),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SMetaHumanCharacterEditorViewport::IsCameraFrameChecked, EMetaHumanCharacterCameraFrame::Face)
	);
	CommandList->MapAction(
		Commands.FocusBody,
		FExecuteAction::CreateSP(this, &SMetaHumanCharacterEditorViewport::FocusOnFrame, EMetaHumanCharacterCameraFrame::Body),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SMetaHumanCharacterEditorViewport::IsCameraFrameChecked, EMetaHumanCharacterCameraFrame::Body)
	);
	CommandList->MapAction(
		Commands.FocusFar,
		FExecuteAction::CreateSP(this, &SMetaHumanCharacterEditorViewport::FocusOnFrame, EMetaHumanCharacterCameraFrame::Far),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SMetaHumanCharacterEditorViewport::IsCameraFrameChecked, EMetaHumanCharacterCameraFrame::Far)
	);
	CommandList->MapAction(
		Commands.FocusHands,
		FExecuteAction::CreateSP(this, &SMetaHumanCharacterEditorViewport::FocusOnFrame, EMetaHumanCharacterCameraFrame::Hands),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SMetaHumanCharacterEditorViewport::IsCameraFrameChecked, EMetaHumanCharacterCameraFrame::Hands)
	);
	CommandList->MapAction(
		Commands.FocusFeet,
		FExecuteAction::CreateSP(this, &SMetaHumanCharacterEditorViewport::FocusOnFrame, EMetaHumanCharacterCameraFrame::Feet),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &SMetaHumanCharacterEditorViewport::IsCameraFrameChecked, EMetaHumanCharacterCameraFrame::Feet)
	);

	SAssetEditorViewport::BindCommands();

	// Re-bind ToggleDistanceBasedCameraSpeed so it reads/writes per-character ViewportSettings instead of the global ULevelEditorViewportSettings used by SEditorViewport.
	{
		TWeakPtr<SMetaHumanCharacterEditorViewport> WeakSelf = SharedThis(this);
		CommandList->UnmapAction(FEditorViewportCommands::Get().ToggleDistanceBasedCameraSpeed);
		CommandList->MapAction(
			FEditorViewportCommands::Get().ToggleDistanceBasedCameraSpeed,
			FExecuteAction::CreateLambda([WeakSelf]()
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> Self = WeakSelf.Pin())
				{
					if (UMetaHumanCharacter* Character = Self->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
					{
						Character->ViewportSettings.bUseDistanceScaledCameraSpeed = !Character->ViewportSettings.bUseDistanceScaledCameraSpeed;
					}
				}
			}),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([WeakSelf]()
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> Self = WeakSelf.Pin())
				{
					if (const UMetaHumanCharacter* Character = Self->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
					{
						return Character->ViewportSettings.bUseDistanceScaledCameraSpeed;
					}
				}
				return false;
			})
		);
	}

	TSharedRef<FEditorViewportClient> ClientRef = Client.ToSharedRef();
	FBufferVisualizationMenuCommands::Get().BindCommands(*CommandList, ClientRef);
	FNaniteVisualizationMenuCommands::Get().BindCommands(*CommandList, ClientRef);
	FLumenVisualizationMenuCommands::Get().BindCommands(*CommandList, ClientRef);
	if (Substrate::IsSubstrateEnabled())
	{
		FSubstrateVisualizationMenuCommands::Get().BindCommands(*CommandList, ClientRef);
	}
	if (IsGroomEnabled())
	{
		FGroomVisualizationMenuCommands::Get().BindCommands(*CommandList, ClientRef);
	}
	FVirtualShadowMapVisualizationMenuCommands::Get().BindCommands(*CommandList, ClientRef);
}

void SMetaHumanCharacterEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SceneCaptureComponent);
	Collector.AddReferencedObject(TrackingRenderTarget);
}

FString SMetaHumanCharacterEditorViewport::GetReferencerName() const
{
	return TEXT("SMetaHumanCharacterEditorViewport");
}

SMetaHumanCharacterEditorViewport::~SMetaHumanCharacterEditorViewport()
{
	// Without this, the component might outlive the world it was registered into
	if (IsValid(SceneCaptureComponent))
	{
		SceneCaptureComponent->DestroyComponent();
		SceneCaptureComponent = nullptr;
	}

	TrackingRenderTarget = nullptr;
}

void SMetaHumanCharacterEditorViewport::InitializeSceneCaptureComponent()
{
	UWorld* TargetWorld = nullptr;
	if (UInteractiveToolManager* ToolManager = GetToolManager())
	{
		if (IToolsContextQueriesAPI* QueriesAPI = ToolManager->GetContextQueriesAPI())
		{
			TargetWorld = QueriesAPI->GetCurrentEditingWorld();
		}
	}

	if (SceneCaptureComponent && (!IsValid(SceneCaptureComponent) || SceneCaptureComponent->GetWorld() != TargetWorld))
	{
		if (IsValid(SceneCaptureComponent))
		{
			SceneCaptureComponent->DestroyComponent();
		}

		SceneCaptureComponent = nullptr;
	}

	if (SceneCaptureComponent)
	{
		return;
	}

	if (TargetWorld)
	{
		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);

		const FVector2D& ViewportSize = CurrentViewportGeometry.GetLocalSize();
		if (RenderTarget && ViewportSize.X > 0 && ViewportSize.Y > 0)
		{
			RenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y);
			RenderTarget->UpdateResourceImmediate(false);
		}

		SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
		SceneCaptureComponent->TextureTarget = RenderTarget;
		SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
		SceneCaptureComponent->bCaptureEveryFrame = false;
		SceneCaptureComponent->bCaptureOnMovement = false;
		SceneCaptureComponent->bAlwaysPersistRenderingState = true;

		SceneCaptureComponent->RegisterComponentWithWorld(TargetWorld);
	}
}

void SMetaHumanCharacterEditorViewport::CaptureCurrentView()
{
	UInteractiveToolManager* ToolManager = GetToolManager();
	if (!ToolManager)
	{
		return;
	}

	InitializeSceneCaptureComponent();
	if (!SceneCaptureComponent)
	{
		return;
	}

	FViewCameraState ViewState;
	if (const IToolsContextQueriesAPI* QueriesAPI = ToolManager->GetContextQueriesAPI())
	{
		QueriesAPI->GetCurrentViewState(ViewState);
	}

	SceneCaptureComponent->SetWorldRotation(ViewState.Orientation.Rotator());
	if (ViewState.bIsOrthographic)
	{
		SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
		SceneCaptureComponent->OrthoWidth = ViewState.OrthoWorldCoordinateWidth;

		constexpr float BackUpDist = 1000.0f;
		SceneCaptureComponent->SetWorldLocation(ViewState.Position - SceneCaptureComponent->GetForwardVector() * BackUpDist);
	}
	else
	{
		SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
		SceneCaptureComponent->FOVAngle = ViewState.HorizontalFOVDegrees;
		SceneCaptureComponent->SetWorldLocation(ViewState.Position);
	}

	const FVector2D& ViewportSize = CurrentViewportGeometry.GetLocalSize();
	UTextureRenderTarget2D* RenderTarget = SceneCaptureComponent->TextureTarget;
	if (RenderTarget && ViewportSize.X > 0 && ViewportSize.Y > 0)
	{
		RenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y);
		RenderTarget->UpdateResourceImmediate(true);
	}

	SceneCaptureComponent->CaptureScene();
}

FBox2D SMetaHumanCharacterEditorViewport::ComputeTextureFitRect(const FIntPoint& SourceSize, const FIntPoint& TargetSize) const
{
	if (SourceSize.X <= 0 || SourceSize.Y <= 0 || TargetSize.X <= 0 || TargetSize.Y <= 0)
	{
		return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
	}

	const double SourceAspect = static_cast<double>(SourceSize.X) / static_cast<double>(SourceSize.Y);
	const double TargetAspect = static_cast<double>(TargetSize.X) / static_cast<double>(TargetSize.Y);

	double DrawWidth = 0.0;
	double DrawHeight = 0.0;
	const bool bIsExpanding = TargetSize.X > SourceSize.X || TargetSize.Y > SourceSize.Y;
	if (!bIsExpanding)
	{
		// SHRINKING
		if (TargetAspect > SourceAspect)
		{
			// Target is wider: fit height, black bands left/right.
			DrawHeight = TargetSize.Y;
			DrawWidth = DrawHeight * SourceAspect;
		}
		else
		{
			// Target is taller/narrower: fit width, black bands top/bottom.
			DrawWidth = TargetSize.X;
			DrawHeight = DrawWidth / SourceAspect;
		}
	}
	else
	{
		// EXPANDING
		if (TargetAspect > SourceAspect)
		{
			// Target is wider: fit width, crop top/bottom.
			DrawWidth = TargetSize.X;
			DrawHeight = DrawWidth / SourceAspect;
		}
		else
		{
			// Target is taller/narrower: fit height, crop left/right.
			DrawHeight = TargetSize.Y;
			DrawWidth = DrawHeight * SourceAspect;
		}
	}

	const double OffsetX = (static_cast<double>(TargetSize.X) - DrawWidth) * 0.5;
	const double OffsetY = (static_cast<double>(TargetSize.Y) - DrawHeight) * 0.5;

	return FBox2D(FVector2D(OffsetX, OffsetY), FVector2D(OffsetX + DrawWidth, OffsetY + DrawHeight));
}

UInteractiveToolManager* SMetaHumanCharacterEditorViewport::GetToolManager() const
{
	FEditorModeTools* ModeTools = Client.IsValid() ? Client->GetModeTools() : nullptr;	
	UEdMode* ActiveMode = ModeTools ? ModeTools->GetActiveScriptableMode(UMetaHumanCharacterEditorMode::EM_MetaHumanCharacterEditorModeId) : nullptr;
	if(ActiveMode)
	{
		return ActiveMode->GetToolManager();
	}

	return nullptr;
}

void SMetaHumanCharacterEditorViewport::UpdateCurrentViewportImageBrush(UTexture* InTexture, const FIntPoint& InImageSize)
{
	if (CurrentViewportBrush.IsValid() && InTexture)
	{
		CurrentViewportBrush->SetResourceObject(InTexture);
		CurrentViewportBrush->ImageSize = InImageSize;
		CurrentViewportBrush->DrawAs = ESlateBrushDrawType::Image;
	}
}

void SMetaHumanCharacterEditorViewport::UpdateTrackerImageBrush(UTexture* InTexture, const FIntPoint& InImageSize)
{
	if (TrackerImageViewerBrush.IsValid() && InTexture)
	{
		TrackerImageViewerBrush->SetResourceObject(InTexture);
		TrackerImageViewerBrush->ImageSize = InImageSize;
		TrackerImageViewerBrush->DrawAs = ESlateBrushDrawType::Image;
	}
}

FText SMetaHumanCharacterEditorViewport::Get2DViewportModeOverlayLabelText() const
{
	return
		TrackerImageViewer.IsValid() && TrackerImageViewer->GetVisibility() == EVisibility::Visible ?
		LOCTEXT("3DViewportOverlayLabel", "Viewport") :
		LOCTEXT("2DViewportOverlayLabel", "Facial Tracing");
}

const FSlateBrush* SMetaHumanCharacterEditorViewport::Get2DViewportModeOverlayImageBrush() const
{
	return 
		TrackerImageViewer.IsValid() && TrackerImageViewer->GetVisibility() == EVisibility::Visible ? 
		CurrentViewportBrush.Get() : 
		TrackerImageViewerBrush.Get();
}

EVisibility SMetaHumanCharacterEditorViewport::Get2DViewportModeOverlayVisibility() const
{
	const bool bIsVisible = 
		bIs2DViewOverlayEnabled &&
		TrackerImageViewer.IsValid() && 
		TrackerImageViewer->ContainsData();

	return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SMetaHumanCharacterEditorViewport::On2DViewportModeOverlayClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (TrackerImageViewer.IsValid())
	{
		const EVisibility NewVisibility = TrackerImageViewer->GetVisibility() == EVisibility::Visible ? EVisibility::Hidden : EVisibility::Visible;
		TrackerImageViewer->SetVisibility(NewVisibility);

		const bool bIsTrackerVisible = NewVisibility == EVisibility::Visible;
		Update2DViewportOverlay(bIsTrackerVisible);
		OnTrackerImageVisibilityChangedDelegate.ExecuteIfBound(bIsTrackerVisible);
	}

	return FReply::Handled();
}

void SMetaHumanCharacterEditorViewport::FocusOnFrame(EMetaHumanCharacterCameraFrame FrameToFocus)
{
	GetMetaHumanCharacterEditorViewportClient()->FocusOnSelectedFrame(FrameToFocus, /*bInRotate*/ true);
}

bool SMetaHumanCharacterEditorViewport::IsCameraFrameChecked(EMetaHumanCharacterCameraFrame Frame) const
{
	if (UMetaHumanCharacter* Character = GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
	{
		return Character->ViewportSettings.CameraFrame == Frame;
	}

	return false;
}

bool SMetaHumanCharacterEditorViewport::ShouldDrawTrackingPoints() const
{
	return OnShouldDrawTrackingPointsDelegate.IsBound() && OnShouldDrawTrackingPointsDelegate.Execute();
}

bool SMetaHumanCharacterEditorViewport::IsViewportModeVisible() const
{
	return IsViewportModeVisibleAttribute.IsSet() && IsViewportModeVisibleAttribute.IsBound() && IsViewportModeVisibleAttribute.Get();
}

#undef LOCTEXT_NAMESPACE
