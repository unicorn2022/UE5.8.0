// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

#include "Editor/LevelEditor/Private/SLevelEditor.h"
#include "EditorGizmos/TransformGizmo.h"
#include "TransformGizmoEditorSettings.h"
#include "InteractiveToolsFrameworkTestUtilities.h"

namespace UE::Editor::InteractiveToolsFramework::TransformGizmoTests
{
	/** Assumes Previous and New transform have the same orientation. */
	float GetTranslateDeltaAlongAxis(const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type InAxis);

	float GetRotateDeltaOnAxis(const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type InAxis);

	/** Assumes Previous and New transform have the same orientation. */
	float GetScaleDeltaAlongAxis(const FTransform& InPreviousTransform, const FTransform& InNewTransform, const EAxis::Type InAxis);

	TArray<EMouseButtons::Type> GetMouseButtonsForIndirectAxis(EAxis::Type InAxis);

	enum class EInteractionType : uint8
	{
		Direct,
		Indirect,

		Max
	};

	enum class EGizmoComponentType : uint8
	{
		Axis,
		Planar,
		Uniform,
		Screen,
		Arcball,

		Max
	};

	struct FInteractionTestParams
	{
		ETransformGizmoPartIdentifier PartId;

		EGizmoTransformMode TransformMode;
		EInteractionType InteractionType;
		EGizmoComponentType ComponentType;
		ECoordSystem CoordinateSystem;
		EAxisList::Type AxisList;
		EAxisRotateMode::Type RotateMode = EAxisRotateMode::Pull;

		Tests::EViewportType ViewportType;

		TOptional<FVector2D> DragDirection; // Explicit drag direction, rather than auto-aligned
	};

	/** Returns valid components for the given transform mode, ie. arcball only applies to rotate. */
	TArray<EGizmoComponentType> GetGizmoComponentTypesForTransformMode(const EGizmoTransformMode InTransformMode);

	/** Returns all valid AxisList combinations for the given component type and transform mode. */
	TArray<EAxisList::Type> GetAxisListValuesForTransformModeAndComponentType(const EGizmoTransformMode InTransformMode, const EGizmoComponentType InComponentType);

	template <typename Derived, typename AsserterType>
	struct TTransformGizmoTest : public Tests::TInteractionTest<Derived, AsserterType>
	{
		using Super = Tests::TInteractionTest<Derived, AsserterType>;
		
	protected:
		virtual void Setup() override;

		/** Shoots a bunch of rays until we hit the given part. */
		bool GetHitPositionForPart(const ETransformGizmoPartIdentifier InPartId, FVector& OutHitPosition);
		
		UInteractiveGizmoManager* GetGizmoManager();
		UTransformGizmo* GetTransformGizmo();

		/** Set via the mapped shortcut keys for each mode, emulating user input. */
		void SetTransformMode(const EGizmoTransformMode InTransformMode);

		/** Sets directly, not via user input emulation. */
		void SetCoordinateSystem(const ECoordSystem InCoordinateSystem);

#pragma region Interaction
		void TestTransformDirectAxis(
			const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons,
			const ETransformGizmoPartIdentifier InPartId,
			const EAxis::Type InAxis,
			const FVector2D& InDragDirection,
			const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction,
			const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
			const FString& InValuePastFormLabel);

		void TestTransformDirectAxis(
			const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons,
			const ETransformGizmoPartIdentifier InPartId,
			const EAxis::Type InAxis,
			const TOptional<const Tests::FLocator>& InStartLocatorOverride,
			const Tests::FLocator::FOffsetFunction& InDragFunction,
			const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction,
			const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
			const FString& InValuePastFormLabel);

		void TestTransformIndirectAxis(
			const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons,
			const EAxis::Type InAxis,
			const FVector2D& InDragDirection,
			const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction,
			const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
			const FString& InValuePastFormLabel);
#pragma endregion Interaction

	protected:
		void TestTransformDirectAxisInternal(
			const TArray<FKey>& InModifierKeys, const TArray<EMouseButtons::Type>& InMouseButtons,
			const ETransformGizmoPartIdentifier InPartId,
			const EAxis::Type InAxis,
			const TOptional<const Tests::FLocator>& InStartLocatorOverride,
			const Tests::FLocator& InEndLocator,
			const TFunction<float(const FTransform&, const FTransform&, const EAxis::Type)>& InGetDeltaFunction,
			const TFunction<FString(const FTransform&, const EAxis::Type)>& InGetValueStringFunction,
			const FString& InValuePastFormLabel);

	protected:
		static FVector2D UserDragDirectionMultiplier; // Flip the Y axis so that positive Y = upper right
	};
}

const TCHAR* LexToString(EGizmoTransformMode InTransformMode);

const TCHAR* LexToString(UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EInteractionType InInteractionType);

const TCHAR* LexToString(UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType InComponentType);

ENUM_RANGE_BY_FIRST_AND_LAST(
	EGizmoTransformMode,
	EGizmoTransformMode::Translate,
	EGizmoTransformMode::Scale);

ENUM_RANGE_BY_COUNT(
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EInteractionType,
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EInteractionType::Max);

ENUM_RANGE_BY_COUNT(
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType,
	UE::Editor::InteractiveToolsFramework::TransformGizmoTests::EGizmoComponentType::Max);

#endif
