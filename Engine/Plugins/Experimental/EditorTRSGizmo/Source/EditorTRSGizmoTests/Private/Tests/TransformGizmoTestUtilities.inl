// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tests/InteractiveToolsFrameworkTestUtilities.inl"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "EditorGizmos/EditorTransformGizmoUtil.h"
#include "EditorGizmos/TransformGizmoAccessor.h"
#include "EditorViewportCommands.h"
#include "InteractiveGizmoManager.h"
#include "TransformGizmoEditorSettings.h"

namespace UE::Editor::InteractiveToolsFramework::TransformGizmoTests
{
	namespace Private
	{
		inline FKey GetCommandKeyBind(const TSharedPtr<FUICommandInfo>& InCommandInfo)
		{
			if (InCommandInfo.IsValid())
			{
				return InCommandInfo->GetFirstValidChord()->Key;
			}

			return EKeys::Invalid;
		}
		
		inline FKey GetTransformModeKeyBind(const EGizmoTransformMode InTransformMode)
		{
			// @note: this assumes a single key, without modifiers
			switch (InTransformMode)
			{
			case EGizmoTransformMode::Translate:
				return GetCommandKeyBind(FEditorViewportCommands::Get().TranslateMode);

			case EGizmoTransformMode::Rotate:
				return GetCommandKeyBind(FEditorViewportCommands::Get().RotateMode);

			case EGizmoTransformMode::Scale:
				return GetCommandKeyBind(FEditorViewportCommands::Get().ScaleMode);

			default:
			case EGizmoTransformMode::None:
			case EGizmoTransformMode::Max:
				return EKeys::Invalid;
			}
		}
	}

	template <typename Derived, typename AsserterType>
	FVector2D TTransformGizmoTest<Derived, AsserterType>::UserDragDirectionMultiplier = FVector2D(1, -1);

	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::Setup()
	{
		Super::Setup();

		// Select Gizmo (subject Actor)
		{
			this->TestCommandBuilder
			.Do(TEXT("Select the subject actor"),
				[this]()
				{
					this->TestWorld->SelectActor(this->TestWorld->GetActor());
				});
		}

		// Ensure experimental gizmo enabled
		{
			UTransformGizmoEditorSettings* GizmoSettings = GetMutableDefault<UTransformGizmoEditorSettings>();
			GizmoSettings->SetUseExperimentalGizmo(true);

			// UEditorTRSEditorSettings* TRSSettings = GetMutableDefault<UEditorTRSEditorSettings>();
			// TRSSettings->SetEnableExperimentalGizmo58(true);
			// TRSSettings->PostEditChange();
		}
	}

	template <typename Derived, typename AsserterType>
	bool TTransformGizmoTest<Derived, AsserterType>::GetHitPositionForPart(const ETransformGizmoPartIdentifier InPartId, FVector& OutHitPosition)
	{
		FEditorViewportClient* ViewportClient = this->SharedEnvironment->EditorProvider->GetEditorViewportClient();
		if (!this->Assert.IsNotNull(ViewportClient, TEXT("Viewport client isn't valid")))
		{
			return false;
		}

		const UTransformGizmo* TransformGizmo = GetTransformGizmo();
		if (!this->Assert.IsNotNull(TransformGizmo, TEXT("Transform gizmo isn't valid")))
		{
			return false;
		}

		const FVector ViewLocation = ViewportClient->GetViewTransform().GetLocation();
		FVector ViewDirection = ViewportClient->GetViewTransform().GetRotation().Vector();

		FTransform GizmoTransform = FTransformGizmoAccessor().GetGizmoTransform(*TransformGizmo);
		UGizmoElementHitMultiTarget* HitTarget = FTransformGizmoAccessor().GetHitTarget(*TransformGizmo);
		if (!this->Assert.IsNotNull(HitTarget, TEXT("Hit target isn't valid")))
		{
			return false;
		}

		const FVector DirectionToGizmo = (GizmoTransform.GetLocation() - ViewLocation).GetSafeNormal();

		constexpr int32 NumAttempts = 16384;
		int32 AttemptNum = 0;

		FRandomStream RandomDirectionStream(128);

		constexpr double MaxHalfConeAngleRad = FMath::DegreesToRadians(80.0f);
		constexpr double MinHalfConeAngleRad = FMath::DegreesToRadians(0.5f);
		constexpr double ExpandHalfConeAngleRadStep = FMath::DegreesToRadians(4.0f);
		constexpr uint32 ConeExpandSampleThreshold = NumAttempts / 16; // The number of ray samples before the cone is adjusted
		
		constexpr bool bDebugHitPoints = false;
		
		double HalfConeAngleRad = MaxHalfConeAngleRad;

		FInputDeviceRay Ray;
		Ray.WorldRay.Origin = ViewLocation;
		Ray.WorldRay.Direction = DirectionToGizmo;

		TOptional<ETransformGizmoPartIdentifier> HitPartId;
		while (AttemptNum < NumAttempts && !HitPartId.IsSet())
		{
			FInputRayHit Hit = HitTarget->IsHit(Ray);
			if (Hit.bHit)
			{
				if (Hit.HitIdentifier == static_cast<uint32>(InPartId))
				{
					OutHitPosition = Ray.WorldRay.PointAt(Hit.HitDepth);

					// Debug
					if (bDebugHitPoints)
					{
						const UWorld* World = this->TestWorld->GetWorld();
						DrawDebugPoint(World, Ray.WorldRay.Origin + Ray.WorldRay.Direction * 100.0f, 5.0f, FColor::Red, false, 1.0f);
					}

					return true;
				}

				// Something hit, but not the correct part, adjust the cone angle
				HalfConeAngleRad = FMath::Clamp(
					MinHalfConeAngleRad + (ExpandHalfConeAngleRadStep * (AttemptNum / ConeExpandSampleThreshold)),
					MinHalfConeAngleRad,
					MaxHalfConeAngleRad);
			}

			// Set a new ray direction
			RandomDirectionStream.GenerateNewSeed();
			Ray.WorldRay.Direction = RandomDirectionStream.VRandCone(DirectionToGizmo, HalfConeAngleRad);

			// Debug
			if (bDebugHitPoints)
			{
				const UWorld* World = this->TestWorld->GetWorld();
				DrawDebugPoint(World, Ray.WorldRay.Origin + Ray.WorldRay.Direction * 100.0f, 5.0f, FLinearColor::Gray.ToFColor(true), false, 4.0f);
			}

			++AttemptNum;
		}

		this->Assert.Fail(TEXT("Failed to get hit position for gizmo part."));
		return false;
	}

	template <typename Derived, typename AsserterType>
	UInteractiveGizmoManager* TTransformGizmoTest<Derived, AsserterType>::GetGizmoManager()
	{
		const UInteractiveToolsContext* ToolsContext = this->SharedEnvironment->EditorProvider->ToolsContext;
		if (!ToolsContext)
		{
			return nullptr;
		}

		UInteractiveGizmoManager* GizmoManager = ToolsContext->GizmoManager;
		if (!this->Assert.IsNotNull(GizmoManager, TEXT("Gizmo manager is null")))
		{
			return nullptr;
		};

		return GizmoManager;
	}

	template <typename Derived, typename AsserterType>
	UTransformGizmo* TTransformGizmoTest<Derived, AsserterType>::GetTransformGizmo()
	{
		const UInteractiveToolsContext* ToolsContext = this->SharedEnvironment->EditorProvider->ToolsContext;
		if (!ToolsContext)
		{
			return nullptr;
		}

		UTransformGizmo* TransformGizmo = UE::EditorTransformGizmoUtil::FindDefaultTransformGizmo(ToolsContext->ToolManager);
		if (!this->Assert.IsNotNull(TransformGizmo, TEXT("Transform gizmo is null")))
		{
			return nullptr;
		};

		return TransformGizmo;
	}

	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::SetTransformMode(const EGizmoTransformMode InTransformMode)
	{
		// Assumes viewport focus and object selection
		this->TestCommandBuilder
		.template DoAsync<bool>(
			TEXT("Set Interaction Mode"),
			[this, InTransformMode]() -> TAsyncResult<bool>
			{
				const TSharedRef<IAsyncDriverSequence> LocalSequence = this->Driver->CreateSequence();
				LocalSequence->Actions()
				.Type(Private::GetTransformModeKeyBind(InTransformMode));

				return LocalSequence->Perform();
			},
			[this, InTransformMode](bool)
			{
				if (UTransformGizmo* TransformGizmo = GetTransformGizmo())
				{
					ASSERT_THAT(AreEqual(FTransformGizmoAccessor().GetCurrentMode(*TransformGizmo), InTransformMode, TEXT("Transform gizmo is in the expected interaction mode")));
				}
			},
			this->DriverWaitTimeout);			
	}

	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::SetCoordinateSystem(const ECoordSystem InCoordinateSystem)
	{
		if (FEditorViewportClient* ViewportClient = this->SharedEnvironment->EditorProvider->GetEditorViewportClient())
		{
			ViewportClient->SetWidgetCoordSystemSpace(InCoordinateSystem);
		}
	}
	
	inline TAutoConsoleVariable<int32> CVarDragSteps(
		TEXT("Editor.Gizmos.Tests.DragSteps"),
		16,
		TEXT("Controls the number of steps in a simulated drag.")
	);

	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::TestTransformDirectAxis(
		const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const ETransformGizmoPartIdentifier InPartId, const EAxis::Type InAxis, const FVector2D& InDragDirection,
		const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction, const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
		const FString& InValuePastFormLabel)
	{
		this->TestTransformDirectAxisInternal(
			InModifierKeys,
			InMouseButtons,
			InPartId,
			InAxis,
			TOptional<const Tests::FLocator>(),
			Tests::FLocator::FromOffset(
				FIntPoint(InDragDirection.X * (this->SharedEnvironment->EditorProvider->ViewportSize.X / 2.5f),
					InDragDirection.Y * (this->SharedEnvironment->EditorProvider->ViewportSize.Y / 2.5f)),
					CVarDragSteps.GetValueOnAnyThread()),
			InGetDeltaFunction,
			InGetValueStringFunction,
			InValuePastFormLabel);
	}

	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::TestTransformDirectAxis(
		const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const ETransformGizmoPartIdentifier InPartId, const EAxis::Type InAxis, const TOptional<const Tests::FLocator>& InStartLocatorOverride, const Tests::FLocator::FOffsetFunction& InDragFunction,
		const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction, const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
		const FString& InValuePastFormLabel)
	{
		this->TestTransformDirectAxisInternal(
			InModifierKeys,
			InMouseButtons,
			InPartId,
			InAxis,
			InStartLocatorOverride,
			Tests::FLocator::FromOffsetFunction(
				[this, InDragFunction](const float InAlpha)
				{
					return InDragFunction(InAlpha) * UserDragDirectionMultiplier;
				},
				CVarDragSteps.GetValueOnAnyThread()),
			InGetDeltaFunction,
			InGetValueStringFunction,
			InValuePastFormLabel);
	}

	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::TestTransformDirectAxisInternal(
		const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const ETransformGizmoPartIdentifier InPartId, const EAxis::Type InAxis, const TOptional<const Tests::FLocator>& InStartLocatorOverride, const Tests::FLocator& InEndLocator,
		const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction, const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
		const FString& InValuePastFormLabel)
	{
		using namespace UE::Editor::InteractiveToolsFramework::Tests;

		AActor* TestActor = this->TestWorld->GetActor();
		ASSERT_THAT(IsNotNull(TestActor, TEXT("Test actor isn't valid")));

		const FTransform InitialTransform = TestActor->GetTransform();

		Tests::FLocator StartLocator = Tests::FLocator::Empty();
		if (InStartLocatorOverride.IsSet())
		{
			StartLocator = InStartLocatorOverride.GetValue();
		}
		else
		{
			FVector HitPositionOnPart = FVector::ZeroVector;
			const bool bHitPart = this->GetHitPositionForPart(InPartId, HitPositionOnPart);
			ASSERT_THAT(IsTrue(bHitPart, *FString::Printf(TEXT("Unable to get hit position for gizmo part %d"), static_cast<uint32>(InPartId))));

			FVector2D HitPosition2D;
			ASSERT_THAT(IsTrue(this->SharedEnvironment->EditorProvider->WorldToPixel(HitPositionOnPart, HitPosition2D), TEXT("Unable to convert hit position to screen space")));

			StartLocator = FLocator::FromOffset(
				this->SharedEnvironment->EditorProvider->ViewportLocator.ToSharedRef(),
				FIntPoint(HitPosition2D.X, HitPosition2D.Y));
		}

		this->DoClickDrag(
			this->TestCommandBuilder,
			InModifierKeys,
			InMouseButtons,
			StartLocator,
			InEndLocator);

		this->TestCommandBuilder
			.Then(*FString::Printf(TEXT("Check that the actor was %s in the %s axis"), *InValuePastFormLabel, LexToString(InAxis)),
				[this, InitialTransform, InAxis, InValuePastFormLabel, TestActor,
					GetDeltaFunction = InGetDeltaFunction,
					GetValueStringFunction = InGetValueStringFunction]()
				{
					ASSERT_THAT(IsNotNull(TestActor, TEXT("Test actor isn't valid")));

					const FTransform NewTransform = TestActor->GetTransform();
					const float Delta = GetDeltaFunction(InitialTransform, NewTransform, InAxis);

					ASSERT_THAT(IsFalse(FMath::IsNearlyZero(Delta, UE_KINDA_SMALL_NUMBER), *FString::Printf(TEXT("Actor should have %s along axis (was %s, is now %s"),
						*InValuePastFormLabel,
						*GetValueStringFunction(InitialTransform, InAxis),
						*GetValueStringFunction(NewTransform, InAxis))));
				});
	}
	
	
	template <typename Derived, typename AsserterType>
	void TTransformGizmoTest<Derived, AsserterType>::TestTransformIndirectAxis(
		const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const EAxis::Type InAxis, const FVector2D& InDragDirection,
		const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction, const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
		const FString& InValuePastFormLabel)
	{
		using namespace UE::Editor::InteractiveToolsFramework::Tests;

		AActor* TestActor = this->TestWorld->GetActor();
		ASSERT_THAT(IsNotNull(TestActor, TEXT("Test actor isn't valid")));

		const FTransform InitialTransform = TestActor->GetTransform();

		this->DoClickDrag(
			this->TestCommandBuilder,
			InModifierKeys,
			InMouseButtons,
			FLocator(this->SharedEnvironment->EditorProvider->ViewportLocator.ToSharedRef()),
			FLocator::FromOffset(
				FIntPoint(InDragDirection.X * (this->SharedEnvironment->EditorProvider->ViewportSize.X / 2.5f),
					InDragDirection.Y * (this->SharedEnvironment->EditorProvider->ViewportSize.Y / 2.5f)),
					CVarDragSteps.GetValueOnAnyThread()));

		this->TestCommandBuilder
			.Then(*FString::Printf(TEXT("Check that the actor was %s in the %s axis"), *InValuePastFormLabel, LexToString(InAxis)),
				[this, InitialTransform, InAxis, InValuePastFormLabel, TestActor,
					GetDeltaFunction = InGetDeltaFunction,
					GetValueStringFunction = InGetValueStringFunction]()
				{
					ASSERT_THAT(IsNotNull(TestActor, TEXT("Test actor isn't valid")));

					const FTransform NewTransform = TestActor->GetTransform();
					const float Delta = GetDeltaFunction(InitialTransform, NewTransform, InAxis);

					ASSERT_THAT(IsFalse(FMath::IsNearlyZero(Delta, UE_KINDA_SMALL_NUMBER), *FString::Printf(TEXT("Actor should have %s along axis (was %s, is now %s"),
						*InValuePastFormLabel,
						*GetValueStringFunction(InitialTransform, InAxis),
						*GetValueStringFunction(NewTransform, InAxis))));
				});
	}
}

#endif
