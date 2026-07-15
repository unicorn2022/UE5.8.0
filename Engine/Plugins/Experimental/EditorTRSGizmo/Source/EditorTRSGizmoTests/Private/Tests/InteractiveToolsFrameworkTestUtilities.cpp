// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/InteractiveToolsFrameworkTestUtilities.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "Tests/InteractiveToolsFrameworkTestUtilities.inl"

#include "Components/StaticMeshComponent.h"
#include "EditorModeTools.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Engine/StaticMeshActor.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SLevelViewport.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/Docking/SDockTab.h"

namespace UE::Editor::InteractiveToolsFramework::Tests
{
	void FTestWorld::Initialize(const TSharedRef<FEditorProvider>& InEditorProvider)
	{
		InitializeWithMap(FString(), InEditorProvider);
	}

	void FTestWorld::InitializeWithMap(const FString& InMapName, const TSharedRef<FEditorProvider>& InEditorProvider)
	{
		EditorProvider = InEditorProvider;

		World = InMapName.IsEmpty() 
			? FAutomationEditorCommonUtils::CreateNewMap() 
			: UEditorLoadingAndSavingUtils::LoadMap(InMapName);

		ensureAlways(World.IsValid());

		ActorSelectionSet = InEditorProvider->GetActorSelectionSet();

		ensureAlways(EditorProvider->GetEditorViewportClient() && EditorProvider->GetEditorViewportClient()->GetWorld() == World.Get());
	}

	void FTestWorld::EmptyWorld()
	{
		if (!World.IsValid())
		{
			return;
		}

		for (const TPair<FName, TWeakObjectPtr<AActor>>& NamedActor : Actors)
		{
			if (!NamedActor.Value.IsValid())
			{
				continue;
			}

			World->DestroyActor(NamedActor.Value.Get());
		}

		CollectGarbage(EObjectFlags::RF_NoFlags);
	}

	void FTestWorld::Reset()
	{
		Actors.Reset();
		CapturedActorStates.Reset();
		World = nullptr;
		ActorSelectionSet = nullptr;
	}

	void FTestWorld::CaptureState()
	{
		CapturedTransactionId = GEditor->BeginTransaction(FText::FromString(TEXT("TransientTestState")));

		CapturedActorStates.Reserve(Actors.Num());
		for (const TPair<FName, TWeakObjectPtr<AActor>>& NamedActor : Actors)
		{
			if (!NamedActor.Value.IsValid())
			{
				continue;
			}

			CapturedActorStates.Emplace(NamedActor.Key, NamedActor.Value->GetTransform());
		}

		// This should always be valid here
		if (!ensure(GEditor->GetActiveViewport()))
		{
			return;
		}

		if (FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient()))
		{
			FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
			CapturedViewLocation = ViewTransform.GetLocation();
			CapturedViewRotation = ViewTransform.GetRotation();
		}
	}

	void FTestWorld::RestoreState()
	{
		if (CapturedTransactionId != INDEX_NONE)
		{
			GEditor->CancelTransaction(CapturedTransactionId);
			CapturedTransactionId = INDEX_NONE;
		}
		
		for (const TPair<FName, TWeakObjectPtr<AActor>>& NamedActor : Actors)
		{
			if (!NamedActor.Value.IsValid())
			{
				continue;
			}
				
			if (const FTransform* StoredTransform = CapturedActorStates.Find(NamedActor.Key))
			{
				NamedActor.Value->SetActorTransform(*StoredTransform);
			}
		}

		SetViewportCamera(CapturedViewLocation, CapturedViewRotation);
	}

	void FTestWorld::SetViewportCamera(const FVector& InLocation, const FRotator& InRotation)
	{
		if (!ensure(GEditor->GetActiveViewport()))
		{
			return;
		}

		if (FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient()))
		{
			FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
			ViewTransform.SetLocation(InLocation);
			ViewTransform.SetRotation(InRotation);	
		}
	}

	AStaticMeshActor* FTestWorld::SpawnCube(const FName InName, const FTransform& InTransform)
	{
		AStaticMeshActor* CubeActor = SpawnActor<AStaticMeshActor>(InName, InTransform);
		if (CubeActor)
		{
			constexpr const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
			UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
			CubeActor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
		}

		return CubeActor;
	}

	UWorld* FTestWorld::GetWorld() const
	{
		return World.Get();
	}

	void FTestWorld::SelectActor(const AActor* InActor) const
	{
		if (ActorSelectionSet && InActor)
		{
			const FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor);
			ActorSelectionSet->SelectElement(ActorElementHandle, { });
			ActorSelectionSet->NotifyPendingChanges();
		}
	}

	void FTestWorld::DeselectActor(const AActor* InActor) const
	{
		if (ActorSelectionSet && InActor)
		{
			const FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor);
			ActorSelectionSet->DeselectElement(ActorElementHandle, { });
			ActorSelectionSet->NotifyPendingChanges();
		}
	}

	void FTestWorld::ClearSelection() const
	{
		if (ActorSelectionSet)
		{
			ActorSelectionSet->ClearSelection({ });
			ActorSelectionSet->NotifyPendingChanges();
		}
	}

	bool FTestWorld::IsValid() const
	{
		// Probably not initialized if this isn't valid
		return World.IsValid();
	}

	FLocator::FLocator() = default;

	FLocator::FLocator(const TSharedRef<IElementLocator>& InElementLocator)
		: bUseElementLocator(true)
		, ElementLocator(InElementLocator)
	{
	}

	float FLocator::GetEstimatedTime() const
	{
		// Conservative estimate
		constexpr float FramesPerSecond = 30.0f;
		return Steps * (1.0f / FramesPerSecond); 
	}

	FLocator FLocator::Empty()
	{
		static FLocator EmptyLocator;
		return EmptyLocator;
	}

	FLocator FLocator::FromWorldPositionInViewport(const TSharedRef<IElementLocator>& InViewportLocator, const FSceneView* InSceneView, const FVector& InWorldPosition)
	{
		FLocator Locator(InViewportLocator);

		if (InSceneView)
		{
			FVector2D Position2D = FVector2D::ZeroVector;
			InSceneView->WorldToPixel(InWorldPosition, Position2D);

			Locator.Offset = FIntPoint(Position2D.X, Position2D.Y);	
		}

		return Locator;
	}

	FLocator FLocator::FromOffsetFunction(const FOffsetFunction& InOffsetFunction, const int32 InSteps)
	{
		FLocator Locator;
		Locator.Steps = InSteps;
		Locator.OffsetFunction = InOffsetFunction;

		return Locator;
	}

	FLocator FLocator::FromOffset(const TSharedRef<IElementLocator>& InElementLocator, const FIntPoint& InOffset, int32 InSteps)
	{
		FLocator Locator(InElementLocator);
		Locator.Steps = InSteps;
		Locator.Offset = InOffset;	

		return Locator;
	}

	FLocator FLocator::FromOffset(const FIntPoint& InOffset, int32 InSteps)
	{
		FLocator Locator;
		Locator.Steps = InSteps;
		Locator.Offset = InOffset;

		return Locator;
	}

	void FLocator::AppendToActions(IAsyncActionSequence& InActions) const
	{
		const int32 OffsetX = Offset.Get(0).X;
		const int32 OffsetY = Offset.Get(0).Y;

		if (bUseElementLocator)
		{
			InActions.MoveToElement(ElementLocator.ToSharedRef());
			if (Offset.IsSet())
			{
				// If there's an offset AND and an element, we offset relative to that element (moved to above)
				InActions.MoveByOffset(OffsetX, OffsetY);
			}
		}
		else if (Offset.IsSet())
		{
			if (Steps > 0)
			{
				FVector2D Direction = FVector2D(Offset.GetValue());
				const float Distance = Direction.Length();
				Direction.Normalize();

				const float StepDistance = Distance / FMath::Max(1, Steps);

				for (int32 StepIdx = 0; StepIdx < Steps; ++StepIdx)
				{
					const FVector2D Delta = Direction * StepDistance;
					InActions.MoveByOffset(Delta.X, Delta.Y);
				}
			}
			else
			{
				InActions.MoveByOffset(OffsetX, OffsetY);
			}
		}
		else if (OffsetFunction.IsSet())
		{
			// Jump to the first offset.
			FVector2D Last = OffsetFunction(0.0f);
			InActions.MoveByOffset(Last.X, Last.Y);
			
			const int32 NumSteps = FMath::Max(1, Steps - 1);
			for (int32 StepIdx = 1; StepIdx < Steps; ++StepIdx)
			{
				// Move incrementally to each next step
				const float Alpha = Steps > 1 ? static_cast<float>(StepIdx) / NumSteps : 1.0f;
				const FVector2D Next = OffsetFunction(Alpha); 
				const FVector2D Delta = Next - Last;
				InActions.MoveByOffset(Delta.X, Delta.Y);
				Last = Next;
			}
		}
	}

	bool FEditorProvider::WorldToPixel(const FVector& InWorldPosition, FVector2D& OutPixelPosition) const
	{
		if (FEditorViewportClient* ViewportClient = this->GetEditorViewportClient();
			ViewportClient && ViewportClient->Viewport && ViewportClient->GetScene() && ViewportClient->GetScene()->GetWorld())
		{
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				ViewportClient->Viewport,
				ViewportClient->GetScene(),
				ViewportClient->EngineShowFlags)
				.SetRealtimeUpdate(ViewportClient->IsRealtime()));

			const FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
			if (SceneView->WorldToPixel(InWorldPosition, OutPixelPosition))
			{
				// Unlike the default implementation, this is based on the center of the viewport rather than the top-left
				const FVector2D ViewportCenter = (SceneView->UnconstrainedViewRect.Max - SceneView->UnconstrainedViewRect.Min) / 2;
				OutPixelPosition -= FVector2D(ViewportCenter);

				return true;
			}
		}

		return false;
	}

	FLevelEditorProvider::FLevelEditorProvider(const TSharedRef<ILevelEditor>& InLevelEditor)
	{
		LevelEditor = InLevelEditor;
		ViewportWidgetPath = TEXT("<SLevelEditor>//<SLevelViewport>//<SViewport>");
		ViewportLocator = By::Path(ViewportWidgetPath);

		constexpr FLazyName LevelEditorTabName = "LevelEditor";
		const TSharedPtr<SDockTab> LevelEditorTab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(LevelEditorTabName));
		if (LevelEditorTab.IsValid())
		{
			TopLevelWindow = LevelEditorTab->GetParentWindow();	
		}

		ToolsContext = InLevelEditor->GetEditorModeManager().GetInteractiveToolsContext();
	}

	FEditorViewportClient* FLevelEditorProvider::GetEditorViewportClient() const
	{
		if (LevelEditor.IsValid() && LevelEditor->GetActiveViewportInterface().IsValid())
		{
			return &LevelEditor->GetActiveViewportInterface()->GetLevelViewportClient();
		}

		return nullptr;
	}

	UTypedElementSelectionSet* FLevelEditorProvider::GetActorSelectionSet() const
	{
		if (LevelEditor.IsValid())
		{
			return LevelEditor->GetMutableElementSelectionSet();
		}

		return nullptr;
	}
}

const TCHAR* LexToString(UE::Editor::InteractiveToolsFramework::Tests::EViewportType InViewportType)
{
	switch (InViewportType)
	{
	case UE::Editor::InteractiveToolsFramework::Tests::EViewportType::Perspective:
		return TEXT("Perspective");

	case UE::Editor::InteractiveToolsFramework::Tests::EViewportType::Orthographic:
		return TEXT("Orthographic");

	default:
		return TEXT("Unknown");
	}
}

const TCHAR* LexToString(ECoordSystem InCoordinateSystem)
{
	switch (InCoordinateSystem)
	{
	case ECoordSystem::COORD_World:
		return TEXT("World");

	case ECoordSystem::COORD_Local:
		return TEXT("Local");

	case ECoordSystem::COORD_Parent:
		return TEXT("Parent");

	case ECoordSystem::COORD_Explicit:
		return TEXT("Explicit");

	default:
		return TEXT("Unknown");
	}
}

const TCHAR* LexToString(EAxis::Type InAxis)
{
	switch (InAxis)
	{
	case EAxis::X:
		return TEXT("X");

	case EAxis::Y:
		return TEXT("Y");

	case EAxis::Z:
		return TEXT("Z");

	case EAxis::None:
	default:
		return TEXT("None");
	}
}

const TCHAR* LexToString(EAxisList::Type InAxisList)
{
	// Covers used, but not all values
	switch (InAxisList)
	{
	case EAxisList::X:
		return TEXT("X");

	case EAxisList::Y:
		return TEXT("Y");

	case EAxisList::Z:
		return TEXT("Z");

	case EAxisList::XY:
		return TEXT("XY");

	case EAxisList::YZ:
		return TEXT("YZ");

	case EAxisList::XZ:
		return TEXT("XZ");

	case EAxisList::Screen:
		return TEXT("Screen");

	case EAxisList::XYZ:
		return TEXT("XYZ");

	case EAxisList::None:
		return TEXT("None");

	default:
		return TEXT("Unknown");
	}
}

#endif
