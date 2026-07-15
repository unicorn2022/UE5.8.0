// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "CQTest.h"
#include "EditorGizmos/TransformGizmo.h"
#include "TransformGizmoEditorSettings.h"
#include "InputCoreTypes.h"
#include "Tests/InteractiveToolsFrameworkTestUtilities.h"
#include "Tests/TransformGizmoTestUtilities.h"
#include "Tests/TransformGizmoTestUtilities.inl"
#include "TransformGizmoEditorSettings.h"

using namespace UE::Editor::InteractiveToolsFramework::TransformGizmoTests;

TEST_CLASS_WITH_BASE_AND_TAGS(TransformGizmoTranslateTests, "Editor.InteractiveToolsFramework.TransformGizmo.Translate",
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::TTransformGizmoTest,
	"[EditorContext][EngineFilter]")
{
	using Super = TTransformGizmoTest<TransformGizmoTranslateTests, FNoDiscardAsserter>;
	
	static constexpr EGizmoTransformMode GizmoTransformMode = EGizmoTransformMode::Translate;

	BEFORE_EACH()
	{
		Super::Setup();

		SetTransformMode(GizmoTransformMode);
		SetCoordinateSystem(COORD_Local);
		SetViewportType(UE::Editor::InteractiveToolsFramework::Tests::EViewportType::Perspective);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Translate_Direct_Axis_Facing)
	{
		TestTranslateDirectAxis({ }, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::TranslateXAxis, EAxis::X);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Translate_Indirect_Axis_Facing)
	{
 		TestTranslateIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::X), EAxis::X);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Translate_Direct_Axis_Up)
	{
		TestTranslateDirectAxis({ }, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::TranslateZAxis, EAxis::Z);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Translate_Indirect_Axis_Up)
	{
		TestTranslateIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::Z), EAxis::Z);
	}

	TEST_METHOD(Translate_Indirect_Axis_Grazing)
	{
		TestTranslateIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::Y), EAxis::Y);
	}

protected:
	void TestTranslateDirectAxis(const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const ETransformGizmoPartIdentifier InPartId, const EAxis::Type InAxis)
	{
		const FVector AxisVector = UE::Editor::InteractiveToolsFramework::Tests::GetAxisVector(InAxis);
		
		const FTransformGizmoInteraction& InteractionSettings = GetDefault<UTransformGizmoEditorSettings>()->GizmosParameters.Interaction;
		const FVector2D PositiveDragDirection = InteractionSettings.TranslateInteraction.Direction.IsSet()
			? UserDragDirectionMultiplier * InteractionSettings.TranslateInteraction.Direction.GetValue()
			: UE::Editor::InteractiveToolsFramework::FTransformGizmoAccessor().GetScreenProjectedDirection(*GetTransformGizmo(), AxisVector);

		TestTransformDirectAxis(
			InModifierKeys, InMouseButtons,
			InPartId,
			InAxis,
			PositiveDragDirection,
			[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
			{
				return GetTranslateDeltaAlongAxis(InPreviousTransform, InNewTransform, Axis);
			},
			[](const FTransform& InTransform, const EAxis::Type Axis)
			{
				return InTransform.GetLocation().ToString();
			},
			TEXT("translated"));
	}

	void TestTranslateIndirectAxis(const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const EAxis::Type InAxis)
	{
		const FTransformGizmoInteraction& InteractionSettings = GetDefault<UTransformGizmoEditorSettings>()->GizmosParameters.Interaction;
		const FVector2D PositiveDragDirection = UserDragDirectionMultiplier * InteractionSettings.TranslateInteraction.Direction.Get(FVector2D(1, 1)); // @todo: get screen axis direction

		TestTransformIndirectAxis(
			InModifierKeys, InMouseButtons,
			InAxis,
			PositiveDragDirection,
			[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
			{
				return GetTranslateDeltaAlongAxis(InPreviousTransform, InNewTransform, Axis);
			},
			[](const FTransform& InTransform, const EAxis::Type Axis)
			{
				return InTransform.GetLocation().ToString();
			},
			TEXT("translated"));
	}
};

TEST_CLASS_WITH_BASE_AND_TAGS(TransformGizmoRotateTests, "Editor.InteractiveToolsFramework.TransformGizmo.Rotate",
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::TTransformGizmoTest,
	"[EditorContext][EngineFilter]")
{
	using Super = UE::Editor::InteractiveToolsFramework::TransformGizmoTests::TTransformGizmoTest<TransformGizmoRotateTests, FNoDiscardAsserter>;

	static constexpr EGizmoTransformMode GizmoTransformMode = EGizmoTransformMode::Rotate;

	BEFORE_EACH()
    {
    	Super::Setup();

    	SetTransformMode(GizmoTransformMode);
    	SetCoordinateSystem(COORD_Local);
		SetViewportType(UE::Editor::InteractiveToolsFramework::Tests::EViewportType::Perspective);
    }

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Rotate_Direct_Axis_Facing_Pull)
	{
		TestRotateDirectAxis<EAxisRotateMode::Pull>({}, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::RotateXAxis, EAxis::X);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Rotate_Direct_Axis_Facing_Arc)
	{
		TestRotateDirectAxis<EAxisRotateMode::Arc>({}, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::RotateXAxis, EAxis::X);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Rotate_Direct_Axis_Facing_ScreenArc)
	{
		TestRotateDirectAxis<EAxisRotateMode::ScreenArc>({}, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::RotateXAxis, EAxis::X);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Rotate_Indirect_Axis_Facing)
	{
 		TestRotateIndirectAxis({ EKeys::LeftControl }, { EMouseButtons::Left }, EAxis::X);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Rotate_Direct_Axis_Up_Pull)
	{
		TestRotateDirectAxis<EAxisRotateMode::Pull>({}, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::RotateZAxis, EAxis::Z);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Rotate_Direct_Axis_Up_Arc)
	{
		TestRotateDirectAxis<EAxisRotateMode::Arc>({}, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::RotateZAxis, EAxis::Z);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Rotate_Indirect_Axis_Up)
	{
		TestRotateIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::Z), EAxis::Z);
	}

	TEST_METHOD(Rotate_Direct_Axis_Grazing_Arc)
	{
		TestRotateDirectAxis<EAxisRotateMode::Arc>({}, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::RotateYAxis, EAxis::Y);
	}

	TEST_METHOD(Rotate_Indirect_Axis_Grazing)
	{
		TestRotateIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::Y), EAxis::Y);
	}

protected:
	template <EAxisRotateMode::Type RotateMode>
	void TestRotateDirectAxis(const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const ETransformGizmoPartIdentifier InPartId, const EAxis::Type InAxis, const bool bInTurnCLockwise = true)
	{
		const float TurnDirectionMultiplier = bInTurnCLockwise ? -1.0f : 1.0f;

		if constexpr (RotateMode == EAxisRotateMode::Pull)
		{
			const FTransformGizmoInteraction& InteractionSettings = GetDefault<UTransformGizmoEditorSettings>()->GizmosParameters.Interaction;
			const FVector2D DragDirection = (UserDragDirectionMultiplier * InteractionSettings.RotateInteraction.Direction.Get(FVector2D(1, 1))) * TurnDirectionMultiplier;

			TestTransformDirectAxis(
				InModifierKeys, InMouseButtons,
				InPartId,
				InAxis,
				DragDirection,
				[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
				{
					return GetRotateDeltaOnAxis(InPreviousTransform, InNewTransform, Axis);
				},
				[](const FTransform& InTransform, const EAxis::Type Axis)
				{
					return InTransform.GetRotation().ToString();
				},
				TEXT("rotated"));

			return;
		}
		else
		{
			TOptional<const UE::Editor::InteractiveToolsFramework::Tests::FLocator> StartLocatorOverride = { };
			UE::Editor::InteractiveToolsFramework::Tests::FLocator::FOffsetFunction EndLocatorFunction;

			if constexpr (RotateMode == EAxisRotateMode::ScreenArc)
			{
				EndLocatorFunction = [TurnDirectionMultiplier](const float InAlpha)
				{
					// For ScreenArc, we just make a "wheel" around the center 
					const double AngleRad = (UE_TWO_PI * TurnDirectionMultiplier) * InAlpha;
					const FVector2D AngleVec = FVector2D(FMath::Cos(AngleRad), FMath::Sin(AngleRad));
					
					constexpr float CursorOffsetFromOrigin = 100.0f;
					return AngleVec * CursorOffsetFromOrigin;
				};
			}
			else if constexpr (RotateMode == EAxisRotateMode::Arc)
			{
				EndLocatorFunction = [EditorProvider = this->SharedEnvironment->EditorProvider, TurnDirectionMultiplier](const float InAlpha)
				{
					// For Arc, we make a "half wheel" around the center of the gizmo, aligned to it's plane
					const double AngleRad = (UE_PI * TurnDirectionMultiplier) * InAlpha;

					// @todo: is there's shared functionality that can be referenced here?
					const FVector PlaneOrigin = FVector::ZeroVector;
					const FVector PlaneNormal = FVector::UpVector;

					const FVector PlaneAxis = FVector::RightVector; // This doesn't matter, but should be different to Normal
					FVector StartAxis = (PlaneAxis - FVector::DotProduct(PlaneAxis, PlaneNormal) * PlaneNormal).GetSafeNormal();
					if (StartAxis.IsNearlyZero())
					{
						StartAxis = FVector::CrossProduct(PlaneNormal, PlaneAxis).GetSafeNormal();
					}

					const FVector AngleVec = StartAxis.RotateAngleAxisRad(-AngleRad, PlaneNormal);

					constexpr float CursorOffsetFromOrigin = 100.0f;

					FVector2D AngleVec2D = FVector2D::One();
					EditorProvider->WorldToPixel(PlaneOrigin + (AngleVec * CursorOffsetFromOrigin), AngleVec2D);

					return AngleVec2D;
				};
			}

			TestTransformDirectAxis(
				InModifierKeys, InMouseButtons,
				InPartId,
				InAxis,
				StartLocatorOverride,
				EndLocatorFunction,
				[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
				{
					return GetRotateDeltaOnAxis(InPreviousTransform, InNewTransform, Axis);
				},
				[](const FTransform& InTransform, const EAxis::Type Axis)
				{
					return InTransform.GetRotation().ToString();
				},
				TEXT("rotated"));
		}
	}
	
	void TestRotateIndirectAxis(const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const EAxis::Type InAxis)
	{
		const FTransformGizmoInteraction& InteractionSettings = GetDefault<UTransformGizmoEditorSettings>()->GizmosParameters.Interaction;
		const FVector2D PositiveDragDirection = UserDragDirectionMultiplier * InteractionSettings.RotateInteraction.Direction.Get(FVector2D(1, 1)); // @todo: get screen axis direction

		TestTransformIndirectAxis(
			InModifierKeys, InMouseButtons,
			InAxis,
			PositiveDragDirection,
			[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
			{
				return GetRotateDeltaOnAxis(InPreviousTransform, InNewTransform, Axis);
			},
			[](const FTransform& InTransform, const EAxis::Type Axis)
			{
				return InTransform.GetRotation().ToString();
			},
			TEXT("rotated"));
	}
};

TEST_CLASS_WITH_BASE_AND_TAGS(TransformGizmoScaleTests, "Editor.InteractiveToolsFramework.TransformGizmo.Scale",
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::TTransformGizmoTest,
	"[EditorContext][EngineFilter]")
{
	using Super = UE::Editor::InteractiveToolsFramework::TransformGizmoTests::TTransformGizmoTest<TransformGizmoScaleTests, FNoDiscardAsserter>;
	
	static constexpr EGizmoTransformMode GizmoTransformMode = EGizmoTransformMode::Scale;

	BEFORE_EACH()
	{
		Super::Setup();

		SetTransformMode(GizmoTransformMode);
		SetCoordinateSystem(COORD_Local);
		SetViewportType(UE::Editor::InteractiveToolsFramework::Tests::EViewportType::Perspective);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Scale_Direct_Axis_Facing)
	{
		TestScaleDirectAxis({ }, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::ScaleXAxis, EAxis::X);
	}

	/** Operates on whatever axis is more facing the view. */
	TEST_METHOD(Scale_Indirect_Axis_Facing)
	{
 		TestScaleIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::X), EAxis::X);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Scale_Direct_Axis_Up)
	{
		TestScaleDirectAxis({ }, { EMouseButtons::Left }, ETransformGizmoPartIdentifier::ScaleZAxis, EAxis::Z);
	}

	/** Operates on the Z/Up axis, which in some cases has unique quirks. */
	TEST_METHOD(Scale_Indirect_Axis_Up)
	{
		TestScaleIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::Z), EAxis::Z);
	}

	TEST_METHOD(Scale_Indirect_Axis_Grazing)
	{
		TestScaleIndirectAxis({ EKeys::LeftControl }, GetMouseButtonsForIndirectAxis(EAxis::Y), EAxis::Y);
	}

protected:
	void TestScaleDirectAxis(const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const ETransformGizmoPartIdentifier InPartId, const EAxis::Type InAxis)
	{
		const FTransformGizmoInteraction& InteractionSettings = GetDefault<UTransformGizmoEditorSettings>()->GizmosParameters.Interaction;
		const FVector2D PositiveDragDirection = UserDragDirectionMultiplier * InteractionSettings.ScaleInteraction.Direction;

		TestTransformDirectAxis(
			InModifierKeys, InMouseButtons,
			InPartId,
			InAxis,
			PositiveDragDirection,
			[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
			{
				return GetScaleDeltaAlongAxis(InPreviousTransform, InNewTransform, Axis);
			},
			[](const FTransform& InTransform, const EAxis::Type Axis)
			{
				return InTransform.GetScale3D().ToString();
			},
			TEXT("scaled"));
	}

	void TestScaleIndirectAxis(const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons, const EAxis::Type InAxis)
	{
		const FTransformGizmoInteraction& InteractionSettings = GetDefault<UTransformGizmoEditorSettings>()->GizmosParameters.Interaction;
		const FVector2D PositiveDragDirection = UserDragDirectionMultiplier * InteractionSettings.ScaleInteraction.Direction;

		TestTransformIndirectAxis(
			InModifierKeys, InMouseButtons,
			InAxis,
			PositiveDragDirection,
			[](const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type Axis)
			{
				return GetScaleDeltaAlongAxis(InPreviousTransform, InNewTransform, Axis);
			},
			[](const FTransform& InTransform, const EAxis::Type Axis)
			{
				return InTransform.GetScale3D().ToString();
			},
			TEXT("scaled"));
	}
};

#endif // WITH_EDITOR && WITH_AUTOMATION_TESTS
