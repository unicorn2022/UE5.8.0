// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractionMechanic.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorMeshEditingTools.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorMeshImportTargetScene.h"
#include "Tools/MetaHumanCharacterEditorMeshTargetHitResult.h"

#include "MetaHumanCharacterEditorMeshTarget3DKeyPointMechanic.generated.h"

USTRUCT()
struct FKeyPointData
{
	GENERATED_BODY()
	
	UPROPERTY()
	FMetaHumanCharacterTargetKeyPoints TargetKeyPoints;
		
	UPROPERTY()
	TArray<FName> ManipulatorIDToKeyPointName;
	
	UPROPERTY()
	TArray<EHitMeshType> ManipulatorIDHitType;
	
	UPROPERTY()
	TArray<FVector> ManipulatorPositions;

	// Track type for each keypoint
	UPROPERTY()
	TMap<FName, EKeyPointType> KeyPointTypes;

	void Initialize(const FMetaHumanCharacterTargetKeyPoints& InCharacterTargetKeyPoints);
		
	bool KeyPointIndexExists(const FName& InName) const;
	bool KeyPointTargetExists(const FName& InName) const;
	int32 GetCharacterManipulatorIndex(const FName& InName) const;
	int32 GetTargetManipulatorIndex(const FName& InName) const;
	int32 GetCorrespondingManipulatorIndex(int32 InManipulatorIndex) const;
	
	void AddTargetKeyPoint(const FName& InName, bool bInIsHead, const FVector& InTargetWorldPosition, const FTransform& InWorldToLocalTransform);
	void AddCharacterKeyPoint(const FName& InName, int32 InCharacterVertexID, bool bInIsHead, const FVector& InCharacterPointPosition);
	void RemoveKeyPoint(int32 InManipulatorIndex);
	void RemoveAllCustomKeyPoints();

	void CopyToKeyPointCorrespondences(TMap<int32, FVector3f>& OutKeyPointCorrespondences) const;	
	bool GetKeyPointNameForManipulatorIndex(int32 InManipulatorIndex, FName& OutKeyPointName) const;
	TArray<FVector> RebuildManipulatorPositions(const FTransform& InBodyComponentTransform,
		const FTransform& InHeadComponentTransform,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState);
	
	void UpdateCharacterManipulatorPositions(const TArray<FVector3f>& InBodyVertices, 
		const TArray<FVector3f>& InFaceVertices,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState);
};

UCLASS(MinimalAPI)
class UMeshTarget3DKeyPointMechanic : public UInteractionMechanic
{
	GENERATED_BODY()
	
public:
	virtual void Setup(UInteractiveTool* InParentTool) override;
	virtual void Shutdown() override;
	
	void Initialize(TObjectPtr<UMetaHumanCharacter> InMetaHumanCharacter,
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
		TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState, 
 		TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState,
 		TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> InMeshTargetScene);
	
	void RebuildKeyPointData(TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState, 
	 TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState);
	
	void RecreateManipulators();
	
	void Clear();
	void RemoveAllKeyPoints();
	void DeleteSelectedKeyPoint();
	void DeselectAll();

	void DrawHUD(FCanvas* InCanvas, IToolsContextRenderAPI* InRenderAPI);
	virtual void Render(IToolsContextRenderAPI* InRenderAPI) override;
	void SetCharacterManipulatorsColor(FLinearColor InColor);
	void SetTargetManipulatorsColor(FLinearColor InColor);
	void SetManipulatorsScale(float Scale);

	virtual bool HitTest(const FRay& InRay, FHitResult& OutHit);	
	virtual void OnBeginDrag(bool bCtrlModifier, bool bIsRightClick, bool bShiftModifier);
	virtual bool OnEndDrag(const FMetaHumanTargetHitResult& InHitResult);
	
	bool IsDraggingManipulator() const;
	EHitMeshType GetDraggingManipulatorHitMeshType() const;
	void UpdateDraggingManipulatorPosition(const FMetaHumanTargetHitResult& InHitResult);
	bool GetSelectedManipulatorWorldPosition(FVector& OutPosition) const;
	int32 GetSelectedManipulator() const { return SelectedManipulator; }

	void UpdateComponentTransform();
	void UpdateCharacterManipulatorPositions(const TArray<FVector3f>& InBodyVertices,
		const TArray<FVector3f>& InFaceVertices,
		TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState);

	// Visibility control
	void UpdateKeyPointManipulatorsVisibility();

	// Visibility flags
	bool bShowCustomKeyPoints = true;
	bool bShowPresetBodyKeyPoints = false;

	// True between Initialize() and Clear()/Shutdown(). Used by observers to skip rebuilds issued before the mechanic is ready.
	bool IsInitialized() const { return bIsInitialized; }

protected:
	bool ShouldShowKeyPoint(const FName& KeyPointName) const;

	virtual void CreateManipulator(const FVector& InPosition, bool bInIsCharacter);
	virtual UStaticMesh* GetManipulatorMesh(bool bInIsCharacterMesh) const;
	virtual UMaterialInterface* GetManipulatorMaterial(bool bInIsCharacterMaterial) const;
	virtual float GetManipulatorScale() const;
	
	void SelectManipulator(int32 InManipulatorIndex);
	void SetHoveredManipulator(int32 InManipulatorIndex);
	void SetDraggingManipulator(int32 InManipulatorIndex);
	
	void UpdateManipulatorPosition(const FMetaHumanTargetHitResult& InHitResult, int32 InManipulatorIndex);
	
protected:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacter> MetaHumanCharacter;
		
	UPROPERTY()
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;
	
	UPROPERTY()
	FKeyPointData KeyPointData;
	
	UPROPERTY()
	TObjectPtr<AInternalToolFrameworkActor> ManipulatorsActor;
	
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> ManipulatorComponents;
	
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> MeshTargetScene;
	
	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld;
	
	// Mesh editing property set 
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorMeshEditingToolProperties> MeshEditingToolProperties;
	
	bool bIsInitialized = false;
	bool bCtrlToggledOnBeginDrag = false;
	bool bIsRightClick = false;
	bool bShiftToggledOnBeginDrag = false;
	bool bWasPresetKeyPointClick = false;

	FMetaHumanCharacterTargetKeyPoints BeginDragTargetKeyPoints;
	TMap<FName, EKeyPointType> BeginDragKeyPointTypes;

	// Index of the manipulator hovering
	int32 HoveredManipulator = INDEX_NONE;
	
	// Index of currently dragging manipulator
	int32 DraggingManipulator = INDEX_NONE;
	
	// Index of currently selected manipulator
	int32 SelectedManipulator = INDEX_NONE;
	
	FTransform TargetBodyComponentTransform;
	FTransform TargetHeadComponentTransform;

	float CurrentManipulatorScale = 1.f;
 };