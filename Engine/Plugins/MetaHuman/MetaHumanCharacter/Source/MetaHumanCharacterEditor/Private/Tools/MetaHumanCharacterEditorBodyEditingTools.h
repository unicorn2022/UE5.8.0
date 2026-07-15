// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "MetaHumanCharacterEditorMeshEditingTools.h"
#include "MetaHumanCharacterEditorSubTools.h"
#include "BaseTools/SingleTargetWithSelectionTool.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterIdentity.h"

#include "MetaHumanCharacterEditorBodyEditingTools.generated.h"

enum class EMetaHumanClothingVisibilityState : uint8;

UENUM()
enum class EMetaHumanCharacterBodyEditingTool : uint8
{
	Model,
	Blend
};

UCLASS()
class UMetaHumanCharacterEditorBodyToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual bool CanBuildTool(const FToolBuilderState& InSceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface

	UPROPERTY()
	EMetaHumanCharacterBodyEditingTool ToolType = EMetaHumanCharacterBodyEditingTool::Blend;
};

class FMetaHumanCharacterClothVisibilityBase
{
protected:
	/** Storage for the last preview material set */
	TOptional<EMetaHumanCharacterSkinPreviewMaterial> SavedPreviewMaterial;

	/** Storage for the last preview material set */
	TOptional<EMetaHumanClothingVisibilityState> SavedClothingVisibilityState;

	/** Helper to update the visibility of the input character if needed */
	void UpdateClothVisibility(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, bool bStartBodyModeling, bool bUpdateMaterialHiddenFaces = true);
};

UCLASS(Abstract)
class UMetaHumanCharacterBodyModelSubToolBase : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	virtual void SetEnabled(bool bInIsEnabled)
	{
		bSubToolActive = bInIsEnabled;
	};

	UPROPERTY(Transient)
	bool bSubToolActive = true;
};


struct FMetaHumanCharacterBodyConstraintItem
{
	FName Name;
	bool bIsActive = false;
	float TargetMeasurement = 100.0f;
	float ActualMeasurement = 100.0f;
	float MinMeasurement = 0.0f;
	float MaxMeasurement = 200.0f;
};

using FMetaHumanCharacterBodyConstraintItemPtr = TSharedPtr<FMetaHumanCharacterBodyConstraintItem>;


UCLASS()
class UMetaHumanCharacterParametricBodyProperties : public UMetaHumanCharacterBodyModelSubToolBase, public FMetaHumanCharacterClothVisibilityBase
{
	GENERATED_BODY()

public:
	//~Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface
	
	bool IsFixedBodyType() const;

	void OnBeginConstraintEditing();
	void OnConstraintItemsChanged(bool bInCommitChange);
	void ResetConstraints();
	void PerformParametricFit();

	TArray<FMetaHumanCharacterBodyConstraintItemPtr> GetConstraintItems(const TArray<FName>& ConstraintNames);
	void OnBodyStateChanged();
	/**
	 * Refreshes BodyConstraintItems' ActualMeasurement (and optionally MinMeasurement / MaxMeasurement)
	 * plus ActiveContours from the current body state.
	 *
	 * @param bUpdateRanges  When true, also recomputes Min/MaxMeasurement via BodyState->GetBodyConstraints()
	 */
	void UpdateMeasurements(bool bUpdateRanges = true);

	/** Multiplier applied to the measurement ranges defined in the body model to expand or contract the range of the sliders for each constraint. This does not affect the actual constraints in the model, just the UI representation of them. */
	UPROPERTY(EditAnywhere, Category = "Parametric Body", meta = (UIMin = "1.0", UIMax = "5.0", ClampMin = "1.0", ClampMax = "5.0"))
	float ParametersRangeMultiplier = 2.5f;

	/** Show debug lines for active measurements in viewport */
	UPROPERTY(EditAnywhere, Category = "Parametric Body")
	bool bShowMeasurements = true;

	/** Scale the measurement ranges by height to help stay within realistic model proportions */
	UPROPERTY(EditAnywhere, Category = "Parametric Body")
	bool bScaleRangesByHeight = true;

	/** Unlock the measurement ranges to allow for more flexible body proportions */
	UPROPERTY(EditAnywhere, Category = "Parametric Body")
	bool bUnlockBodyRanges = false;

	TArray<FMetaHumanCharacterBodyConstraintItemPtr> BodyConstraintItems;
	TArray<TArray<FVector>> ActiveContours;
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> PreviousBodyState;
};

UENUM()
enum class EMetaHumanCharacterFixedBodyToolHeight : uint8
{
	Short,
	Average,
	Tall
};

UCLASS()
class UMetaHumanCharacterFixedCompatibilityBodyProperties : public UMetaHumanCharacterBodyModelSubToolBase
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, Category = "Body")
	EMetaHumanCharacterFixedBodyToolHeight Height = EMetaHumanCharacterFixedBodyToolHeight::Average;

	UPROPERTY(EditAnywhere, Category = "Body")
	EMetaHumanBodyType MetaHumanBodyType = EMetaHumanBodyType::BlendableBody;

	int32 GetHeightIndex() const { return static_cast<int32>(Height); }
	void UpdateHeightFromBodyType();

	void OnBodyStateChanged();
	void OnMetaHumanBodyTypeChanged();
};

UCLASS()
class UMetaHumanCharacterEditorBodyModelTool : public UMetaHumanCharacterEditorToolWithSubTools
{
	GENERATED_BODY()

public:
	//~Begin UInteractiveTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType InShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	//~End UInteractiveTool interface

	void SetEnabledSubTool(UMetaHumanCharacterBodyModelSubToolBase* InSubTool, bool bInEnabled);

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterParametricBodyProperties> ParametricBodyProperties;
	
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterFixedCompatibilityBodyProperties> FixedCompatibilityBodyProperties;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorHeadAndBodyParameterProperties> BodyParameterProperties;

	bool bNeedsFullUpdate = false;
};


UCLASS()
class UBodyStateChangeTransactor : public UObject, public IMeshStateChangeTransactorInterface, public FMetaHumanCharacterClothVisibilityBase
{
	GENERATED_BODY()

public:
	virtual FSimpleMulticastDelegate& GetStateChangedDelegate(UMetaHumanCharacter* InMetaHumanCharacter) override;

	virtual void CommitShutdownState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, EToolShutdownType InShutdownType, const FText& InCommandChangeDescription) override;
	
	virtual void StoreBeginDragState(UMetaHumanCharacter* InMetaHumanCharacter) override;
	virtual void CommitEndDragState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, const FText& InCommandChangeDescription) override;

	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> GetBeginDragState() const;
	
protected:

	// Hold the state of the character when a dragging operation begins so it can be undone while the tool is active
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BeginDragState;
};

UCLASS()
class UDualStateChangeTransactor : public UObject, public IMeshStateChangeTransactorInterface, public FMetaHumanCharacterClothVisibilityBase
{
	GENERATED_BODY()

public:
	bool IsBodyManipulator(int32 InIndex) const { return InIndex >= 0 && InIndex < NumBodyManipulators; }
	void SetActiveManipulatorIndex(int32 InIndex) { ActiveManipulatorIndex = InIndex; bBlendBothTypes = false; }
	void SetBlendBothTypes() { bBlendBothTypes = true; ActiveManipulatorIndex = INDEX_NONE; }
	void SetNumBodyManipulators(int32 InNum) { NumBodyManipulators = InNum; }

	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> GetBodyBeginDragState() const { return BodyBeginDragState.ToSharedRef(); }
	TSharedRef<FMetaHumanCharacterIdentity::FState> GetFaceBeginDragState() const { return FaceBeginDragState.ToSharedRef(); }

	// IMeshStateChangeTransactorInterface
	virtual FSimpleMulticastDelegate& GetStateChangedDelegate(UMetaHumanCharacter* InMetaHumanCharacter) override;
	virtual void CommitShutdownState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, EToolShutdownType InShutdownType, const FText& InCommandChangeDescription) override;
	virtual void StoreBeginDragState(UMetaHumanCharacter* InMetaHumanCharacter) override;
	virtual void CommitEndDragState(UInteractiveToolManager* InToolManager, UMetaHumanCharacter* InMetaHumanCharacter, const FText& InCommandChangeDescription) override;

protected:
	int32 NumBodyManipulators = 0;
	int32 ActiveManipulatorIndex = INDEX_NONE;
	bool bBlendBothTypes = false;
	bool bDelegatesBound = false;

	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyBeginDragState;
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceBeginDragState;

	FSimpleMulticastDelegate CombinedStateChangedDelegate;
};

UCLASS()
class UMetaHumanCharacterEditorHeadAndBodyBlendToolProperties : public UMetaHumanCharacterEditorMeshBlendToolProperties
{
	GENERATED_BODY()

public:
	bool IsFixedBodyType() const;

	void PerformParametricFit() const;

	/** Blend facial features, proportions, or both */
	UPROPERTY(EditAnywhere, DisplayName = "Blend Space", Category = "BlendTool", meta = (ShowOnlyInnerProperties))
	EBlendOptions FaceBlendOptions = EBlendOptions::Both;

	/** Blend shape, skeleton, or both */
	UPROPERTY(EditAnywhere, DisplayName = "Blend Type", Category = "BlendTool")
	EBodyBlendOptions BodyBlendOptions = EBodyBlendOptions::Both;

	/** Show or hide all manipulator gizmos */
	UPROPERTY(EditAnywhere, DisplayName = "Show Manipulators", Category = "BlendTool")
	bool bShowManipulators = true;

	/** Soft references to the preset characters assigned to each thumbnail slot. */
	UPROPERTY()
	TArray<TSoftObjectPtr<UMetaHumanCharacter>> PresetCharacters;
};

UCLASS()
class UMetaHumanCharacterEditorHeadAndBodyBlendTool : public UMetaHumanCharacterEditorMeshBlendTool
{
	GENERATED_BODY()

public:
	//~Begin UMetaHumanCharacterEditorMeshBlendTool interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType InShutdownType) override;
	virtual void AddMetaHumanCharacterPreset(class UMetaHumanCharacter* InCharacterPreset, int32 InItemIndex) override;
	virtual void RemoveMetaHumanCharacterPreset(int32 InItemIndex) override;
	virtual void BlendToMetaHumanCharacterPreset(UMetaHumanCharacter* InCharacterPreset) override;
	virtual void BlendToMetaHumanCharacterPresetHeadOnly(UMetaHumanCharacter* InCharacterPreset) override;
	virtual void BlendToMetaHumanCharacterPresetBodyOnly(UMetaHumanCharacter* InCharacterPreset) override;
	//~End UMetaHumanCharacterEditorMeshBlendTool interface

protected:
	//~Begin UMetaHumanCharacterEditorMeshEditingTool interface
	virtual void InitStateChangeTransactor() override;
	virtual const FText GetDescription() const override;
	virtual const FText GetCommandChangeDescription() const override;
	virtual const FText GetCommandChangeIntermediateDescription() const override;
	virtual float GetManipulatorScale() const override;
	virtual TArray<FVector3f> GetManipulatorPositions() const override;
	//~End UMetaHumanCharacterEditorMeshEditingTool interface

	//~Begin UMeshSurfacePointTool interface
	virtual void OnBeginDrag(const FRay& InRay) override;
	//~End UMeshSurfacePointTool interface

	//~Begin UMetaHumanCharacterEditorMeshBlendTool interface
	virtual TArray<FVector3f> BlendPresets(int32 InManipulatorIndex, const TArray<float>& Weights) override;
	virtual float GetAncestryCircleRadius() const override;
	//~End UMetaHumanCharacterEditorMeshBlendTool interface

private:
	/** Cached number of body manipulators for index routing. */
	mutable int32 NumBodyManipulators = 0;

	/** Holds the face states of the presets. */
	TArray<TSharedPtr<const FMetaHumanCharacterIdentity::FState>> FacePresetStates;

	/** Holds the body states of the presets. */
	TArray<TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>> BodyPresetStates;
};