// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorMeshTarget3DKeyPointMechanic.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "MetaHumanCharacterEditorMeshTargetHitResult.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolObjects.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorMeshEditingTools.h"
#include "MetaHumanCharacterEditorMeshImportTool.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterTargetKeyPoints.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"
#include "Chaos/Convex.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterMeshTarget3DKeyPointMechanic"

namespace UE::MetaHuman
{
	FMetaHumanCharacterTargetKeyPoints GetCharacterTargetKeyPoints(UMetaHumanCharacter* MetaHumanCharacter, FMetaHumanCharacterTargetMeshKey TargetMeshKey,TMap<FName, EKeyPointType>& OutKeyPointTypes )
	{
		OutKeyPointTypes.Empty();
		// First we load custom serialized keypoints
		FMetaHumanCharacterTargetKeyPoints TargetKeyPoints;
		if (FMetaHumanCharacterTargetKeyPoints* KeyPoints = MetaHumanCharacter->TargetMeshKeyPointsCollection.PerMeshTargetKeyPoints.Find(TargetMeshKey))
		{
			TargetKeyPoints = *KeyPoints;

			// These are all custom user created keypoints ( we don't serialize presets )
			for (const TPair<FName, int32>& Pair : TargetKeyPoints.CharacterBodyVertexIndexes)
			{
				OutKeyPointTypes.Add(Pair.Key, EKeyPointType::Custom);
			}

			for (const TPair<FName, int32>& Pair : TargetKeyPoints.CharacterHeadVertexIndexes)
			{
				OutKeyPointTypes.Add(Pair.Key, EKeyPointType::Custom);
			}

			// Target positions are persisted unconditionally. Tag any target-only keypoint that has no character-side partner yet so ShouldShowKeyPoint recognises it after a rebuild. 
			// Without this, a freshly placed target-only keypoint becomes invisible after the post-commit broadcast rebuild,
			// until a character-side correspondent is added.
			for (const TPair<FName, FVector3f>& Pair : TargetKeyPoints.TargetBodyPositions)
			{
				if (!OutKeyPointTypes.Contains(Pair.Key))
				{
					OutKeyPointTypes.Add(Pair.Key, EKeyPointType::Custom);
				}
			}

			for (const TPair<FName, FVector3f>& Pair : TargetKeyPoints.TargetHeadPositions)
			{
				if (!OutKeyPointTypes.Contains(Pair.Key))
				{
					OutKeyPointTypes.Add(Pair.Key, EKeyPointType::Custom);
				}
			}
		}

		// Now we get preset keypoints and merge these with custom ones
		if (UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get())
		{
			TMap<FName, int32> PresetBodyKeyPoints = Subsystem->GetPresetBodyKeyPoints(MetaHumanCharacter);

			for (const TPair<FName, int32>& Pair : PresetBodyKeyPoints)
			{
				// Maybe this check isn't needed but just in case
				if (!TargetKeyPoints.CharacterBodyVertexIndexes.Contains(Pair.Key))
				{
					TargetKeyPoints.CharacterBodyVertexIndexes.Add(Pair.Key, Pair.Value);
					OutKeyPointTypes.Add(Pair.Key, EKeyPointType::PresetMH);
				}
			}
		}

		return TargetKeyPoints;
	}
}

// ---------------------------------------------------------------
// FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange ------
// ---------------------------------------------------------------

class FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange : public FToolCommandChange
{
public:
	FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange(
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
		const FName& InKeyPointName,
		FMetaHumanCharacterTargetKeyPoints InOldTargetKeyPoints,
		FMetaHumanCharacterTargetKeyPoints InNewTargetKeyPoints,
		TMap<FName, EKeyPointType> InOldKeyPointTypes,
		TMap<FName, EKeyPointType> InNewKeyPointTypes,
		TNotNull<UInteractiveToolManager*> InToolManager)
		: TargetMeshKey(InTargetMeshKey)
		, KeyPointName(InKeyPointName)
		, OldTargetKeyPoints(InOldTargetKeyPoints)
		, NewTargetKeyPoints(InNewTargetKeyPoints)
		, OldKeyPointTypes(InOldKeyPointTypes)
		, NewKeyPointTypes(InNewKeyPointTypes)
		, ToolManager(InToolManager)
	{
	}

	virtual void Apply(UObject* InObject) override
	{
		ApplyChange(InObject, NewTargetKeyPoints, NewKeyPointTypes);
	}

	virtual void Revert(UObject* InObject) override
	{
		ApplyChange(InObject, OldTargetKeyPoints, OldKeyPointTypes);
	}

	virtual bool HasExpired(UObject* InObject) const override
	{
		// If the ToolManager is not valid anymore it means the asset editor was closed so mark the transaction as expired
		return !ToolManager.IsValid();
	}

protected:
	virtual void ApplyChange(UObject* InObject, const FMetaHumanCharacterTargetKeyPoints& InTargetKeyPoints, const TMap<FName, EKeyPointType>& InKeyPointTypes)
	{
		if (UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject))
		{
			UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshKeypoints(Character, TargetMeshKey, InTargetKeyPoints, InKeyPointTypes);
		}
	}

private:
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;
	FName KeyPointName;
	FMetaHumanCharacterTargetKeyPoints OldTargetKeyPoints;
	FMetaHumanCharacterTargetKeyPoints NewTargetKeyPoints;
	TMap<FName, EKeyPointType> OldKeyPointTypes;
	TMap<FName, EKeyPointType> NewKeyPointTypes;

	TWeakObjectPtr<UInteractiveToolManager> ToolManager;
};

// -----------------------------------------------------
// FKeyPointData implementation ------------------------
// -----------------------------------------------------

void FKeyPointData::Initialize(const FMetaHumanCharacterTargetKeyPoints& InCharacterTargetKeyPoints)
{
	TargetKeyPoints = InCharacterTargetKeyPoints;
	ManipulatorIDToKeyPointName.Empty();
	ManipulatorIDHitType.Empty();
	ManipulatorPositions.Empty();
}

bool FKeyPointData::KeyPointIndexExists(const FName& InName) const
{
	 if (const int32* VertexIndex = TargetKeyPoints.CharacterBodyVertexIndexes.Find(InName))
	 {
	 	return *VertexIndex != INDEX_NONE;
	 }
	
	 if (const int32* VertexIndex = TargetKeyPoints.CharacterHeadVertexIndexes.Find(InName))
	 {
	 	return *VertexIndex != INDEX_NONE;
	 }
 	return false;
 }

 bool FKeyPointData::KeyPointTargetExists(const FName& InName) const
 {
 	return TargetKeyPoints.TargetBodyPositions.Contains(InName) || TargetKeyPoints.TargetHeadPositions.Contains(InName);
 }

int32 FKeyPointData::GetCharacterManipulatorIndex(const FName& InName) const
{
	int32 CharacterManipulatorIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ManipulatorIDToKeyPointName.Num(); Index++)
	{
		if (ManipulatorIDToKeyPointName[Index] == InName)
		{
			if (ManipulatorIDHitType[Index] == EHitMeshType::CharacterBody || ManipulatorIDHitType[Index] == EHitMeshType::CharacterHead)
			{
				CharacterManipulatorIndex = Index;
				break;
			}
		}
	}
	return CharacterManipulatorIndex;
}

int32 FKeyPointData::GetTargetManipulatorIndex(const FName& InName) const
{
	int32 TargetManipulatorIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ManipulatorIDToKeyPointName.Num(); Index++)
	{
		if (ManipulatorIDToKeyPointName[Index] == InName)
		{
			if (ManipulatorIDHitType[Index] == EHitMeshType::TargetBody || ManipulatorIDHitType[Index] == EHitMeshType::TargetHead)
			{
				TargetManipulatorIndex = Index;
				break;
			}
		}
	}
	return TargetManipulatorIndex;
}

int32 FKeyPointData::GetCorrespondingManipulatorIndex(int32 InManipulatorIndex) const
{
	if (InManipulatorIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	
	int32 CorrespondingManipulatorIndex = INDEX_NONE;

	const FName ManipulatorName = ManipulatorIDToKeyPointName[InManipulatorIndex];
	for (int32 Index = 0; Index < ManipulatorIDToKeyPointName.Num(); Index++)
	{
		if (Index == InManipulatorIndex)
		{
			continue;
		}
		
		if (ManipulatorIDToKeyPointName[Index] == ManipulatorName)
		{
			CorrespondingManipulatorIndex = Index;
			break;
		}
	}
	return CorrespondingManipulatorIndex;
}

void FKeyPointData::AddTargetKeyPoint(const FName& InName, bool bInIsHead, const FVector& InTargetWorldPosition, const FTransform& InWorldToLocalTransform)
{
	FVector3f LocalTargetPosition = FVector3f(InWorldToLocalTransform.TransformPosition(InTargetWorldPosition));
	if (bInIsHead)
	{
		TargetKeyPoints.TargetHeadPositions.Add(InName, LocalTargetPosition);
		ManipulatorIDHitType.Add(EHitMeshType::TargetHead);
	}
	else
	{
		TargetKeyPoints.TargetBodyPositions.Add(InName, LocalTargetPosition);
		ManipulatorIDHitType.Add(EHitMeshType::TargetBody);
	}

	ManipulatorIDToKeyPointName.Add(InName);
	ManipulatorPositions.Add(InTargetWorldPosition);

	// Track as custom keypoint (user-placed) if correspondent doesn't exist, in other cases correspondents are the same type as original
	if(!KeyPointTypes.Contains(InName))
	{
		KeyPointTypes.Add(InName, EKeyPointType::Custom);
	}
	
}

void FKeyPointData::AddCharacterKeyPoint(const FName& InName, int32 InCharacterVertexID, bool bInIsHead, const FVector& InCharacterPointPosition)
{
	if (bInIsHead)
	{
		TargetKeyPoints.CharacterHeadVertexIndexes.Add(InName, InCharacterVertexID);
		ManipulatorIDHitType.Add(EHitMeshType::CharacterHead);
	}
	else
	{
		TargetKeyPoints.CharacterBodyVertexIndexes.Add(InName, InCharacterVertexID);
		ManipulatorIDHitType.Add(EHitMeshType::CharacterBody);
	}

	ManipulatorIDToKeyPointName.Add(InName);
	ManipulatorPositions.Add(InCharacterPointPosition);

	// Track as custom keypoint (user-placed) if correspondent doesn't exist, in other cases correspondents are the same type as original
	if(!KeyPointTypes.Contains(InName))
	{
		KeyPointTypes.Add(InName, EKeyPointType::Custom);
	}
	
}

void FKeyPointData::RemoveKeyPoint(int32 InManipulatorIndex)
{
	const FName KeyPointName = ManipulatorIDToKeyPointName[InManipulatorIndex];
	if (ManipulatorIDHitType[InManipulatorIndex] == EHitMeshType::TargetBody)
	{
		TargetKeyPoints.TargetBodyPositions.Remove(KeyPointName);
	}
	else if (ManipulatorIDHitType[InManipulatorIndex] == EHitMeshType::TargetHead)
	{
		TargetKeyPoints.TargetHeadPositions.Remove(KeyPointName);
	}
	else if (ManipulatorIDHitType[InManipulatorIndex] == EHitMeshType::CharacterBody)
	{
		TargetKeyPoints.CharacterBodyVertexIndexes.Remove(KeyPointName);		
	}
	else if (ManipulatorIDHitType[InManipulatorIndex] == EHitMeshType::CharacterHead)
	{
		TargetKeyPoints.CharacterHeadVertexIndexes.Remove(KeyPointName);		
	}
	
	ManipulatorIDToKeyPointName.RemoveAt(InManipulatorIndex);
	ManipulatorIDHitType.RemoveAt(InManipulatorIndex);
	ManipulatorPositions.RemoveAt(InManipulatorIndex);

	// Clean up KeyPointTypes if no correspondent exists
    bool bHasCorrespondent = TargetKeyPoints.TargetBodyPositions.Contains(KeyPointName) ||
                              TargetKeyPoints.TargetHeadPositions.Contains(KeyPointName) ||
                              TargetKeyPoints.CharacterBodyVertexIndexes.Contains(KeyPointName) ||
                              TargetKeyPoints.CharacterHeadVertexIndexes.Contains(KeyPointName);

    if (!bHasCorrespondent)
    {
		KeyPointTypes.Remove(KeyPointName);
    }
}

void FKeyPointData::RemoveAllCustomKeyPoints()
{
    TargetKeyPoints.TargetBodyPositions.Empty();
    TargetKeyPoints.TargetHeadPositions.Empty();

    TArray<FName> CustomKeypointNames;
    for (const TPair<FName, EKeyPointType>& Pair : KeyPointTypes)
    {
        if (Pair.Value == EKeyPointType::Custom)
        {
			CustomKeypointNames.Add(Pair.Key);
        }
    }

    for (const FName& Name : CustomKeypointNames)
    {
		TargetKeyPoints.CharacterBodyVertexIndexes.Remove(Name);
        TargetKeyPoints.CharacterHeadVertexIndexes.Remove(Name);
        KeyPointTypes.Remove(Name);
    }

	TArray<FName> NewManipulatorIDToKeyPointName;
    TArray<EHitMeshType> NewManipulatorIDHitType;
    TArray<FVector> NewManipulatorPositions;

    for (int32 Index = 0; Index < ManipulatorIDToKeyPointName.Num(); ++Index)
    {
		const FName& KeyPointName = ManipulatorIDToKeyPointName[Index];
        EHitMeshType HitType = ManipulatorIDHitType[Index];

        bool bIsCharacterSide = (HitType == EHitMeshType::CharacterBody ||
                                    HitType == EHitMeshType::CharacterHead);
		
		// Preserve the Presets on chracter side
        if (bIsCharacterSide)
        {
			const EKeyPointType* TypePtr = KeyPointTypes.Find(KeyPointName);
            if (TypePtr && *TypePtr == EKeyPointType::PresetMH)
            {
                NewManipulatorIDToKeyPointName.Add(KeyPointName);
                NewManipulatorIDHitType.Add(HitType);
                NewManipulatorPositions.Add(ManipulatorPositions[Index]);
            }
        }
    }

    ManipulatorIDToKeyPointName = MoveTemp(NewManipulatorIDToKeyPointName);
    ManipulatorIDHitType = MoveTemp(NewManipulatorIDHitType);
    ManipulatorPositions = MoveTemp(NewManipulatorPositions);
}

void FKeyPointData::CopyToKeyPointCorrespondences(TMap<int32, FVector3f>& OutKeyPointCorrespondences) const
{
	OutKeyPointCorrespondences.Empty();
	for (const TPair<FName, int32>& NameVertexID : TargetKeyPoints.CharacterBodyVertexIndexes)
	{
		if (const FVector3f* TargetBodyPosition = TargetKeyPoints.TargetBodyPositions.Find(NameVertexID.Key))
		{
			OutKeyPointCorrespondences.Add(NameVertexID.Value, *TargetBodyPosition);
		}
		else if (const FVector3f* TargetHeadPosition = TargetKeyPoints.TargetHeadPositions.Find(NameVertexID.Key))
		{
			OutKeyPointCorrespondences.Add(NameVertexID.Value, *TargetHeadPosition);
		}
	}
}

bool FKeyPointData::GetKeyPointNameForManipulatorIndex(int32 InManipulatorIndex, FName& OutKeyPointName) const
{
	if (ManipulatorIDToKeyPointName.IsValidIndex(InManipulatorIndex))
	{
		OutKeyPointName = ManipulatorIDToKeyPointName[InManipulatorIndex];
		return true;
	}
	return false;
}

TArray<FVector> FKeyPointData::RebuildManipulatorPositions(const FTransform& InBodyComponentTransform,
	const FTransform& InHeadComponentTransform,
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState, 
	TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState)
{
	ManipulatorPositions.Empty();
	ManipulatorIDToKeyPointName.Empty();
	ManipulatorIDHitType.Empty();
	
	int32 NumManipulators = TargetKeyPoints.CharacterBodyVertexIndexes.Num() + TargetKeyPoints.CharacterHeadVertexIndexes.Num() + TargetKeyPoints.TargetBodyPositions.Num() + TargetKeyPoints.TargetHeadPositions.Num();
	ManipulatorPositions.Reserve(NumManipulators);
		
	TArray<FVector3f> BodyVertices = BodyState->GetVerticesAndVertexNormals().Vertices;
	TArray<FVector3f> FaceVertices = FaceState->Evaluate().Vertices;
	
	for (const TPair<FName, int32>& NameCharVertexIndexPair : TargetKeyPoints.CharacterBodyVertexIndexes)
	{
		if (const int32 VertexIndex = NameCharVertexIndexPair.Value; VertexIndex < BodyVertices.Num())
		{
			FVector BodyVertexPos = FVector(BodyVertices[VertexIndex][0], BodyVertices[VertexIndex][2], BodyVertices[VertexIndex][1]);
			ManipulatorPositions.Add(BodyVertexPos);
			ManipulatorIDToKeyPointName.Add(NameCharVertexIndexPair.Key);
			ManipulatorIDHitType.Add(EHitMeshType::CharacterBody);
		}
	}
	
	for (const TPair<FName, int32>& NameCharVertexIndexPair : TargetKeyPoints.CharacterHeadVertexIndexes)
	{
		if (const int32 VertexIndex = NameCharVertexIndexPair.Value; VertexIndex < FaceVertices.Num())
		{
			constexpr int32 LodIndex = 0;
			FVector3f HeadVertexPos = FaceState->GetVertex(FaceVertices, LodIndex, VertexIndex);
			ManipulatorPositions.Add(FVector(HeadVertexPos));
			ManipulatorIDToKeyPointName.Add(NameCharVertexIndexPair.Key);
			ManipulatorIDHitType.Add(EHitMeshType::CharacterHead);
		}
	}
	
	for (const TPair<FName, FVector3f>& NameTargetPosPair : TargetKeyPoints.TargetBodyPositions)
	{
		FVector TargetPos = InBodyComponentTransform.TransformPosition(FVector(NameTargetPosPair.Value));
		ManipulatorPositions.Add(TargetPos);
		ManipulatorIDToKeyPointName.Add(NameTargetPosPair.Key);
		ManipulatorIDHitType.Add(EHitMeshType::TargetBody);
	}
	
	for (const TPair<FName, FVector3f>& NameTargetPosPair : TargetKeyPoints.TargetHeadPositions)
	{
		FVector TargetPos = InHeadComponentTransform.TransformPosition(FVector(NameTargetPosPair.Value));
		ManipulatorPositions.Add(TargetPos);
		ManipulatorIDToKeyPointName.Add(NameTargetPosPair.Key);
		ManipulatorIDHitType.Add(EHitMeshType::TargetHead);
	}
	
	return ManipulatorPositions;
}

void FKeyPointData::UpdateCharacterManipulatorPositions(const TArray<FVector3f>& InBodyVertices, 
	const TArray<FVector3f>& InFaceVertices,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState)
{
	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorIDToKeyPointName.Num(); ManipulatorIndex++)
	{
		if (ManipulatorIDHitType[ManipulatorIndex] == EHitMeshType::CharacterBody || ManipulatorIDHitType[ManipulatorIndex] == EHitMeshType::CharacterHead)
		{
			const FName& ManipulatorName = ManipulatorIDToKeyPointName[ManipulatorIndex];		
			for (const TPair<FName, int32>& NameCharVertexIndexPair : TargetKeyPoints.CharacterBodyVertexIndexes)
			{
				if (NameCharVertexIndexPair.Key == ManipulatorName)
				{
					const int32 BodyVertexIndex = NameCharVertexIndexPair.Value;
					if (BodyVertexIndex < InBodyVertices.Num())
					{
						FVector BodyVertexPos = FVector(InBodyVertices[BodyVertexIndex][0], InBodyVertices[BodyVertexIndex][2], InBodyVertices[BodyVertexIndex][1]);
						ManipulatorPositions[ManipulatorIndex] = BodyVertexPos;
					}
					break;
				}
			}
		
			for (const TPair<FName, int32>& NameCharVertexIndexPair : TargetKeyPoints.CharacterHeadVertexIndexes)
			{
				if (NameCharVertexIndexPair.Key == ManipulatorName)
				{
					const int32 FaceVertexIndex = NameCharVertexIndexPair.Value;
					if (FaceVertexIndex < InFaceVertices.Num())
					{
						constexpr int32 LodIndex = 0;
						FVector FaceVertexPos =  FVector(InFaceState->GetVertex(InFaceVertices, LodIndex, FaceVertexIndex));
						ManipulatorPositions[ManipulatorIndex] = FaceVertexPos;
					}
					break;
				}
			}
		}
		
	}
}

// ---------------------------------------------------------------
// UMeshTarget3DKeyPointMechanic implementation ------------------
// ---------------------------------------------------------------

void UMeshTarget3DKeyPointMechanic::Setup(UInteractiveTool* InParentTool)
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
		// Spawn an actor used as a container for the manipulator components
		ManipulatorsActor = TargetWorld->SpawnActor<AInternalToolFrameworkActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
	
	MeshEditingToolProperties = NewObject<UMetaHumanCharacterEditorMeshEditingToolProperties>(this);
}

void UMeshTarget3DKeyPointMechanic::Shutdown()
{
	Super::Shutdown();
	Clear();
	
	if (ManipulatorsActor != nullptr)
	{
		ManipulatorsActor->Destroy();
		ManipulatorsActor = nullptr;
	}
}

void UMeshTarget3DKeyPointMechanic::Initialize(TObjectPtr<UMetaHumanCharacter> InMetaHumanCharacter,
	const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState, 
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState,
	TObjectPtr<UMetaHumanCharacterEditorMeshImportTargetScene> InMeshTargetScene)
{
	MetaHumanCharacter = InMetaHumanCharacter;
	TargetMeshKey = InTargetMeshKey;
	MeshTargetScene = InMeshTargetScene;
	if (MeshTargetScene)
	{
		TargetBodyComponentTransform = MeshTargetScene->GetBodyComponentTransform();	
		TargetHeadComponentTransform = MeshTargetScene->GetHeadComponentTransform();	
	}	
	RebuildKeyPointData(InBodyState, InFaceState);
	bIsInitialized = true;
}

void UMeshTarget3DKeyPointMechanic::RebuildKeyPointData(TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState, 
                                                        TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState)
{
	FName SelectedName;
	EHitMeshType SelectedMeshType = EHitMeshType::None; 
	if (SelectedManipulator != INDEX_NONE)
	{
		// Keep existing selected manipulator
		SelectedName = KeyPointData.ManipulatorIDToKeyPointName[SelectedManipulator];
		SelectedMeshType = KeyPointData.ManipulatorIDHitType[SelectedManipulator];
	}
		
	SetHoveredManipulator(INDEX_NONE);
	SetDraggingManipulator(INDEX_NONE);
	SelectedManipulator = INDEX_NONE;	
	KeyPointData.Initialize(UE::MetaHuman::GetCharacterTargetKeyPoints(MetaHumanCharacter, TargetMeshKey, KeyPointData.KeyPointTypes));
	KeyPointData.RebuildManipulatorPositions(TargetBodyComponentTransform, TargetHeadComponentTransform, InBodyState, InFaceState);
	RecreateManipulators();

	if (SelectedMeshType != EHitMeshType::None)
	{
		const int32 NewSelectedIndex = (SelectedMeshType == EHitMeshType::TargetBody || SelectedMeshType == EHitMeshType::TargetHead) ? KeyPointData.GetTargetManipulatorIndex(SelectedName) : KeyPointData.GetCharacterManipulatorIndex(SelectedName);
		SelectManipulator(NewSelectedIndex);
	}
}

void UMeshTarget3DKeyPointMechanic::RecreateManipulators()
{
	if (ManipulatorsActor)
	{
		for (UStaticMeshComponent* ManipulatorComponent : ManipulatorComponents)
		{
			if (ManipulatorComponent)
			{
				ManipulatorComponent->UnregisterComponent();
				ManipulatorComponent->DestroyComponent();
			}
		}

		ManipulatorComponents.Empty();
		SelectManipulator(INDEX_NONE);

		// We do get a tiny bit of overhead for creating all manipulators but code readability is better when SelectedIndex in ManipulatorComponents and
		// KeyPointData indexes align, other solution would be to map them but I wouldn't complicate it that much for minimal performance gains.
		for (int32 ManipulatorIndex = 0; ManipulatorIndex < KeyPointData.ManipulatorPositions.Num(); ++ManipulatorIndex)
        {
            const FName& KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[ManipulatorIndex];
			const FVector& ManipulatorPosition = KeyPointData.ManipulatorPositions[ManipulatorIndex];
            bool bIsCharacter = KeyPointData.ManipulatorIDHitType[ManipulatorIndex] == EHitMeshType::CharacterHead
                               || KeyPointData.ManipulatorIDHitType[ManipulatorIndex] == EHitMeshType::CharacterBody;

            CreateManipulator(ManipulatorPosition, bIsCharacter);

            // Set visibility based on data from KeyPointData
            bool bShouldShow = ShouldShowKeyPoint(KeyPointName);
            ManipulatorComponents[ManipulatorIndex]->SetVisibility(bShouldShow);
        }
	}
}

void UMeshTarget3DKeyPointMechanic::Clear()
{
	FMetaHumanCharacterTargetKeyPoints EmptyKeyPoints;
	KeyPointData.Initialize(EmptyKeyPoints);
	HoveredManipulator = INDEX_NONE;
	DraggingManipulator = INDEX_NONE;
	SelectedManipulator = INDEX_NONE;
	RecreateManipulators(); // Clear Manipulators
	bIsInitialized = false;
}

void UMeshTarget3DKeyPointMechanic::RemoveAllKeyPoints()
{
	if (!bIsInitialized)
	{
		return;
	}

	// Capture the pre-delete snapshot so the mutation can be undone.
	FMetaHumanCharacterTargetKeyPoints OldTargetKeyPoints = KeyPointData.TargetKeyPoints;
	TMap<FName, EKeyPointType> OldKeyPointTypes = KeyPointData.KeyPointTypes;

	// No-op early out if there are no Custom entries to remove. Prevents spurious undo stack entries.
	bool bHasAnyCustom = false;
	for (const TPair<FName, EKeyPointType>& Pair : KeyPointData.KeyPointTypes)
	{
		if (Pair.Value == EKeyPointType::Custom)
		{
			bHasAnyCustom = true;
			break;
		}
	}
	if (!bHasAnyCustom && KeyPointData.TargetKeyPoints.TargetBodyPositions.IsEmpty() && KeyPointData.TargetKeyPoints.TargetHeadPositions.IsEmpty())
	{
		return;
	}

	KeyPointData.RemoveAllCustomKeyPoints();

	HoveredManipulator = INDEX_NONE;
	DraggingManipulator = INDEX_NONE;
	SelectedManipulator = INDEX_NONE;

	RecreateManipulators();

	// Persist the cleared state to the asset. Without this, the next rebuild (e.g. body-state change
	// from conform) would repopulate the keypoints from the stale asset data and they'd reappear.
	if (MetaHumanCharacter)
	{
		UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshKeypoints(MetaHumanCharacter, TargetMeshKey, KeyPointData.TargetKeyPoints, KeyPointData.KeyPointTypes);

		// Wire the clear into the transaction system so Ctrl+Z restores the deleted keypoints.
		UInteractiveToolManager* ToolManager = GetParentTool()->GetToolManager();
		TUniquePtr<FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange>(
			TargetMeshKey,
			FName(),
			OldTargetKeyPoints,
			KeyPointData.TargetKeyPoints,
			OldKeyPointTypes,
			KeyPointData.KeyPointTypes,
			ToolManager);
		ToolManager->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), LOCTEXT("RemoveAllKeyPointsCommandChange", "Delete All Key Points"));
	}
}

void UMeshTarget3DKeyPointMechanic::DeleteSelectedKeyPoint()
{
	if (SelectedManipulator == INDEX_NONE)
	{
		return;
	}

	// Capture state before deletion for undo
	FMetaHumanCharacterTargetKeyPoints OldTargetKeyPoints = KeyPointData.TargetKeyPoints;
	TMap<FName, EKeyPointType> OldKeyPointTypes = KeyPointData.KeyPointTypes;

	const FName KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[SelectedManipulator];
	bool bKeypointRemoved = false;

	if (KeyPointData.ManipulatorIDHitType[SelectedManipulator] == EHitMeshType::TargetBody)
	{
		KeyPointData.TargetKeyPoints.TargetBodyPositions.Remove(KeyPointName);
		bKeypointRemoved = true;
	}
	else if (KeyPointData.ManipulatorIDHitType[SelectedManipulator] == EHitMeshType::TargetHead)
	{
		KeyPointData.TargetKeyPoints.TargetHeadPositions.Remove(KeyPointName);
		bKeypointRemoved = true;
	}
	else if (KeyPointData.ManipulatorIDHitType[SelectedManipulator] == EHitMeshType::CharacterBody)
	{
		KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Remove(KeyPointName);
		bKeypointRemoved = true;
	}
	else if (KeyPointData.ManipulatorIDHitType[SelectedManipulator] == EHitMeshType::CharacterHead)
	{
		KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Remove(KeyPointName);
		bKeypointRemoved = true;
	}

	if (bKeypointRemoved)
	{
		KeyPointData.ManipulatorIDToKeyPointName.RemoveAt(SelectedManipulator);
		KeyPointData.ManipulatorIDHitType.RemoveAt(SelectedManipulator);
		KeyPointData.ManipulatorPositions.RemoveAt(SelectedManipulator);

		bool bStillExists = KeyPointData.TargetKeyPoints.TargetBodyPositions.Contains(KeyPointName) ||
		                    KeyPointData.TargetKeyPoints.TargetHeadPositions.Contains(KeyPointName) ||
		                    KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Contains(KeyPointName) ||
		                    KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Contains(KeyPointName);

		if (!bStillExists)
		{
			KeyPointData.KeyPointTypes.Remove(KeyPointName);
		}

		RecreateManipulators();

		// Commit the change
		UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshKeypoints(MetaHumanCharacter, TargetMeshKey, KeyPointData.TargetKeyPoints, KeyPointData.KeyPointTypes);

		UInteractiveToolManager* ToolManager = GetParentTool()->GetToolManager();
		TUniquePtr<FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange>(
			TargetMeshKey,
			KeyPointName,
			OldTargetKeyPoints,
			KeyPointData.TargetKeyPoints,
			OldKeyPointTypes,
			KeyPointData.KeyPointTypes,
			ToolManager);
		ToolManager->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), LOCTEXT("DeleteTargetKeyPointCommandChange", "Delete Key Point"));

		UpdateKeyPointManipulatorsVisibility();
	}
}

void UMeshTarget3DKeyPointMechanic::DrawHUD(FCanvas* InCanvas, IToolsContextRenderAPI* InRenderAPI)
{
	if (!InCanvas || !InRenderAPI)
	{
		return;
	}

	const FSceneView* SceneView = InRenderAPI->GetSceneView();
	if (!SceneView)
	{
		return;
	}

	// Choose a font and color
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FLinearColor TextColor = FLinearColor::White;
	const float DPIScale = InCanvas->GetDPIScale();

	for (int32 ManipulatorIndex = 0; ManipulatorIndex < KeyPointData.ManipulatorPositions.Num(); ++ManipulatorIndex)
	{
		if (!ShouldShowKeyPoint(KeyPointData.ManipulatorIDToKeyPointName[ManipulatorIndex]))
		{
			continue;
		}
		
		FVector ManipulatorPosition = KeyPointData.ManipulatorPositions[ManipulatorIndex];		
		FVector2D Manipulator2DPixelLocation;
		if (SceneView->WorldToPixel(ManipulatorPosition, Manipulator2DPixelLocation))
		{
			const FVector2D ScreenOffset(12.0f, -12.0f);
			FVector2D Text2DLocation = (Manipulator2DPixelLocation / DPIScale) + ScreenOffset;
			const FString Label = KeyPointData.ManipulatorIDToKeyPointName[ManipulatorIndex].ToString();
			
			InCanvas->DrawShadowedString(Text2DLocation.X, Text2DLocation.Y, *Label, Font, TextColor);
		}
	}
}

void UMeshTarget3DKeyPointMechanic::Render(IToolsContextRenderAPI* InRenderAPI)
{
	const UMetaHumanCharacterEditorMeshImportTool* OwnerTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(ParentTool.Get());
	const UMetaHumanCharacterEditorMeshImportToolProperties* ToolProperties = OwnerTool ? OwnerTool->GetMeshImportProperties() : nullptr;
	if (!ToolProperties) // !ToolProperties->bShowConnectionLines || SelectedManipulator == INDEX_NONE
	{
		return;
	}

	FPrimitiveDrawInterface* PDI = InRenderAPI->GetPrimitiveDrawInterface();
	if(!PDI)
	{
		return;
	}

	if(ToolProperties->bShowConnectionLines)
	{
		TSet<int32> ProcessedManipulators;

        for (int32 ManipulatorIndex = 0; ManipulatorIndex < KeyPointData.ManipulatorPositions.Num(); ++ManipulatorIndex)
        {
            // Skip if already processed (avoid drawing same line twice)
            if (ProcessedManipulators.Contains(ManipulatorIndex))
			{
				continue;
			}

			int32 CorrespondingManipulator = KeyPointData.GetCorrespondingManipulatorIndex(ManipulatorIndex);
			// Don't show line if Keypoint shouldn't be visible
			if(!ShouldShowKeyPoint(KeyPointData.ManipulatorIDToKeyPointName[ManipulatorIndex]))
			{
				// Mark both as processed
                ProcessedManipulators.Add(ManipulatorIndex);
                ProcessedManipulators.Add(CorrespondingManipulator);
				continue;
			}
            

            if (CorrespondingManipulator != INDEX_NONE)
            {
                FVector Start = KeyPointData.ManipulatorPositions[ManipulatorIndex];
                FVector End = KeyPointData.ManipulatorPositions[CorrespondingManipulator];

                PDI->DrawLine(Start, End, FLinearColor(1.0f, 0.2f, 0.2f), SDPG_MAX, 0.0f);

                // Mark both as processed
                ProcessedManipulators.Add(ManipulatorIndex);
                ProcessedManipulators.Add(CorrespondingManipulator);
            }
        }
          return; 
	}

	if(SelectedManipulator != INDEX_NONE)
	{
		if(ShouldShowKeyPoint(KeyPointData.ManipulatorIDToKeyPointName[SelectedManipulator]))
		{
			if (int32 CorrespondingManipulator = KeyPointData.GetCorrespondingManipulatorIndex(SelectedManipulator); CorrespondingManipulator != INDEX_NONE)
			{
				FVector Start = KeyPointData.ManipulatorPositions[SelectedManipulator];
				FVector End = KeyPointData.ManipulatorPositions[CorrespondingManipulator];
				PDI->DrawLine(Start, End, FLinearColor(1.0f, 0.2f, 0.2f), SDPG_MAX, 0.0f);
			}
		}
	}
}

void UMeshTarget3DKeyPointMechanic::SetCharacterManipulatorsColor(FLinearColor InColor)
{
	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorComponents.Num(); ++ManipulatorIndex)
	{
		EHitMeshType HitType = KeyPointData.ManipulatorIDHitType[ManipulatorIndex];
		if (HitType == EHitMeshType::CharacterBody || HitType == EHitMeshType::CharacterHead)
		{
			UStaticMeshComponent* ManipulatorComponent = ManipulatorComponents[ManipulatorIndex];
			UMaterialInstanceDynamic* ManipulatorMaterialInstance = CastChecked<UMaterialInstanceDynamic>(ManipulatorComponent->GetMaterial(0));
			ManipulatorMaterialInstance->SetVectorParameterValue(TEXT("Color"), InColor);
		}
	}
}

void UMeshTarget3DKeyPointMechanic::SetTargetManipulatorsColor(FLinearColor InColor)
{
	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorComponents.Num(); ++ManipulatorIndex)
	{
		EHitMeshType HitType = KeyPointData.ManipulatorIDHitType[ManipulatorIndex];
		if (HitType == EHitMeshType::TargetBody || HitType == EHitMeshType::TargetHead)
		{
			UStaticMeshComponent* ManipulatorComponent = ManipulatorComponents[ManipulatorIndex];
			UMaterialInstanceDynamic* ManipulatorMaterialInstance = CastChecked<UMaterialInstanceDynamic>(ManipulatorComponent->GetMaterial(0));
			ManipulatorMaterialInstance->SetVectorParameterValue(TEXT("Color"), InColor);
		}
	}
}

void UMeshTarget3DKeyPointMechanic::SetManipulatorsScale(float Scale)
{
	CurrentManipulatorScale = Scale;
	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorComponents.Num(); ++ManipulatorIndex)
	{
		const float GizmoScale = GetManipulatorScale();
		UStaticMeshComponent* ManipulatorComponent = ManipulatorComponents[ManipulatorIndex];
		ManipulatorComponent->SetWorldScale3D(FVector{ GizmoScale * Scale });
	}
}

bool UMeshTarget3DKeyPointMechanic::HitTest(const FRay& InRay, FHitResult& OutHit)
{	
	const FVector StartPoint = InRay.Origin;
	const FVector EndPoint = InRay.PointAt(HALF_WORLD_MAX);
	
	int32 NewHoveredManipulatorIndex = INDEX_NONE;
	float Distance = -1;
	
	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorComponents.Num(); ++ManipulatorIndex)
	{
		UStaticMeshComponent* ManipulatorComponent = ManipulatorComponents[ManipulatorIndex];

		// Skip hidden manipulators
		if (!ManipulatorComponent || !ManipulatorComponent->IsVisible())
		{
			continue;
		}

		const bool bTraceComplex = false;
		if (ManipulatorComponent->LineTraceComponent(OutHit, StartPoint, EndPoint, FCollisionQueryParams(SCENE_QUERY_STAT(HitTest), bTraceComplex)))
		{
			// Store the index of the manipulator that was hit and the hit distance, which is
			// used to calculate the movement delta of the gizmo translation
			if(Distance == -1 || OutHit.Distance < Distance)
			{
				NewHoveredManipulatorIndex = ManipulatorIndex;
				Distance = OutHit.Distance;
			}
		}
	}
	
	SetHoveredManipulator(NewHoveredManipulatorIndex);	
	return NewHoveredManipulatorIndex != INDEX_NONE;
}

void UMeshTarget3DKeyPointMechanic::OnBeginDrag(bool bCtrlModifier, bool bRightClickModifier, bool bShiftModifier)
{
	if (!bIsInitialized)
	{
		return;
	}

	// Check if trying to interact with a preset MH keypoint on character side
    bool bIsPresetMHKeyPoint = false;
    if (HoveredManipulator != INDEX_NONE)
    {
        const FName& KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[HoveredManipulator];
        const EKeyPointType* TypePtr = KeyPointData.KeyPointTypes.Find(KeyPointName);
        EHitMeshType HitType = KeyPointData.ManipulatorIDHitType[HoveredManipulator];
        bool bIsCharacterSide = (HitType == EHitMeshType::CharacterBody || HitType == EHitMeshType::CharacterHead);

        bIsPresetMHKeyPoint = (TypePtr && *TypePtr == EKeyPointType::PresetMH && bIsCharacterSide);
    }

    // If it's a preset keypoint, only allow selection (no dragging/removal)
	if (bIsPresetMHKeyPoint)
    {
		// RMB on character preset keypoint - ignore it completely (RMB is for target mesh only)
		if (bRightClickModifier)
		{
            bCtrlToggledOnBeginDrag = false;
            bIsRightClick = false;
            bShiftToggledOnBeginDrag = false;
            SetDraggingManipulator(INDEX_NONE);
            bWasPresetKeyPointClick = false;
            return;
		}

		if (bCtrlModifier)
		{
            // Ctrl+LMB on preset sphere: Deselect preset, don't add keypoint (hovering over sphere, not mesh)
            SelectedManipulator = INDEX_NONE;
            bWasPresetKeyPointClick = false;
            return;
		}
		else
		{
            // Plain LMB on preset: Select for visual reference
            SelectManipulator(HoveredManipulator);
            bWasPresetKeyPointClick = true;
			return;
		}
    }
    bWasPresetKeyPointClick = false;

	// Check if preset is SELECTED but clicking elsewhere on mesh (not hovering over preset sphere)
	// This handles: Preset selected + Ctrl+LMB on CHARACTER mesh. Deselect preset and allow adding new custom keypoint
	// but preserve selection for Ctrl+RMB on TARGET mesh (correspondence/auto-pairing needs the selection)
	if (SelectedManipulator != INDEX_NONE && HoveredManipulator == INDEX_NONE && bCtrlModifier && !bRightClickModifier)
	{
		const FName& SelectedKeyPointName = KeyPointData.ManipulatorIDToKeyPointName[SelectedManipulator];
		const EKeyPointType* SelectedTypePtr = KeyPointData.KeyPointTypes.Find(SelectedKeyPointName);
		EHitMeshType SelectedHitType = KeyPointData.ManipulatorIDHitType[SelectedManipulator];
		bool bSelectedIsCharacterSide = (SelectedHitType == EHitMeshType::CharacterBody || SelectedHitType == EHitMeshType::CharacterHead);
		bool bSelectedIsPreset = (SelectedTypePtr && *SelectedTypePtr == EKeyPointType::PresetMH && bSelectedIsCharacterSide);

		if (bSelectedIsPreset)
		{
			// Preset is selected but user is Ctrl+LEFT-clicking on character mesh (not the preset sphere)
			// User wants to add a new custom keypoint, so unselect the preset and continue to normal add flow
			// Ctrl+RIGHT-click (target mesh) keeps selection for correspondence/auto-pairing
			SelectedManipulator = INDEX_NONE;
			// Don't return, allow normal flow to add new custom keypoint
		}
	}

    if (HoveredManipulator != INDEX_NONE)
    {
        const EHitMeshType HitType = KeyPointData.ManipulatorIDHitType[HoveredManipulator];
        const bool bIsCharacterSide = (HitType == EHitMeshType::CharacterBody || HitType == EHitMeshType::CharacterHead);
        const bool bIsTargetSide = (HitType == EHitMeshType::TargetBody || HitType == EHitMeshType::TargetHead);
        const bool bButtonMatchesSide = (bIsCharacterSide && !bRightClickModifier) || (bIsTargetSide && bRightClickModifier);

        if (!bButtonMatchesSide)
        {
            // Reset drag state so OnEndDrag sees no pending drag and no Ctrl action.
            bCtrlToggledOnBeginDrag = false;
            bIsRightClick = false;
            bShiftToggledOnBeginDrag = false;
            SetDraggingManipulator(INDEX_NONE);
            return;
        }
    }

	bCtrlToggledOnBeginDrag = bCtrlModifier;
	bIsRightClick = bRightClickModifier;
	bShiftToggledOnBeginDrag = bShiftModifier;
	SetDraggingManipulator(HoveredManipulator);
	BeginDragTargetKeyPoints = KeyPointData.TargetKeyPoints;
	BeginDragKeyPointTypes = KeyPointData.KeyPointTypes;

	if (!bCtrlToggledOnBeginDrag)
	{
		SelectManipulator(HoveredManipulator);
	}
}

bool UMeshTarget3DKeyPointMechanic::OnEndDrag(const FMetaHumanTargetHitResult& InHitResult)
{
	if (!bIsInitialized)
	{
		return false;
	}
	
	bool bCommitCommandChange = false;
	FText CommandChangeDescription;
	FName KeyPointName;
	
	if (bCtrlToggledOnBeginDrag)
	{
		if (bShiftToggledOnBeginDrag && DraggingManipulator != INDEX_NONE)
		{
			KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[DraggingManipulator];
			bool bKeypointRemoved = false;

			if (KeyPointData.ManipulatorIDHitType[DraggingManipulator] == EHitMeshType::TargetBody && bIsRightClick)
			{
				KeyPointData.TargetKeyPoints.TargetBodyPositions.Remove(KeyPointName);
				bKeypointRemoved = true;
			}
			else if (KeyPointData.ManipulatorIDHitType[DraggingManipulator] == EHitMeshType::TargetHead && bIsRightClick)
			{
				KeyPointData.TargetKeyPoints.TargetHeadPositions.Remove(KeyPointName);
				bKeypointRemoved = true;
			}
			else if (KeyPointData.ManipulatorIDHitType[DraggingManipulator] == EHitMeshType::CharacterBody && !bIsRightClick)
			{
				KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Remove(KeyPointName);
				bKeypointRemoved = true;
			}
			else if (KeyPointData.ManipulatorIDHitType[DraggingManipulator] == EHitMeshType::CharacterHead && !bIsRightClick)
			{
				KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Remove(KeyPointName);	
				bKeypointRemoved = true;
			}
			
			if(bKeypointRemoved)
			{
				KeyPointData.ManipulatorIDToKeyPointName.RemoveAt(DraggingManipulator);
				KeyPointData.ManipulatorIDHitType.RemoveAt(DraggingManipulator);
				KeyPointData.ManipulatorPositions.RemoveAt(DraggingManipulator);

				bool bStillExists = KeyPointData.TargetKeyPoints.TargetBodyPositions.Contains(KeyPointName) ||
                          KeyPointData.TargetKeyPoints.TargetHeadPositions.Contains(KeyPointName) ||
                          KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Contains(KeyPointName) ||
                          KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Contains(KeyPointName);

				if (!bStillExists)
				{
					KeyPointData.KeyPointTypes.Remove(KeyPointName);
				}
			
				RecreateManipulators();
			
				//Remove key point command change
				CommandChangeDescription = LOCTEXT("RemoveTargetKeyPointCommandChange", "Remove Key Point");
				bCommitCommandChange = true;
			}

		}
		else
		{
			if (SelectedManipulator != INDEX_NONE)
			{
				bool SelectedIsCharacter = KeyPointData.ManipulatorIDHitType[SelectedManipulator] == EHitMeshType::CharacterBody || KeyPointData.ManipulatorIDHitType[SelectedManipulator] == EHitMeshType::CharacterHead;
				bool HitMeshIsCharacter = InHitResult.HitMeshType == EHitMeshType::CharacterBody || InHitResult.HitMeshType == EHitMeshType::CharacterHead;
				FName SelectedKeyPointName = KeyPointData.ManipulatorIDToKeyPointName[SelectedManipulator];

				if(SelectedIsCharacter == HitMeshIsCharacter)
				{
					KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[SelectedManipulator];
				}
				else
				{
				    bool bCorrespondenceExists = false;
					if (HitMeshIsCharacter)
					{
						bCorrespondenceExists = (KeyPointData.GetCharacterManipulatorIndex(SelectedKeyPointName) != INDEX_NONE);
					}
					else
					{
						bCorrespondenceExists = (KeyPointData.GetTargetManipulatorIndex(SelectedKeyPointName) != INDEX_NONE);
					}

					if (!bCorrespondenceExists)
					{
						// No correspondence
						KeyPointName = SelectedKeyPointName;
					}
				}
			}
			if (KeyPointName.IsNone())
			{
				int32 MaxNumber = 0;
				for (const FName& OtherName : KeyPointData.ManipulatorIDToKeyPointName)
				{
					// Custom keypoints are numerated so that is the only case where we have to caluculate max number, we should skip presets
					FString NameStr = OtherName.ToString();
					if (NameStr.IsNumeric())
					{
						int32 OtherNum = FCString::Atoi(*OtherName.ToString());
						MaxNumber = FMath::Max(MaxNumber, OtherNum);
					}
				}
				KeyPointName = FName(FString::FromInt(MaxNumber + 1));
			}
			
			// Right-click only creates keypoints on the target mesh,
			// left-click only creates keypoints on the character (MetaHuman) mesh.
			const bool bHitTarget = InHitResult.HitMeshType == EHitMeshType::TargetBody || InHitResult.HitMeshType == EHitMeshType::TargetHead;
			const bool bHitCharacter = InHitResult.HitMeshType == EHitMeshType::CharacterBody || InHitResult.HitMeshType == EHitMeshType::CharacterHead;

			if (bHitTarget && bIsRightClick)
			{
				if (int32 ManipulatorIndex = KeyPointData.GetTargetManipulatorIndex(KeyPointName); ManipulatorIndex != INDEX_NONE)
				{
					UpdateManipulatorPosition(InHitResult, ManipulatorIndex);
				}
				else
				{
					const bool bIsTargetHead = InHitResult.HitMeshType == EHitMeshType::TargetHead;
					FTransform WorldToLocalTransform = bIsTargetHead ? TargetHeadComponentTransform.Inverse() : TargetBodyComponentTransform.Inverse();
					KeyPointData.AddTargetKeyPoint(KeyPointName, bIsTargetHead, InHitResult.HitResult.Location, WorldToLocalTransform);
					
					CreateManipulator(InHitResult.HitResult.Location, false);
					int32 AddedIndex = ManipulatorComponents.Num() - 1;
					SelectManipulator(AddedIndex);
				}
			}
			else if (bHitCharacter && !bIsRightClick)
			{	
				// If selected manipulator and selected manipulator on character and corespo
				if (int32 ManipulatorIndex = KeyPointData.GetCharacterManipulatorIndex(KeyPointName); ManipulatorIndex != INDEX_NONE)
				{
					UpdateManipulatorPosition(InHitResult, ManipulatorIndex);
				}
				else
				{
					const bool bIsCharacterHead = InHitResult.HitMeshType == EHitMeshType::CharacterHead;
					KeyPointData.AddCharacterKeyPoint(KeyPointName, InHitResult.HitVertexID, bIsCharacterHead, InHitResult.HitResult.Location);
	
					CreateManipulator(InHitResult.HitResult.Location, true);
					int32 AddedIndex = ManipulatorComponents.Num() - 1;
					SelectManipulator(AddedIndex);
				}
			}

			// Only commit if a keypoint was actually added/moved (button matched the mesh side)
			if ((bHitTarget && bIsRightClick) || (bHitCharacter && !bIsRightClick))
			{
				//Add key point command change
				CommandChangeDescription = LOCTEXT("AddTargetKeyPointCommandChange", "Add Key Point");
				bCommitCommandChange = true;
			}
		}
	}
	else if (DraggingManipulator != INDEX_NONE)
	{
		if (BeginDragTargetKeyPoints != KeyPointData.TargetKeyPoints)
		{
			//Move key point command change
			CommandChangeDescription = LOCTEXT("MoveTargetKeyPointCommandChange", "Move Key Point");
			KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[DraggingManipulator];
			bCommitCommandChange = true;
		}
	}
	// Handle deselection on empty click without modifiers.
	else if (!bWasPresetKeyPointClick &&
	         !bCtrlToggledOnBeginDrag && !bShiftToggledOnBeginDrag &&
	         InHitResult.HitMeshType == EHitMeshType::None && SelectedManipulator != INDEX_NONE)
	{
		// Clicked empty space without modifiers - deselect
		DeselectAll();
	}
	bWasPresetKeyPointClick = false;

	if (bCommitCommandChange)
	{
		UMetaHumanCharacterEditorSubsystem::Get()->CommitTargetMeshKeypoints(MetaHumanCharacter, TargetMeshKey, KeyPointData.TargetKeyPoints, KeyPointData.KeyPointTypes);

		UInteractiveToolManager* ToolManager = GetParentTool()->GetToolManager();
		TUniquePtr<FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange> CommandChange = MakeUnique<FMetaHumanCharacterEditorChangeTargetKeyPointCommandChange>(TargetMeshKey,
			KeyPointName,
			BeginDragTargetKeyPoints,
			KeyPointData.TargetKeyPoints,
			BeginDragKeyPointTypes,
			KeyPointData.KeyPointTypes,
			ToolManager);
		ToolManager->GetContextTransactionsAPI()->AppendChange(MetaHumanCharacter, MoveTemp(CommandChange), CommandChangeDescription);
	}
	
	SetDraggingManipulator(INDEX_NONE);
	UpdateKeyPointManipulatorsVisibility();
	return true;
}

bool UMeshTarget3DKeyPointMechanic::IsDraggingManipulator() const
{
	return DraggingManipulator != INDEX_NONE;
}

bool UMeshTarget3DKeyPointMechanic::GetSelectedManipulatorWorldPosition(FVector& OutPosition) const
{
	if (SelectedManipulator != INDEX_NONE && SelectedManipulator < ManipulatorComponents.Num())
	{
		OutPosition = ManipulatorComponents[SelectedManipulator]->GetComponentLocation();
		return true;
	}
	return false;
}

EHitMeshType UMeshTarget3DKeyPointMechanic::GetDraggingManipulatorHitMeshType() const
{
	EHitMeshType HitMeshType = EHitMeshType::None;
	if (DraggingManipulator != INDEX_NONE && DraggingManipulator < KeyPointData.ManipulatorIDHitType.Num())
	{
		HitMeshType =  KeyPointData.ManipulatorIDHitType[DraggingManipulator];
	}
	return HitMeshType;
}

void UMeshTarget3DKeyPointMechanic::UpdateDraggingManipulatorPosition(const FMetaHumanTargetHitResult& InHitResult)
{
	if (DraggingManipulator != INDEX_NONE)
	{
		UpdateManipulatorPosition(InHitResult, DraggingManipulator);
	}
}

void UMeshTarget3DKeyPointMechanic::UpdateManipulatorPosition(const FMetaHumanTargetHitResult& InHitResult, int32 InManipulatorIndex)
{
	FName ManipulatorName = KeyPointData.ManipulatorIDToKeyPointName[InManipulatorIndex];
		
		if (KeyPointData.ManipulatorIDHitType[InManipulatorIndex] != InHitResult.HitMeshType)
		{
			// Deal with change from face to body hit or vice versa
			KeyPointData.ManipulatorIDHitType[InManipulatorIndex] = InHitResult.HitMeshType;
			if (InHitResult.HitMeshType == EHitMeshType::TargetBody)
			{
				if (int32 Removed = KeyPointData.TargetKeyPoints.TargetHeadPositions.Remove(ManipulatorName); Removed > 0)
				{
					FTransform WorldToLocalTransform = TargetBodyComponentTransform.Inverse();
					FVector3f LocalTargetPosition = FVector3f(WorldToLocalTransform.TransformPosition(InHitResult.HitResult.Location));
					KeyPointData.TargetKeyPoints.TargetBodyPositions.Add(ManipulatorName, LocalTargetPosition);
				}
			}
			else if (InHitResult.HitMeshType == EHitMeshType::TargetHead)
			{
				if (int32 Removed = KeyPointData.TargetKeyPoints.TargetBodyPositions.Remove(ManipulatorName); Removed > 0)
				{
					FTransform WorldToLocalTransform = TargetHeadComponentTransform.Inverse();
					FVector3f LocalTargetPosition = FVector3f(WorldToLocalTransform.TransformPosition(InHitResult.HitResult.Location));
					KeyPointData.TargetKeyPoints.TargetHeadPositions.Add(ManipulatorName, LocalTargetPosition);
				}
			}
			else if (InHitResult.HitMeshType == EHitMeshType::CharacterBody)
			{
				if (int32 Removed = KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Remove(ManipulatorName); Removed > 0)
				{
					KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Add(ManipulatorName, InHitResult.HitVertexID);
				}
			}
			else if (InHitResult.HitMeshType == EHitMeshType::CharacterHead)
			{
				if (int32 Removed = KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Remove(ManipulatorName); Removed > 0)
				{
					KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Add(ManipulatorName, InHitResult.HitVertexID);
				}
			}
		}
		
		if (InHitResult.HitMeshType == EHitMeshType::TargetBody)
		{
			if (FVector3f* TargetPos = KeyPointData.TargetKeyPoints.TargetBodyPositions.Find(ManipulatorName))
			{
				FTransform WorldToLocalTransform = TargetBodyComponentTransform.Inverse();
				FVector3f LocalTargetPosition = FVector3f(WorldToLocalTransform.TransformPosition(InHitResult.HitResult.Location));
				*TargetPos = LocalTargetPosition;
			}
		}
		else if (InHitResult.HitMeshType == EHitMeshType::TargetHead)
		{
			if (FVector3f* TargetPos = KeyPointData.TargetKeyPoints.TargetHeadPositions.Find(ManipulatorName))
			{
				FTransform WorldToLocalTransform = TargetHeadComponentTransform.Inverse();
				FVector3f LocalTargetPosition = FVector3f(WorldToLocalTransform.TransformPosition(InHitResult.HitResult.Location));
				*TargetPos = LocalTargetPosition;
			}
		}
		else if (InHitResult.HitMeshType == EHitMeshType::CharacterBody)
		{
			if (int32* VertexIndex = KeyPointData.TargetKeyPoints.CharacterBodyVertexIndexes.Find(ManipulatorName))
			{
				*VertexIndex = InHitResult.HitVertexID;
			}
		}
		else if (InHitResult.HitMeshType == EHitMeshType::CharacterHead)
		{
			if (int32* VertexIndex = KeyPointData.TargetKeyPoints.CharacterHeadVertexIndexes.Find(ManipulatorName))
			{
				*VertexIndex = InHitResult.HitVertexID;
			}
		}
		
		KeyPointData.ManipulatorPositions[InManipulatorIndex] = InHitResult.HitResult.Location;
		ManipulatorComponents[InManipulatorIndex]->SetWorldLocation(InHitResult.HitResult.Location);
}

void UMeshTarget3DKeyPointMechanic::UpdateComponentTransform()
{
	if (!bIsInitialized)
	{
		return;
	}
	
	if (MeshTargetScene)
	{
		TargetBodyComponentTransform = MeshTargetScene->GetBodyComponentTransform();	
		TargetHeadComponentTransform = MeshTargetScene->GetHeadComponentTransform();	
	}

	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorComponents.Num(); ++ManipulatorIndex)
	{
		FName KeyPointName;
		if (KeyPointData.GetKeyPointNameForManipulatorIndex(ManipulatorIndex, KeyPointName))
		{
		
			EHitMeshType HitType = KeyPointData.ManipulatorIDHitType[ManipulatorIndex];
			if (HitType == EHitMeshType::TargetBody)
			{
				if (FVector3f* LocalPos = KeyPointData.TargetKeyPoints.TargetBodyPositions.Find(KeyPointName))
				{
					FVector WorldPos = TargetBodyComponentTransform.TransformPosition(FVector(*LocalPos));
					ManipulatorComponents[ManipulatorIndex]->SetWorldLocation(WorldPos);
					KeyPointData.ManipulatorPositions[ManipulatorIndex] = WorldPos;
				}
			}
			else if (HitType == EHitMeshType::TargetHead)
			{
				if (FVector3f* LocalPos = KeyPointData.TargetKeyPoints.TargetHeadPositions.Find(KeyPointName))
				{
					FVector WorldPos = TargetHeadComponentTransform.TransformPosition(FVector(*LocalPos));
					ManipulatorComponents[ManipulatorIndex]->SetWorldLocation(WorldPos);
					KeyPointData.ManipulatorPositions[ManipulatorIndex] = WorldPos;
				}
			}
		}
	}
}

void UMeshTarget3DKeyPointMechanic::UpdateCharacterManipulatorPositions(const TArray<FVector3f>& InBodyVertices, 
	const TArray<FVector3f>& InFaceVertices,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InFaceState)
{
	KeyPointData.UpdateCharacterManipulatorPositions(InBodyVertices, InFaceVertices, InFaceState);
	for (int32 ManipulatorIndex = 0; ManipulatorIndex < ManipulatorComponents.Num(); ++ManipulatorIndex)
	{
		ManipulatorComponents[ManipulatorIndex]->SetWorldLocation(KeyPointData.ManipulatorPositions[ManipulatorIndex]);
	}
}

void UMeshTarget3DKeyPointMechanic::CreateManipulator(const FVector& InPosition, bool bInIsCharacter)
{
	const UMetaHumanCharacterEditorMeshImportTool* OwnerTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(ParentTool.Get());
	const UMetaHumanCharacterEditorMeshImportToolProperties* ToolProperties = OwnerTool ? OwnerTool->GetMeshImportProperties() : nullptr;
	if (!ManipulatorsActor || !ToolProperties)
	{
		return;
	}
	
	UStaticMesh* ManipulatorMesh  = GetManipulatorMesh(bInIsCharacter);
	check(ManipulatorMesh );

	// Use different material for landmarks
	UMaterialInterface* ManipulatorMaterial = GetManipulatorMaterial(bInIsCharacter);
	check(ManipulatorMaterial);

	const float GizmoScale = GetManipulatorScale();

	UStaticMeshComponent* ManipulatorComponent = NewObject<UStaticMeshComponent>(ManipulatorsActor);
	ManipulatorComponent->SetStaticMesh(ManipulatorMesh );
	ManipulatorComponent->SetWorldScale3D(FVector{ GizmoScale * CurrentManipulatorScale});
	ManipulatorComponent->SetWorldLocation(InPosition);
	ManipulatorComponent->SetWorldLocation(InPosition);
	ManipulatorComponent->SetCastShadow(false);
	ManipulatorComponent->SetupAttachment(ManipulatorsActor->GetRootComponent());
	UMaterialInstanceDynamic* MaterialInstance = ManipulatorComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, ManipulatorMaterial);
	if(MaterialInstance)
	{
		const FLinearColor ManipulatorColor = bInIsCharacter ? ToolProperties->MHKeyPointsColor : ToolProperties->CustomKeyPointsColor;
		MaterialInstance->SetVectorParameterValue(TEXT("Color"), ManipulatorColor);
	}
	ManipulatorComponent->RegisterComponent();

	ManipulatorComponents.Add(ManipulatorComponent);
}

UStaticMesh* UMeshTarget3DKeyPointMechanic::GetManipulatorMesh(bool bInIsCharacterMesh) const
{
	UStaticMesh* KeyPointManipulatorMesh = nullptr;

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	if (bInIsCharacterMesh && !Settings->KeyPointCharacterManipulatorMesh.IsNull())
	{
		KeyPointManipulatorMesh = Settings->KeyPointCharacterManipulatorMesh.LoadSynchronous();
	}
	else if (!Settings->KeyPointTargetManipulatorMesh.IsNull())
	{
		KeyPointManipulatorMesh = Settings->KeyPointTargetManipulatorMesh.LoadSynchronous();
	}
	else
	{
		// Fallback to a simple sphere
		KeyPointManipulatorMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Sphere.Sphere'"));
	}

	check(KeyPointManipulatorMesh);
	return KeyPointManipulatorMesh;
}

UMaterialInterface* UMeshTarget3DKeyPointMechanic::GetManipulatorMaterial(bool bInIsCharacterMaterial) const
{
	UMaterialInterface* KeyPointManipulatorMaterial = nullptr;

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	if (bInIsCharacterMaterial && !Settings->KeyPointCharacterManipulatorMesh.IsNull())
	{
		KeyPointManipulatorMaterial = Settings->KeyPointCharacterManipulatorMesh->GetMaterial(0);
	}
	else if (!Settings->KeyPointTargetManipulatorMesh.IsNull())
	{
		KeyPointManipulatorMaterial = Settings->KeyPointTargetManipulatorMesh->GetMaterial(0);
	}
	else
	{
		// Fallback to a simple material
		KeyPointManipulatorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.Material'/" UE_PLUGIN_NAME "/Tools/M_MoveTool_Gizmo.M_MoveTool_Gizmo'"));
	}

	check(KeyPointManipulatorMaterial);
	return KeyPointManipulatorMaterial;
}

float UMeshTarget3DKeyPointMechanic::GetManipulatorScale() const
{
	return 0.004f;
}

void UMeshTarget3DKeyPointMechanic::SelectManipulator(int32 InManipulatorIndex)
{
	SelectedManipulator = InManipulatorIndex;

	for (int32 Index = 0; Index < ManipulatorComponents.Num(); ++Index)
	{
		const float MarkedValue = (Index == SelectedManipulator) ? 1.0f : 0.0f;
		UMaterialInstanceDynamic* ManipulatorMaterialInstance = CastChecked<UMaterialInstanceDynamic>(ManipulatorComponents[Index]->GetMaterial(0));
		ManipulatorMaterialInstance->SetScalarParameterValue(TEXT("Marked"), MarkedValue);
	}
}

void UMeshTarget3DKeyPointMechanic::DeselectAll()
{
	SelectManipulator(INDEX_NONE);
}

void UMeshTarget3DKeyPointMechanic::SetHoveredManipulator(int32 InManipulatorIndex)
{
	HoveredManipulator = InManipulatorIndex;
	for (int32 Index = 0; Index < ManipulatorComponents.Num(); ++Index)
	{
		float HoveredValue = (Index == HoveredManipulator) ? 1.0f : 0.0f;		
		UMaterialInstanceDynamic* ManipulatorMaterialInstance = CastChecked<UMaterialInstanceDynamic>(ManipulatorComponents[Index]->GetMaterial(0));
		ManipulatorMaterialInstance->SetScalarParameterValue(TEXT("Hover"), HoveredValue);
	}
}

void UMeshTarget3DKeyPointMechanic::SetDraggingManipulator(int32 InManipulatorIndex)
{
	DraggingManipulator = InManipulatorIndex;
	for (int32 Index = 0; Index < ManipulatorComponents.Num(); ++Index)
	{
		float DraggedValue = (Index == DraggingManipulator) ? 1.0f : 0.0f;
		UMaterialInstanceDynamic* ManipulatorMaterialInstance = CastChecked<UMaterialInstanceDynamic>(ManipulatorComponents[Index]->GetMaterial(0));
		ManipulatorMaterialInstance->SetScalarParameterValue(TEXT("Drag"), DraggedValue);
	}
}

bool UMeshTarget3DKeyPointMechanic::ShouldShowKeyPoint(const FName& KeyPointName) const
{
	const EKeyPointType* TypePtr = KeyPointData.KeyPointTypes.Find(KeyPointName);
	if (!TypePtr)
	{
		return false;
	}

	switch (*TypePtr)
	{
		case EKeyPointType::Custom:
			return bShowCustomKeyPoints;
		case EKeyPointType::PresetMH:
			return bShowPresetBodyKeyPoints;
		case EKeyPointType::PresetTarget:
			return bShowPresetBodyKeyPoints;
		default:
			return true;
	}
}

void UMeshTarget3DKeyPointMechanic::UpdateKeyPointManipulatorsVisibility()
{
	for (int32 Index = 0; Index < ManipulatorComponents.Num(); ++Index)
    {
		const FName& KeyPointName = KeyPointData.ManipulatorIDToKeyPointName[Index];
        bool bShouldBeVisible = ShouldShowKeyPoint(KeyPointName);
        ManipulatorComponents[Index]->SetVisibility(bShouldBeVisible);
    }
}


#undef LOCTEXT_NAMESPACE