// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorMeshTargetContourMechanic.h"

#include "InteractiveToolManager.h"
#include "LandmarkConfigIdentityHelper.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorLandmarkTracker.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorMeshImportTargetScene.h"
#include "MetaHumanCharacterEditorMeshImportTool.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "MetaHumanIdentityPose.h"
#include "Editor.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterMeshTargetContourMechanic"

namespace UE::MetaHuman
{
	static TAutoConsoleVariable<bool> CVarMHSaveFaceTrackingPNG(
	TEXT("mh.Character.SaveFaceTrackingPNG"),
	false,
	TEXT("If true, saves captured face tracking images as PNG files to Project/Saved/FaceTracking/\n")
	TEXT("Usage: mh.Character.SaveFaceTrackingPNG 1 (enable) or 0 (disable)"),
	ECVF_Default);
	
	void SaveTrackingImageIfEnabled(const TArray<FColor>& InTrackingImage, const FIntPoint& InImageSize)
	{
		if (!InTrackingImage.IsEmpty() && CVarMHSaveFaceTrackingPNG.GetValueOnAnyThread())
		{
			// Save as PNG for debugging
			FString SavePath = FPaths::ProjectSavedDir() / TEXT("FaceTracking");
			IFileManager::Get().MakeDirectory(*SavePath, true);
			FString FileName = FString::Printf(TEXT("Capture_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
			FString FullPath = SavePath / FileName;

			// Convert FColor array to PNG
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

			if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(InTrackingImage.GetData(), InTrackingImage.Num() * sizeof(FColor), InImageSize.X, InImageSize.Y, ERGBFormat::BGRA, 8))
			{
				const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(100);
				if (FFileHelper::SaveArrayToFile(CompressedData, *FullPath))
				{
					UE_LOGF(LogMetaHumanCharacterEditor, Log, "Saved tracking capture PNG to: %ls", *FullPath);
				}
				else
				{
					UE_LOGF(LogMetaHumanCharacterEditor, Error, "Failed to save PNG file: %ls", *FullPath);
				}
			}
			else
			{
				UE_LOGF(LogMetaHumanCharacterEditor, Error, "Failed to create PNG image wrapper");
			}
		}	
	}
	
	bool GetTrackingResultsFromCharacter(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey, FMetaHumanCharacterTargetTrackingResults& OutTrackingResults)
	{
		if (!InCharacter)
		{
			return false;
		}

		if (const FMetaHumanCharacterTargetTrackingResults* TrackingResults = InCharacter->TargetMeshTrackingResultsCollection.PerMeshTrackingResults.Find(InTargetMeshKey))
		{
			OutTrackingResults = *TrackingResults;
			return true;
		}

		return false;
	}
}


void UMeshTargetContourMechanic::Setup(UInteractiveTool* InParentTool)
{
	Super::Setup(InParentTool);
	
	if (UInteractiveToolManager* ToolManager = ParentTool->GetToolManager())
	{
		if (IToolsContextQueriesAPI* QueriesAPI = ToolManager->GetContextQueriesAPI())
		{
			TargetWorld = QueriesAPI->GetCurrentEditingWorld();
		}
	}

	if (TargetWorld)
	{
		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);

		const FIntPoint ImageSize = GetImageSize();
		RenderTarget->InitAutoFormat(ImageSize.X, ImageSize.Y);
		RenderTarget->UpdateResourceImmediate(false);

		SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
		SceneCaptureComponent->TextureTarget = RenderTarget;
		SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
		SceneCaptureComponent->bCaptureEveryFrame = false;
		SceneCaptureComponent->bCaptureOnMovement = false;
		SceneCaptureComponent->bAlwaysPersistRenderingState = true;
		
		SceneCaptureComponent->RegisterComponentWithWorld(TargetWorld);
	}
	
	LandmarkTracker = NewObject<UMetaHumanCharacterEditorLandmarkTracker>(this);
	LandmarkTracker->LoadTrackers();
	
	if (TargetWorld)
	{
		SplinesActor = TargetWorld->SpawnActor<AInternalToolFrameworkActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	}

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
}

void UMeshTargetContourMechanic::Shutdown()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	Super::Shutdown();

	if (SceneCaptureComponent)
	{
		SceneCaptureComponent->DestroyComponent();
		SceneCaptureComponent = nullptr;
	}
	
	if (SplinesActor != nullptr)
	{
		SplinesActor->Destroy();
		SplinesActor = nullptr;
	}
}

void UMeshTargetContourMechanic::Initialize(TObjectPtr<UMetaHumanCharacter> InMetaHumanCharacter,
	TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> InMeshTargetScene,
	const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey)
{
	Clear();
	MetaHumanCharacter = InMetaHumanCharacter;
	MeshTargetScene = InMeshTargetScene;
	TargetMeshKey = InTargetMeshKey;

	// Restore tracking results from the character if available
	FMetaHumanCharacterTargetTrackingResults TrackingResults;
	if (UE::MetaHuman::GetTrackingResultsFromCharacter(MetaHumanCharacter, InTargetMeshKey, TrackingResults))
	{
		if (TrackingResults.CurveTrackingPoints.Num() > 0)
		{
			CapturedViewInfo = TrackingResults.CameraViewInfo;

			TMap<FString, TArray<FVector2D>> RestoredContours;
			FFrameTrackingContourData TrackingContourData;

			for (const TPair<FString, FMetaHumanCharacterCurveTrackingPoints>& Pair : TrackingResults.CurveTrackingPoints)
			{
				const FString& CurveName = Pair.Key;
				const TArray<FVector2D>& Points = Pair.Value.Points;

				RestoredContours.Add(CurveName, Points);

				FTrackingContour Contour;
				Contour.DensePoints = Points;
				Contour.DensePointsConfidence.Init(1.0f, Points.Num());
				TrackingContourData.TrackingContours.Add(CurveName, Contour);
			}

			TrackingContours = RestoredContours;
			RestoredTrackerImageSize = FIntPoint(DefaultTrackingImageWidth, DefaultTrackingImageHeight);
				
			// Restore the tracked image as a UTexture2D for the image viewer
			if (TrackingResults.TrackedImage.Num() > 0)
			{
				RestoredTrackerImageSize = TrackingResults.ImageSize;
				const int32 Width = RestoredTrackerImageSize.X;
				const int32 Height = RestoredTrackerImageSize.Y;

				RestoredTrackerImage = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
				if (RestoredTrackerImage)
				{
					void* MipData = RestoredTrackerImage->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
					FMemory::Memcpy(MipData, TrackingResults.TrackedImage.GetData(), Width * Height * sizeof(FColor));
					RestoredTrackerImage->GetPlatformData()->Mips[0].BulkData.Unlock();
					RestoredTrackerImage->UpdateResource();
				}
			}
			
			LandmarkTracker->InitializeContourData(RestoredTrackerImageSize.X, RestoredTrackerImageSize.Y);
			LandmarkTracker->GetCurveDataController()->UpdateFromContourData(TrackingContourData, true);

			// Restore the exact control vertex positions that were fitted at track time, overwriting
			// the re-fitted result from UpdateFromContourData. Without this, the lossy shape-annotation
			// fitting pass produces slightly different control vertices on reload, causing curve ends
			// to shift. Only applies to curves that were saved with control vertex data (new assets);
			// older assets fall through to the re-fitted result unchanged.
			if (!TrackingResults.ControlVertexPoints.IsEmpty())
			{
				if (TSharedPtr<FMetaHumanCurveDataController> Controller = LandmarkTracker->GetCurveDataController())
				{
					if (UMetaHumanContourData* ContourData = Controller->GetContourData())
					{
						for (TPair<FString, FReducedContour>& Pair : ContourData->ReducedContourData)
						{
								if (const FMetaHumanCharacterCurveTrackingPoints* SavedPoints = TrackingResults.ControlVertexPoints.Find(Pair.Key))
							{
									if (SavedPoints->Points.Num() == Pair.Value.ControlVertices.Num())
								{
									for (int32 i = 0; i < Pair.Value.ControlVertices.Num(); ++i)
									{
											Pair.Value.ControlVertices[i].PointPosition = SavedPoints->Points[i];
									}
								}
							}
						}
						Controller->GenerateCurvesFromControlVertices();
						Controller->GenerateDrawDataForDensePoints();
					}
				}
			}

			SubscribeToCurveDataController();

			ProjectTo3DPoints();
		}
	}
}

TSharedPtr<FMetaHumanCurveDataController> UMeshTargetContourMechanic::GetCurveDataController() const
{
	return LandmarkTracker ? LandmarkTracker->GetCurveDataController() : nullptr;
}

void UMeshTargetContourMechanic::SubscribeToCurveDataController()
{
	if (TSharedPtr<FMetaHumanCurveDataController> Controller = GetCurveDataController())
	{
		// Remove any existing subscription first to avoid double-subscribing
		Controller->TriggerContourUpdate().RemoveAll(this);
		Controller->TriggerContourUpdate().AddUObject(this, &UMeshTargetContourMechanic::OnCurveDataControllerUpdated);
	}
}

void UMeshTargetContourMechanic::OnCurveDataControllerUpdated()
{
	if (bIsTracking)
	{
		return;
	}

	if (bIsApplyingUndo)
	{
		return;
	}

	// Extract updated curves from the controller and commit to character
	if (!MetaHumanCharacter)
	{
		return;
	}

	const TSharedPtr<FMetaHumanCurveDataController> Controller = GetCurveDataController();
	if (!Controller)
	{
		return;
	}

	// Get current tracking contours from the controller
	TrackingContours = Controller->GetDensePointsForVisibleCurves();

	// Build and commit tracking results — reuse stored camera info from last track
	FMetaHumanCharacterTargetTrackingResults TrackingResults;
	if (UE::MetaHuman::GetTrackingResultsFromCharacter(MetaHumanCharacter, TargetMeshKey, TrackingResults))
	{
		for (const TPair<FString, TArray<FVector2D>>& Pair : TrackingContours)
		{
			FMetaHumanCharacterCurveTrackingPoints CurvePoints;
			CurvePoints.Points = Pair.Value;
			TrackingResults.CurveTrackingPoints.Add(Pair.Key, CurvePoints);
		}

		TrackingResults.ControlVertexPoints.Empty();
		if (const UMetaHumanContourData* ContourData = Controller->GetContourData())
		{
			for (const TPair<FString, FReducedContour>& Pair : ContourData->ReducedContourData)
			{
				FMetaHumanCharacterCurveTrackingPoints VertexPoints;
				for (const FControlVertex& Vertex : Pair.Value.ControlVertices)
				{
					VertexPoints.Points.Add(Vertex.PointPosition);
				}
				TrackingResults.ControlVertexPoints.Add(Pair.Key, VertexPoints);
			}
		}

		UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshTrackingResults(MetaHumanCharacter, TargetMeshKey, TrackingResults);
		ProjectTo3DPoints();
	}
	else
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Error commiting curves to character");
	}
}

UTexture* UMeshTargetContourMechanic::GetTrackingImageTexture() const
{
	if (RestoredTrackerImage)
	{
		return RestoredTrackerImage;
	}
	
	return SceneCaptureComponent ? SceneCaptureComponent->TextureTarget : nullptr;
}

FIntPoint UMeshTargetContourMechanic::GetTrackingImageSize() const
{
	FIntPoint OutImageSize = FIntPoint::ZeroValue;
	if (RestoredTrackerImage)
	{
		OutImageSize = RestoredTrackerImageSize;
	}
	else if (SceneCaptureComponent && SceneCaptureComponent->TextureTarget)
	{
		OutImageSize = FIntPoint(SceneCaptureComponent->TextureTarget->SizeX, SceneCaptureComponent->TextureTarget->SizeY);
	}
	
	return OutImageSize;
}

bool UMeshTargetContourMechanic::TrackFaceInCurrentView(const FVector& InMeshOffset)
{
	FViewCameraState ViewState;
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(ViewState);

	if (!SceneCaptureComponent)
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Error capturing scene to track face landmarks");
		return false;
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
	
	return TrackFaceFromSceneComponent(InMeshOffset);
}

bool UMeshTargetContourMechanic::TrackFaceWithAutoFraming(const FVector& InMeshOffset)
{
	if (!SceneCaptureComponent)
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Error capturing scene to track face landmarks");
		return false;
	}

	FBox HeadBounds = CalculateHeadBounds();
	if (!HeadBounds.IsValid)
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Failed to calculate head bounds for auto-framing");
		return false;
	}

	//TODO: Test around a bit more with magic values, maybe think about storing them/exposing

	// Position camera to frame the head
	FVector HeadCenter = HeadBounds.GetCenter();
	FVector HeadExtent = HeadBounds.GetExtent();
	float HeadSize = HeadExtent.Size();

	if(HeadSize == 0.f)
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Degenerate bounding box for head.");
		return false;
	}

	// 1.8 value determines how far away from head center we place the camera
	float CameraDistance = HeadSize * 1.8f;
	// 0.2 value here is for raising the camera slightly for better result ( with combined meshes we usually won't get just head but also parts of the upper torso as well )
	FVector CameraOffset = FVector(0, CameraDistance, HeadSize * 0.2f);
	FVector CameraPosition = HeadCenter + CameraOffset;
	FRotator CameraRotation = (HeadCenter - CameraPosition).Rotation();

	// Fill Ratio controls how much of the frame the head fills, larger values mean the head will be smaller in frame and 2.f value means head will take around half of the frame
	const float TargetFillRatio = 2.f;
	float FOVRadians = 2.0f * FMath::Atan(HeadSize / (CameraDistance * TargetFillRatio));
	// -35.0f: Minimum FOV in degrees - prevents too narrow view
	// 110.0f: Maximum FOV in degrees - prevents too wide distortion
	float FOVDegrees = FMath::Clamp(FMath::RadiansToDegrees(FOVRadians), 35.0f, 110.0f);

	SceneCaptureComponent->SetWorldLocation(CameraPosition);
	SceneCaptureComponent->SetWorldRotation(CameraRotation);
	SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Perspective;
	SceneCaptureComponent->FOVAngle = FOVDegrees;

	return TrackFaceFromSceneComponent(InMeshOffset);
}

bool UMeshTargetContourMechanic::HasTrackingResults() const
{
	return !TrackingContours.IsEmpty();
}

bool UMeshTargetContourMechanic::TrackFaceFromSceneComponent(const FVector& InMeshOffset)
{
	// Clear restored image so GetTrackerTexture returns the live render target
	RestoredTrackerImage = nullptr;
	RestoredTrackerImageSize = FIntPoint::ZeroValue;
	
	const FIntPoint ImageSize = GetImageSize();
	UTextureRenderTarget2D* RenderTarget = SceneCaptureComponent->TextureTarget;
	if (RenderTarget && ImageSize.X > 0 && ImageSize.Y > 0)
	{
		RenderTarget->InitAutoFormat(ImageSize.X, ImageSize.Y);
		RenderTarget->UpdateResourceImmediate(false);
	}
	
	// Hide character from scene capture
	TScriptInterface<IMetaHumanCharacterEditorActorInterface> MetaHumanCharacterActor = nullptr;
	if (const TSharedRef<FMetaHumanCharacterEditorData>* EditorData = UMetaHumanCharacterEditorSubsystem::Get()->GetMetaHumanCharacterEditorData(MetaHumanCharacter))
	{
		for (TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> CharacterActor : (*EditorData)->CharacterActorList)
		{
			if (CharacterActor.IsValid())
			{
				MetaHumanCharacterActor = CharacterActor.ToScriptInterface();
			}
		}
	}

	if (MetaHumanCharacterActor)
	{
		const USkeletalMeshComponent* FaceSkelMeshComponent = MetaHumanCharacterActor->GetFaceComponent();
		const USkeletalMeshComponent* BodySkelMeshComponent = MetaHumanCharacterActor->GetBodyComponent();

		SceneCaptureComponent->HideComponent(const_cast<USkeletalMeshComponent*>(FaceSkelMeshComponent));
		SceneCaptureComponent->HideComponent(const_cast<USkeletalMeshComponent*>(BodySkelMeshComponent));
		MetaHumanCharacterActor->SetHairVisibilityState(EMetaHumanHairVisibilityState::Hidden);
		MetaHumanCharacterActor->SetClothingVisibilityState(EMetaHumanClothingVisibilityState::Hidden);
	}

	SceneCaptureComponent->CaptureScene();

	if (MetaHumanCharacterActor)
	{
		MetaHumanCharacterActor->SetHairVisibilityState(EMetaHumanHairVisibilityState::Shown);
		MetaHumanCharacterActor->SetClothingVisibilityState(EMetaHumanClothingVisibilityState::Shown);
	}

	// Read render target and track landmarks
	TArray<FColor> ImageToTrack;
	if (!UKismetRenderingLibrary::ReadRenderTarget(SceneCaptureComponent, SceneCaptureComponent->TextureTarget, ImageToTrack))
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Error capturing scene to track face landmarks");
		return false;
	}
	
	UE::MetaHuman::SaveTrackingImageIfEnabled(ImageToTrack, ImageSize);

	// Track facial landmarks
	FFrameTrackingContourData TrackingContourData = LandmarkTracker->TrackImage(ImageToTrack, ImageSize.X, ImageSize.Y);
	SubscribeToCurveDataController();

	if (!TrackingContourData.ContainsData())
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "Failed to track face landmarks from auto-framed image");
		return false;
	}

	bIsTracking = true;
	TrackingContours = LandmarkTracker->GetTrackingContours(TrackingContourData);
	bIsTracking = false;

	// Store camera info for conform
	FMinimalViewInfo ViewInfo;
	SceneCaptureComponent->GetCameraView(0.0f, ViewInfo);
	const float Aspect = (float)SceneCaptureComponent->TextureTarget->SizeX / (float)SceneCaptureComponent->TextureTarget->SizeY;
	ViewInfo.AspectRatio = Aspect;
	CapturedViewInfo = ViewInfo;
	CapturedMeshOffset = InMeshOffset;
	
	ProjectTo3DPoints();

	// Commit tracking results to the character
	FMetaHumanCharacterTargetTrackingResults TrackingResults;
	TrackingResults.CameraViewInfo = CapturedViewInfo;
	TrackingResults.CameraViewInfo.Location = TrackingResults.CameraViewInfo.Location - CapturedMeshOffset;
	TrackingResults.ImageSize = ImageSize;
	TrackingResults.TrackedImage = ImageToTrack;
	for (const TPair<FString, FTrackingContour>& Pair : TrackingContourData.TrackingContours)
	{
		FMetaHumanCharacterCurveTrackingPoints CurvePoints;
		CurvePoints.Points = Pair.Value.DensePoints;
		TrackingResults.CurveTrackingPoints.Add(Pair.Key, CurvePoints);
	}
	
	for (const TPair<FString, TArray<FVector2D>>& Pair : TrackingContours)
	{
		FMetaHumanCharacterCurveTrackingPoints CurvePoints;
		CurvePoints.Points = Pair.Value;
		TrackingResults.CurveTrackingPoints.Add(Pair.Key, CurvePoints);
	}

	// Save the fitted control vertex positions so Initialize can restore ReducedContourData exactly,
	// bypassing the lossy shape-annotation re-fit that causes curve ends to shift on reload.
	if (const TSharedPtr<FMetaHumanCurveDataController> Controller = GetCurveDataController())
	{
		if (const UMetaHumanContourData* ContourData = Controller->GetContourData())
		{
			for (const TPair<FString, FReducedContour>& Pair : ContourData->ReducedContourData)
			{
				FMetaHumanCharacterCurveTrackingPoints VertexPoints;
				for (const FControlVertex& Vertex : Pair.Value.ControlVertices)
				{
					VertexPoints.Points.Add(Vertex.PointPosition);
				}
				TrackingResults.ControlVertexPoints.Add(Pair.Key, VertexPoints);
			}
		}
	}

	UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshTrackingResults(MetaHumanCharacter, TargetMeshKey, TrackingResults);

	return true;
}

FIntPoint UMeshTargetContourMechanic::GetImageSize() const
{
	FIntPoint ImageSize = FIntPoint(DefaultTrackingImageWidth, DefaultTrackingImageHeight);
	const UMetaHumanCharacterEditorMeshImportTool* OwnerTool = Cast< UMetaHumanCharacterEditorMeshImportTool>(ParentTool.Get());
	if (OwnerTool && OwnerTool->OnGetTrackerImageSizeDelegate.IsBound())
	{
		FVector2D ViewportSize = OwnerTool->OnGetTrackerImageSizeDelegate.Execute();
		ImageSize = {static_cast<int32>(ViewportSize.X), static_cast<int32>(ViewportSize.Y)};
	}

	return ImageSize;
}

void UMeshTargetContourMechanic::ProjectTo3DPoints()
{
	FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
	UGameplayStatics::GetViewProjectionMatrix(CapturedViewInfo, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);
	FMatrix InvViewProjMatrix = ViewProjectionMatrix.Inverse();

	const FIntPoint ImageSize = RestoredTrackerImage ? RestoredTrackerImageSize : GetImageSize();
	
	for (TPair<FString, TArray<FVector2D>> TrackingContour : TrackingContours)
	{
		const FString& CurveName = TrackingContour.Key;
		const TArray<FVector2D>& DensePoints = TrackingContour.Value;
		
		TArray<FVector> TrackingContour3D;
		for (const FVector2D& ContourPoint : DensePoints)
		{
			FVector Origin, Dir;
			{
				const float NormalizedX = ContourPoint.X / static_cast<float>(ImageSize.X);
				const float NormalizedY = ContourPoint.Y / static_cast<float>(ImageSize.Y);
				const float ScreenSpaceX = (NormalizedX - 0.5f) * 2.0f;
				const float ScreenSpaceY = ((1.0f - NormalizedY) - 0.5f) * 2.0f;
				const FVector4 RayStartProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 1.0f, 1.0f);
				const FVector4 RayEndProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 0.01f, 1.0f);
				const FVector4 HGRayStartWorldSpace = InvViewProjMatrix.TransformFVector4(RayStartProjectionSpace);
				const FVector4 HGRayEndWorldSpace = InvViewProjMatrix.TransformFVector4(RayEndProjectionSpace);
				FVector RayStartWorldSpace(HGRayStartWorldSpace.X, HGRayStartWorldSpace.Y, HGRayStartWorldSpace.Z);
				FVector RayEndWorldSpace(HGRayEndWorldSpace.X, HGRayEndWorldSpace.Y, HGRayEndWorldSpace.Z);
				if (HGRayStartWorldSpace.W != 0.0f)
				{
					RayStartWorldSpace /= HGRayStartWorldSpace.W;
				}
				if (HGRayEndWorldSpace.W != 0.0f)
				{
					RayEndWorldSpace /= HGRayEndWorldSpace.W;
				}
				const FVector RayDirWorldSpace = (RayEndWorldSpace - RayStartWorldSpace).GetSafeNormal();
				Origin = RayStartWorldSpace;
				Dir = RayDirWorldSpace;
			}
			
			const FVector ViewDir    = CapturedViewInfo.Rotation.Vector();
			const FVector PlanePoint = CapturedViewInfo.Location + ViewDir * 10.f;
			FPlane DrawingPlane(PlanePoint, ViewDir);
			
			const FVector WorldPointPlane = FMath::RayPlaneIntersection(Origin, Dir, DrawingPlane);
			FVector ComponentPos = TargetComponentTransform.Inverse().TransformPosition(WorldPointPlane);
			TrackingContour3D.Add(ComponentPos);
		}

		if (!TrackingContour3D.IsEmpty() && SplinesActor)
		{
			TrackingContours3D.Add(CurveName, TrackingContour3D);		

			TObjectPtr<USplineComponent> SplineComponent = NewObject<USplineComponent>(SplinesActor);
			SplineComponent->SetupAttachment(SplinesActor->GetRootComponent());
			SplineComponent->RegisterComponent();
		
			SplineComponent->ClearSplinePoints(false);
		
			TArray<FVector> WorldPoints;
			WorldPoints.SetNumUninitialized(TrackingContour3D.Num());
			for (int i = 0; i < TrackingContour3D.Num(); i++)
			{
				WorldPoints[i] = TargetComponentTransform.TransformPosition(TrackingContour3D[i]);
			}			
		
			SplineComponent->SetSplinePoints(WorldPoints, ESplineCoordinateSpace::World);
			SplineComponent->UpdateSpline();
			ContourSplines.Add(CurveName, SplineComponent);
		}
	}
}

void UMeshTargetContourMechanic::DeleteFaceCurves()
{
	Clear();

	if (MetaHumanCharacter)
	{
		FMetaHumanCharacterTargetTrackingResults EmptyTrackingResults;
		UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshTrackingResults(MetaHumanCharacter, TargetMeshKey, EmptyTrackingResults);
	}
}

void UMeshTargetContourMechanic::Clear()
{
	RestoredTrackerImage = nullptr;
	RestoredTrackerImageSize = FIntPoint::ZeroValue;
	if (LandmarkTracker && LandmarkTracker->GetCurveDataController())
	{
		LandmarkTracker->GetCurveDataController()->ClearContourData();
	}
	TrackingContours.Empty();
	TrackingContours3D.Empty();
	for (TPair<FString, TObjectPtr<USplineComponent>>& ContourSpline: ContourSplines)
	{
		USplineComponent* Spline = ContourSpline.Value;
		if (Spline)
		{
			Spline->UnregisterComponent();
			Spline->DestroyComponent();
		}
	}
	ContourSplines.Empty();
}

void UMeshTargetContourMechanic::UpdateComponentTransform()
{
	if (MeshTargetScene)
	{
		TargetComponentTransform = MeshTargetScene->IsUsingCharacterParts() ? MeshTargetScene->GetHeadComponentTransform() : MeshTargetScene->GetBodyComponentTransform();	
	}
	
	for (const TPair<FString, TArray<FVector>>& TrackingContour3D : TrackingContours3D)
	{
		const FString& CurveName = TrackingContour3D.Key;
		if (ContourSplines.Contains(CurveName))
		{
			TArray<FVector> WorldPoints;
			WorldPoints.SetNumUninitialized(TrackingContour3D.Value.Num());
			for (int i = 0; i < TrackingContour3D.Value.Num(); i++)
			{
				WorldPoints[i] = TargetComponentTransform.TransformPosition(TrackingContour3D.Value[i]);
			}			
			
			USplineComponent* SplineComponent = ContourSplines[CurveName];
			SplineComponent->SetSplinePoints(WorldPoints, ESplineCoordinateSpace::World);
			SplineComponent->UpdateSpline();
		}
	}
}

void UMeshTargetContourMechanic::Render(IToolsContextRenderAPI* InRenderAPI)
{
	Super::Render(InRenderAPI);

	// Check camera moved
	FViewCameraState ViewCameraState = InRenderAPI->GetCameraState();
	if (!CapturedViewInfo.Location.Equals(ViewCameraState.Position))
	{
		return;
	}

	if(!CapturedViewInfo.Rotation.Quaternion().Equals(ViewCameraState.Orientation))
	{
		return;
	}

	if (FPrimitiveDrawInterface* PDI = InRenderAPI->GetPrimitiveDrawInterface())
	{
		bool bRender3DSplines = true;
		if (bRender3DSplines)
		{
			for (const TPair<FString, TObjectPtr<USplineComponent>>& ContourSpline: ContourSplines)
			{
				int32 NumPoints = ContourSpline.Value->GetNumberOfSplinePoints();
				if (NumPoints > 1)
				{
					// Draw as polyline samples along the spline (more stable than point-to-point for curves).
					// Increase samples for smoother curves.
					constexpr int32 SamplesPerSegment = 16;

					const float SplineLen = ContourSpline.Value->GetSplineLength();
					if (SplineLen <= KINDA_SMALL_NUMBER)
						continue;

					const int32 TotalSamples = FMath::Max(2, (NumPoints - 1) * SamplesPerSegment + 1);

					FVector Prev = ContourSpline.Value->GetLocationAtDistanceAlongSpline(0.f, ESplineCoordinateSpace::World);
			
					for (int32 i = 1; i < TotalSamples; ++i)
					{
						const float T = (float)i / (float)(TotalSamples - 1);
						const float D = T * SplineLen;

						const FVector Curr = ContourSpline.Value->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);

						// Thickness is in screen space for PDI lines; keep it modest.
						PDI->DrawLine(Prev, Curr, FLinearColor::Yellow, SDPG_Foreground, /*Thickness=*/0.0f);

						Prev = Curr;
					}
				}
			}
		}
	}
}

FBox UMeshTargetContourMechanic::CalculateHeadBounds() const
{
	if (!MeshTargetScene)
	{
		UE_LOGF(LogMetaHumanCharacterEditor, Error, "MeshTargetScene is invalid!");
		return FBox(ForceInit);
	}

	bool bUsingParts = MeshTargetScene->IsUsingCharacterParts();

	// If using separate body and head, just get the head mesh
	if (bUsingParts)
	{
		UDynamicMeshComponent* HeadComponent = MeshTargetScene->GetHeadDynamicMeshComponent();

		if (!HeadComponent)
		{
			UE_LOGF(LogMetaHumanCharacterEditor, Error, "HeadDynamicMeshComponent needed for calculating head bounds is invalid!");
			return FBox(ForceInit);
		}

		FBoxSphereBounds HeadBounds = HeadComponent->Bounds;

		FBox ResultBox = HeadBounds.GetBox();

		return ResultBox;
	}
	else
	{
		// If using combined mesh, we have to calculate where the head is and we get body mesh only ( which is combined mesh ) 
		UDynamicMeshComponent* BodyComponent = MeshTargetScene->GetBodyDynamicMeshComponent();

		if (!BodyComponent)
		{
			UE_LOGF(LogMetaHumanCharacterEditor, Error, "BodyDynamicMeshComponent needed for calculating head bounds is invalid!");
			return FBox(ForceInit);
		}

		FBox CombinedMeshBounds = BodyComponent->Bounds.GetBox();

		if (!CombinedMeshBounds.IsValid)
		{
			UE_LOGF(LogMetaHumanCharacterEditor, Error, "Getting combined mesh bounds failed!");
			return FBox(ForceInit);
		}

		float MeshHeight = CombinedMeshBounds.Max.Z - CombinedMeshBounds.Min.Z;
		// We get approximate here that the head is about 15% of the combined mesh
		float HeadStartZ = CombinedMeshBounds.Max.Z - (MeshHeight * 0.15f);

		FVector HeadMin = FVector( CombinedMeshBounds.Min.X, CombinedMeshBounds.Min.Y, HeadStartZ);
		FVector HeadMax = CombinedMeshBounds.Max;

		FBox HeadBounds(HeadMin, HeadMax);

		FVector HeadCenter = HeadBounds.GetCenter();
		FVector HeadExtent = HeadBounds.GetExtent();

		// Tightening the head bound box horizontally because of torso or shoulders 
		HeadExtent.X *= 0.6f;
		HeadExtent.Y *= 0.6f;

		// Tightening the bounds for the head
		HeadBounds = FBox(HeadCenter - HeadExtent, HeadCenter + HeadExtent);

		return HeadBounds;
	}
}

void UMeshTargetContourMechanic::PostUndo(bool bSuccess)
{
	RebuildAfterUndo();
}

void UMeshTargetContourMechanic::PostRedo(bool bSuccess)
{
	RebuildAfterUndo();
}

bool UMeshTargetContourMechanic::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	/* Filter so PostUndo / PostRedo only fire when the transaction actually touches our owning
	 character asset or our UMetaHumanContourData (which is outered to our LandmarkTracker).
	Without this, every unrelated undo in the editor would trigger a viewport rebuild. */
	for (const TPair<UObject*, FTransactionObjectEvent>& Pair : TransactionObjectContexts)
	{
		UObject* Object = Pair.Key;
		if (!Object)
		{
			continue;
		}

		if (Object == MetaHumanCharacter)
		{
			return true;
		}

		if (LandmarkTracker.Get() && Object->GetOuter() == LandmarkTracker.Get())
		{
			return true;
		}
	}

	return false;
}

void UMeshTargetContourMechanic::BeginDestroy()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}

	Super::BeginDestroy();
}

void UMeshTargetContourMechanic::RebuildAfterUndo()
{
	TGuardValue<bool> ApplyUndoGuard(bIsApplyingUndo, true);

	const TSharedPtr<FMetaHumanCurveDataController> Controller = GetCurveDataController();
	if (Controller.IsValid())
	{
		// Rebuild control vertices, reduced curves, and draw-data caches from the restored ContourData.
		Controller->HandleUndoOperation();

		// Pull the restored 2D contours back into our local map.
		TrackingContours = Controller->GetDensePointsForVisibleCurves();
	}
	else
	{
		TrackingContours.Empty();
	}

	if (MetaHumanCharacter)
	{
		FMetaHumanCharacterTargetTrackingResults TrackingResults;
		if (UE::MetaHuman::GetTrackingResultsFromCharacter(MetaHumanCharacter, TargetMeshKey, TrackingResults))
		{
			// Preserve non-curve state, refresh just the curve points.
			for (const TPair<FString, TArray<FVector2D>>& Pair : TrackingContours)
			{
				FMetaHumanCharacterCurveTrackingPoints CurvePoints;
				CurvePoints.Points = Pair.Value;
				TrackingResults.CurveTrackingPoints.Add(Pair.Key, CurvePoints);
			}

			UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshTrackingResults(MetaHumanCharacter, TargetMeshKey, TrackingResults);

			// Restore CapturedViewInfo from the (possibly-also-restored) TrackFace data.
			CapturedViewInfo = TrackingResults.CameraViewInfo;
		}
	}

	// Tear down stale 3D spline viz and reproject from the restored 2D contours.
	TrackingContours3D.Empty();
	for (TPair<FString, TObjectPtr<USplineComponent>>& Pair : ContourSplines)
	{
		if (USplineComponent* Spline = Pair.Value)
		{
			Spline->UnregisterComponent();
			Spline->DestroyComponent();
		}
	}
	ContourSplines.Empty();

	ProjectTo3DPoints();
	UpdateComponentTransform();
}

#undef LOCTEXT_NAMESPACE
