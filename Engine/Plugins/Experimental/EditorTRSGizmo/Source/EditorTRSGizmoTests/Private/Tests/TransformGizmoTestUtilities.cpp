// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransformGizmoTestUtilities.h"

#include "TransformGizmoEditorSettings.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

namespace UE::Editor::InteractiveToolsFramework::TransformGizmoTests
{
	float GetTranslateDeltaAlongAxis(const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type InAxis)
	{
		const FVector AxisVector = InPreviousTransform.GetUnitAxis(InAxis);

		const FVector PreviousLocation = InPreviousTransform.GetLocation();
		const FVector NewLocation = InNewTransform.GetLocation();
		const FVector Delta = NewLocation - PreviousLocation;

		return FVector::DotProduct(Delta, AxisVector);
	}

	float GetRotateDeltaOnAxis(const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type InAxis)
	{
		const FVector AxisVector = InPreviousTransform.GetUnitAxis(InAxis);

		const FQuat PreviousRotation = InPreviousTransform.GetRotation();
		const FQuat NewRotation = InNewTransform.GetRotation();
		const FQuat Delta = NewRotation * PreviousRotation.Inverse();

		// Project the delta onto the axis
		const FVector DeltaAxis = Delta.GetRotationAxis();
		const float DeltaAngle = FMath::RadiansToDegrees(Delta.GetAngle());

		return FVector::DotProduct(DeltaAxis, AxisVector) * DeltaAngle;
	}

	float GetScaleDeltaAlongAxis(const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type InAxis)
	{
		const FVector AxisVector = InPreviousTransform.GetUnitAxis(InAxis);

		const FVector PreviousScale = InPreviousTransform.GetScale3D();
		const FVector NewScale = InNewTransform.GetScale3D();
		const FVector Delta = NewScale - PreviousScale;

		return FVector::DotProduct(Delta, AxisVector);
	}

	TArray<EMouseButtons::Type> GetMouseButtonsForIndirectAxis(EAxis::Type InAxis)	
	{
		const UTransformGizmoEditorSettings* GizmoSettings = GetDefault<UTransformGizmoEditorSettings>();
			
		// if (GizmoSettings->GizmosParameters.bSequentialIndirectAxesButtons)
		// {
		// 	switch (InAxis)
		// 	{
		// 	case EAxis::X:
		// 		return { EMouseButtons::Left };
		// 	case EAxis::Y:
		// 		return { EMouseButtons::Middle };
		// 	case EAxis::Z:
		// 		return { EMouseButtons::Right };
		// 	default:
		// 		return {};
		// 	}
		// }
			
		switch (InAxis)
		{
		case EAxis::X:
			return { EMouseButtons::Left };
		case EAxis::Y:
			return { EMouseButtons::Right };
		case EAxis::Z:
			return { EMouseButtons::Middle };
		default:
			return {};
		}
	}

	TArray<EGizmoComponentType> GetGizmoComponentTypesForTransformMode(const EGizmoTransformMode InTransformMode)
	{
		if (InTransformMode == EGizmoTransformMode::Rotate)
		{
			static const TArray<EGizmoComponentType> RotateComponents =
			{
				EGizmoComponentType::Axis,
				EGizmoComponentType::Planar,
				EGizmoComponentType::Screen,
				EGizmoComponentType::Arcball
			};

			return RotateComponents;
		}

		static const TArray<EGizmoComponentType> NonRotateComponents =
		{
			EGizmoComponentType::Axis,
			EGizmoComponentType::Planar,
			EGizmoComponentType::Uniform,
		};

		return NonRotateComponents;
	}

	TArray<EAxisList::Type> GetAxisListValuesForTransformModeAndComponentType(const EGizmoTransformMode InTransformMode, const EGizmoComponentType InComponentType)
	{
		if (InComponentType == EGizmoComponentType::Axis)
		{
			static const TArray<EAxisList::Type> AxisComponents =
			{
				EAxisList::X,
				EAxisList::Y,
				EAxisList::Z
			};

			return AxisComponents;
		}

		if (InComponentType == EGizmoComponentType::Planar)
		{
			static const TArray<EAxisList::Type> PlanarComponents =
			{
				EAxisList::XY,
				EAxisList::YZ,
				EAxisList::XZ
			};

			return PlanarComponents;
		}

		if (InComponentType == EGizmoComponentType::Screen)
		{
			static const TArray<EAxisList::Type> ScreenComponents = { EAxisList::Screen };
			return ScreenComponents;
		}

		static const TArray<EAxisList::Type> UniformComponents = { EAxisList::XYZ };
		return UniformComponents;
	}
}

const TCHAR* LexToString(EGizmoTransformMode InTransformMode)
{
	switch (InTransformMode)
	{
	case EGizmoTransformMode::Translate:
		return TEXT("Translate");

	case EGizmoTransformMode::Rotate:
		return TEXT("Rotate");

	case EGizmoTransformMode::Scale:
		return TEXT("Scale");

	default:
		return TEXT("Unknown");
	}
}

const TCHAR* LexToString(UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EInteractionType InInteractionType)
{
	switch (InInteractionType)
	{
	case UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EInteractionType::Direct:
		return TEXT("Direct");

	case UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EInteractionType::Indirect:
		return TEXT("Indirect");

	default:
		return TEXT("Unknown");
	}
}

const TCHAR* LexToString(UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType InComponentType)
{
	switch (InComponentType)
	{
	case UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType::Axis:
		return TEXT("Axis");

	case UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType::Planar:
		return TEXT("Planar");

	case UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType::Uniform:
		return TEXT("Uniform");

	case UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType::Arcball:
		return TEXT("Arcball");

	default:
		return TEXT("Unknown");
	}
}

#endif
