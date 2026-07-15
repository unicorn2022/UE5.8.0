// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorMeshImportTargetScene.h"
#include "MetaHumanCharacterEditorMeshImportContextObject.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "MetaHumanRigEvaluatedState.h"
#include "StaticMeshAttributes.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorMeshImportTargetScene"

namespace UE::MetaHuman
{
	bool HitTestAABBTree(const FDynamicMesh3* InMesh, UE::Geometry::FDynamicMeshAABBTree3* InAABBTree, const FTransform& InMeshTransform, const FRay& InRay, FHitResult& OutHitResult)
	{
		OutHitResult = FHitResult();

		if (!InMesh || InMesh->VertexCount() == 0 || !InAABBTree)
		{
			return false;
		}
		
		UE::Geometry::FTransformSRT3d MeshTransform(InMeshTransform);
		FRay LocalRay = MeshTransform.InverseTransformRay(InRay);
		
		double HitDistance = 0.0;
		int32 HitTriangleID = INDEX_NONE;
		FVector HitBaryCoord;
		UE::Geometry::IMeshSpatial::FQueryOptions Options;
		Options.TriangleFilterF = [&](int32 Tid)
		{
			// Ignore backfaces by checking normal against ray direction
			const FVector3d TriNormal = InMesh->GetTriNormal(Tid);
			return TriNormal.Dot(LocalRay.Direction) < 0;
		};
		
		if (InAABBTree->FindNearestHitTriangle(LocalRay, HitDistance, HitTriangleID, HitBaryCoord, Options))
		{
			UE::Geometry::FTriangle3d TriangleVertices;
			InMesh->GetTriVertices(HitTriangleID, TriangleVertices.V[0], TriangleVertices.V[1], TriangleVertices.V[2]);
			
			UE::Geometry::FIntrRay3Triangle3d IntersectionQuery(LocalRay, TriangleVertices);		
			if (!IntersectionQuery.Find())
			{
				return false;
			}
			
			
			FVector LocalHitPosition = LocalRay.PointAt(IntersectionQuery.RayParameter);
			FVector WorldHitPosition = MeshTransform.TransformPosition(LocalHitPosition);
			FVector LocalNormal = TriangleVertices.Normal();
			FVector WorldNormal = MeshTransform.TransformNormal(LocalNormal);
						
			OutHitResult.bBlockingHit = true;
			OutHitResult.Location     = WorldHitPosition;
			OutHitResult.ImpactPoint  = WorldHitPosition;
			OutHitResult.Normal = WorldNormal;
			OutHitResult.ImpactNormal = WorldNormal;
			OutHitResult.Distance     = FVector::Dist(InRay.Origin, WorldHitPosition);
			OutHitResult.FaceIndex    = HitTriangleID;
			
			return true;
		}

		return false;
	}
	
	void SetMeshMaterialOnComponent(UObject* InMesh, UDynamicMeshComponent* InDynamicMeshComponent)
	{
		if (!InDynamicMeshComponent)
		{
			return;
		}

		// Resolve nullptr material slots (common when the user deletes the DCC material on import) to the engine's default surface material. Without this, slots were silently skipped and the downstream matcap/overlay loops would iterate zero times 
		auto ResolveMaterial = [](UMaterialInterface* InMat) -> UMaterialInterface*
		{
			return InMat ? InMat : UMaterial::GetDefaultMaterial(MD_Surface);
		};

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(InMesh))
		{
			const TArray<FStaticMaterial>& StaticMats = StaticMesh->GetStaticMaterials();
			for (int32 i = 0; i < StaticMats.Num(); ++i)
			{
				InDynamicMeshComponent->SetMaterial(i, ResolveMaterial(StaticMats[i].MaterialInterface));
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(InMesh))
		{
			const TArray<FSkeletalMaterial>& SkelMats = SkeletalMesh->GetMaterials();
			for (int32 i = 0; i < SkelMats.Num(); ++i)
			{
				InDynamicMeshComponent->SetMaterial(i, ResolveMaterial(SkelMats[i].MaterialInterface));
			}
		}
	}
}

void UMetaHumanCharacterEditorMeshImportTargetScene::Initialize(TObjectPtr<UMetaHumanCharacter> InMetaHumanCharacter, UWorld* InWorld)
{
	MetaHumanCharacter = InMetaHumanCharacter;
	if (InWorld)
	{
		FActorSpawnParameters SpawnInfo;
		PreviewMeshActor = InWorld->SpawnActor<AInternalToolFrameworkActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);
		BodyDynamicMeshComponent = NewObject<UDynamicMeshComponent>(PreviewMeshActor);

		BodyDynamicMeshComponent->SetShadowsEnabled(false);
		BodyDynamicMeshComponent->SetupAttachment(PreviewMeshActor->GetRootComponent());
		BodyDynamicMeshComponent->RegisterComponent();
		
		HeadDynamicMeshComponent = NewObject<UDynamicMeshComponent>(PreviewMeshActor);

		HeadDynamicMeshComponent->SetShadowsEnabled(false);
		HeadDynamicMeshComponent->SetupAttachment(PreviewMeshActor->GetRootComponent());
		HeadDynamicMeshComponent->RegisterComponent();
		
		DebugBodyModelDynamicMeshComponent = NewObject<UDynamicMeshComponent>(PreviewMeshActor);

		DebugBodyModelDynamicMeshComponent->SetShadowsEnabled(false);
		DebugBodyModelDynamicMeshComponent->SetupAttachment(PreviewMeshActor->GetRootComponent());
		DebugBodyModelDynamicMeshComponent->RegisterComponent();
	}
	
	CharacterBodyDynamicMesh = MakeUnique<UE::Geometry::FDynamicMesh3>();
	CharacterHeadDynamicMesh = MakeUnique<UE::Geometry::FDynamicMesh3>();
	
	ImportMeshFaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.MaterialInstanceConstant'/" UE_PLUGIN_NAME "/Materials/MI_M2M_Head.MI_M2M_Head'"));
	ImportMeshBodyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Script/Engine.MaterialInstanceConstant'/" UE_PLUGIN_NAME "/Materials/MI_M2M_Body.MI_M2M_Body'"));
}

void UMetaHumanCharacterEditorMeshImportTargetScene::Shutdown()
{
	if (PreviewMeshActor != nullptr)
	{
		PreviewMeshActor->Destroy();
		PreviewMeshActor = nullptr;
	}
}

void UMetaHumanCharacterEditorMeshImportTargetScene::BuildCharacterDynamicMeshes(const TArray<FVector3f>& InBodyVertices, const TArray<FVector3f>& InFaceVertices, bool bShowCharacterMesh)
{
	{
		CharacterBodyDynamicMesh->Clear();
		
		for (int32 VertexIndex = 0; VertexIndex < InBodyVertices.Num(); ++VertexIndex)
		{
			FVector Position(InBodyVertices[VertexIndex][0], InBodyVertices[VertexIndex][2], InBodyVertices[VertexIndex][1]);
			CharacterBodyDynamicMesh->AppendVertex(Position);
		}
		
		TArray<int32> TriangleIndices = UMetaHumanCharacterEditorSubsystem::Get()->GetBodyState(MetaHumanCharacter)->GetTrianglesIndices();
		if (!ensureMsgf(TriangleIndices.Num() % 3 == 0, TEXT("Triangle index buffer size (%d) is not divisible by 3"), TriangleIndices.Num()))
		{
			return;
		}
		
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleIndices.Num(); TriangleIndex += 3)
		{
			CharacterBodyDynamicMesh->AppendTriangle(TriangleIndices[TriangleIndex], TriangleIndices[TriangleIndex + 1], TriangleIndices[TriangleIndex + 2]);
		}
		
		if (!CharacterBodyDynamicMesh->HasAttributes())
		{
			CharacterBodyDynamicMesh->EnableAttributes();
		}

		UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = CharacterBodyDynamicMesh->Attributes()->PrimaryNormals();	
		UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, /*bUseMeshVertexNormalsIfAvailable=*/false);
		UE::Geometry::FMeshNormals::QuickRecomputeOverlayNormals(*CharacterBodyDynamicMesh);
		
		CharacterBodyAABBTree = MakeUnique<UE::Geometry::FDynamicMeshAABBTree3>(CharacterBodyDynamicMesh.Get());
	}
	
	{
		CharacterHeadDynamicMesh->Clear();
		
		for (int32 VertexIndex = 0; VertexIndex < InFaceVertices.Num(); ++VertexIndex)
		{
			FVector Position(InFaceVertices[VertexIndex][0], InFaceVertices[VertexIndex][2], InFaceVertices[VertexIndex][1]);
			CharacterHeadDynamicMesh->AppendVertex(Position);
		}
		
		TArray<int32> TriangleIndices;
		UMetaHumanCharacterEditorSubsystem::Get()->GetFaceState(MetaHumanCharacter)->GetTrianglesIndices(TriangleIndices);
		if (!ensureMsgf(TriangleIndices.Num() % 3 == 0, TEXT("Triangle index buffer size (%d) is not divisible by 3"), TriangleIndices.Num()))
		{
			return;
		}
		
		for (int32 TriangleIndex = 0; TriangleIndex < TriangleIndices.Num(); TriangleIndex += 3)
		{
			CharacterHeadDynamicMesh->AppendTriangle(TriangleIndices[TriangleIndex], TriangleIndices[TriangleIndex + 1], TriangleIndices[TriangleIndex + 2]);
		}
		
		if (!CharacterHeadDynamicMesh->HasAttributes())
		{
			CharacterHeadDynamicMesh->EnableAttributes();
		}

		UE::Geometry::FMeshNormals MeshNormals(CharacterHeadDynamicMesh.Get());
		MeshNormals.RecomputeOverlayNormals(CharacterHeadDynamicMesh->Attributes()->PrimaryNormals());
		MeshNormals.CopyToOverlay(CharacterHeadDynamicMesh->Attributes()->PrimaryNormals());
		
		CharacterHeadAABBTree = MakeUnique<UE::Geometry::FDynamicMeshAABBTree3>(CharacterHeadDynamicMesh.Get());
	}
	
	if (DebugBodyModelDynamicMeshComponent)
	{
		if (CharacterBodyDynamicMesh->VertexCount() == 0)
		{
			DebugBodyModelDynamicMeshComponent->GetDynamicMesh()->Reset();
		}
		else
		{
			UE::Geometry::FDynamicMesh3 DebugBodyModelMesh = *CharacterBodyDynamicMesh; 
			DebugBodyModelDynamicMeshComponent->SetMesh(MoveTemp(DebugBodyModelMesh));
			DebugBodyModelDynamicMeshComponent->SetVisibility(bShowCharacterMesh);
		}
	}
}
void UMetaHumanCharacterEditorMeshImportTargetScene::ClearTargetMesh()
{
	if (BodyDynamicMeshComponent)
	{
		BodyDynamicMeshComponent->GetDynamicMesh()->Reset();
	}
	if (HeadDynamicMeshComponent)
	{
		HeadDynamicMeshComponent->GetDynamicMesh()->Reset();
	}
	TargetBodyMeshAABBTree.Reset();
	TargetHeadMeshAABBTree.Reset();
}

bool UMetaHumanCharacterEditorMeshImportTargetScene::ApplyBuiltDynamicMeshes(
	bool bInUseCharacterParts,
	const TSharedPtr<UE::Geometry::FDynamicMesh3>& InBodyDynamicMesh,
	const TSharedPtr<UE::Geometry::FDynamicMesh3>& InHeadDynamicMesh,
	UObject* InBodyMesh, UObject* InHeadMesh, UObject* InCombinedMesh,
	const FVector& InLocation,
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> InBodyAABBTree,
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> InHeadAABBTree)
{
	if (!BodyDynamicMeshComponent)
	{
		return false;
	}

	bUsingCharacterParts = bInUseCharacterParts;

	if (bInUseCharacterParts)
	{
		if (!HeadDynamicMeshComponent)
		{
			return false;
		}

		bool bBodyMeshValid = false;
		if (InBodyDynamicMesh.IsValid() && InBodyDynamicMesh->VertexCount() > 0)
		{
			TargetBodyMeshAABBTree.Reset();
			BodyDynamicMeshComponent->GetDynamicMesh()->SetMesh(*InBodyDynamicMesh);
			bBodyMeshValid = true;
		}
		else
		{
			BodyDynamicMeshComponent->GetDynamicMesh()->Reset();
		}

		bool bHeadMeshValid = false;
		if (InHeadDynamicMesh.IsValid() && InHeadDynamicMesh->VertexCount() > 0)
		{
			TargetHeadMeshAABBTree.Reset();
			HeadDynamicMeshComponent->GetDynamicMesh()->SetMesh(*InHeadDynamicMesh);
			bHeadMeshValid = true;
		}
		else
		{
			HeadDynamicMeshComponent->GetDynamicMesh()->Reset();
		}

		BodyDynamicMeshComponent->SetWorldLocation(InLocation);
		HeadDynamicMeshComponent->SetWorldLocation(InLocation);

		TargetBodyMeshAABBTree = InBodyAABBTree ? InBodyAABBTree : MakeShared<UE::Geometry::FDynamicMeshAABBTree3>(BodyDynamicMeshComponent->GetMesh());
		TargetHeadMeshAABBTree = InHeadAABBTree ? InHeadAABBTree : MakeShared<UE::Geometry::FDynamicMeshAABBTree3>(HeadDynamicMeshComponent->GetMesh());

		SetTargetMaterialsFromMeshes(InBodyMesh, InHeadMesh);

		return bBodyMeshValid || bHeadMeshValid;
	}
	else
	{
		if (HeadDynamicMeshComponent)
		{
			HeadDynamicMeshComponent->GetDynamicMesh()->Reset();
		}

		if (!InBodyDynamicMesh.IsValid() || InBodyDynamicMesh->VertexCount() == 0)
		{
			BodyDynamicMeshComponent->GetDynamicMesh()->Reset();
			return false;
		}

		TargetBodyMeshAABBTree.Reset();
		BodyDynamicMeshComponent->GetDynamicMesh()->SetMesh(*InBodyDynamicMesh);
		SetMeshLocation(InLocation);

		TargetBodyMeshAABBTree = InBodyAABBTree ? InBodyAABBTree : MakeShared<UE::Geometry::FDynamicMeshAABBTree3>(BodyDynamicMeshComponent->GetMesh());

		SetTargetMaterialFromMesh(InCombinedMesh);

		return true;
	}
}

bool UMetaHumanCharacterEditorMeshImportTargetScene::ApplyBuiltDynamicMeshesFromContextObject(
	const UMetaHumanCharacterEditorMeshImportContextObject& InContextObject,
	bool bInUseCharacterParts,
	UObject* InBodyMesh, UObject* InHeadMesh, UObject* InCombinedMesh,
	const FVector& InLocation)
{
	return ApplyBuiltDynamicMeshes(
		bInUseCharacterParts,
		InContextObject.BodyDynamicMesh, InContextObject.HeadDynamicMesh,
		InBodyMesh, InHeadMesh, InCombinedMesh,
		InLocation,
		InContextObject.BodyAABBTree,
		InContextObject.HeadAABBTree);
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetTargetTranslucentMaterial()
{
	if (BodyDynamicMeshComponent && ImportMeshBodyMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ImportMeshBodyMaterial, BodyDynamicMeshComponent);
		const int32 NumSlots = FMath::Max(1, BodyDynamicMeshComponent->GetNumMaterials());
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			BodyDynamicMeshComponent->SetMaterial(Slot, MID);
		}
	}
	
	if (HeadDynamicMeshComponent && ImportMeshFaceMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ImportMeshFaceMaterial, HeadDynamicMeshComponent);
		const int32 NumSlots = FMath::Max(1, HeadDynamicMeshComponent->GetNumMaterials());
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			HeadDynamicMeshComponent->SetMaterial(Slot, MID);
		}
	}
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetMaterialTranslucency(float InTranslucency)
{
	if (BodyDynamicMeshComponent) 
	{
		for (int32 Slot = 0; Slot < BodyDynamicMeshComponent->GetMaterials().Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BodyDynamicMeshComponent->GetMaterial(Slot)))
			{
				MID->SetScalarParameterValue(TEXT("Opacity"), 1.f - InTranslucency);
			}
		}
	}
	
	if (HeadDynamicMeshComponent) 
	{
		for (int32 Slot = 0; Slot < HeadDynamicMeshComponent->GetMaterials().Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(HeadDynamicMeshComponent->GetMaterial(Slot)))
			{
				MID->SetScalarParameterValue(TEXT("Opacity"), 1.f - InTranslucency);
			}
		}
	}
	
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetMaterialColor(FLinearColor InColor)
{
	if (BodyDynamicMeshComponent)
	{
		for (int32 Slot = 0; Slot < BodyDynamicMeshComponent->GetMaterials().Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BodyDynamicMeshComponent->GetMaterial(Slot)))
			{
				MID->SetVectorParameterValue(TEXT("BaseColor"), InColor);
			}
		}
	}

	if (HeadDynamicMeshComponent)
	{
		for (int32 Slot = 0; Slot < HeadDynamicMeshComponent->GetMaterials().Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(HeadDynamicMeshComponent->GetMaterial(Slot)))
			{
				MID->SetVectorParameterValue(TEXT("BaseColor"), InColor);
			}
		}
	}
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetTargetMaterialFromMesh(UObject* InMesh)
{
	UE::MetaHuman::SetMeshMaterialOnComponent(InMesh, BodyDynamicMeshComponent);
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetTargetMaterialsFromMeshes(UObject* InBodyMesh, UObject* InHeadMesh)
{
	UE::MetaHuman::SetMeshMaterialOnComponent(InBodyMesh, BodyDynamicMeshComponent);
	UE::MetaHuman::SetMeshMaterialOnComponent(InHeadMesh, HeadDynamicMeshComponent);
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetMeshDepthOffset(float InDepthOffset)
{
	if (BodyDynamicMeshComponent)
	{
		for (int32 Slot = 0; Slot < BodyDynamicMeshComponent->GetMaterials().Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BodyDynamicMeshComponent->GetMaterial(Slot)))
			{
				MID->SetScalarParameterValue(TEXT("DepthOffset"), InDepthOffset);
			}
		}
	}

	if (HeadDynamicMeshComponent)
	{
		for (int32 Slot = 0; Slot < HeadDynamicMeshComponent->GetMaterials().Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(HeadDynamicMeshComponent->GetMaterial(Slot)))
			{
				MID->SetScalarParameterValue(TEXT("DepthOffset"), InDepthOffset);
			}
		}
	}
}

bool UMetaHumanCharacterEditorMeshImportTargetScene::HitTestMesh(const FRay& InRay, bool bInTestTarget, bool bInTestCharacter, bool bSelectVertexId, FMetaHumanTargetHitResult& OutHitResult)
{
	bool bIntersectionFound = false;
	FHitResult TargetBodyHitResult;
	FHitResult TargetHeadHitResult;
	FHitResult CharacterBodyHitResult;
	FHitResult CharacterHeadHitResult;
	
	float TargetBodyDistance = TNumericLimits<float>::Max();
	float TargetHeadDistance = TNumericLimits<float>::Max();
	float CharacterBodyDistance = TNumericLimits<float>::Max();
	float CharacterHeadDistance = TNumericLimits<float>::Max();

	if (bInTestTarget)
	{
		if (BodyDynamicMeshComponent && UE::MetaHuman::HitTestAABBTree(BodyDynamicMeshComponent->GetMesh(), TargetBodyMeshAABBTree.Get(), BodyDynamicMeshComponent->GetComponentTransform(), InRay, TargetBodyHitResult))
		{
			bIntersectionFound = true;
			TargetBodyDistance = TargetBodyHitResult.Distance;
		}
		
		if (bUsingCharacterParts)
		{
			if (HeadDynamicMeshComponent && UE::MetaHuman::HitTestAABBTree(HeadDynamicMeshComponent->GetMesh(), TargetHeadMeshAABBTree.Get(), HeadDynamicMeshComponent->GetComponentTransform(), InRay, TargetHeadHitResult))
			{
				bIntersectionFound = true;
				TargetHeadDistance = TargetHeadHitResult.Distance;
			}
		}
	}

	if (bInTestCharacter && UE::MetaHuman::HitTestAABBTree(CharacterBodyDynamicMesh.Get(), CharacterBodyAABBTree.Get(), FTransform(), InRay, CharacterBodyHitResult))
	{
		bIntersectionFound = true;
		CharacterBodyDistance = CharacterBodyHitResult.Distance;
	}
	
	if (bInTestCharacter && UE::MetaHuman::HitTestAABBTree(CharacterHeadDynamicMesh.Get(), CharacterHeadAABBTree.Get(), FTransform(), InRay, CharacterHeadHitResult))
	{
		bIntersectionFound = true;
		CharacterHeadDistance = CharacterHeadHitResult.Distance;
	}
	
	if (bIntersectionFound)
	{
		if (TargetBodyDistance < TargetHeadDistance && TargetBodyDistance < CharacterBodyDistance && TargetBodyDistance < CharacterHeadDistance)
		{
			OutHitResult.HitMeshType = EHitMeshType::TargetBody;
			OutHitResult.HitResult = TargetBodyHitResult;
			OutHitResult.HitVertexID = INDEX_NONE;
		}
		else if (TargetHeadDistance < TargetBodyDistance && TargetHeadDistance < CharacterBodyDistance && TargetHeadDistance < CharacterHeadDistance)
		{
			OutHitResult.HitMeshType = EHitMeshType::TargetHead;
			OutHitResult.HitResult = TargetHeadHitResult;
			OutHitResult.HitVertexID = INDEX_NONE;
		}
		else
		{
			constexpr float CharacterHeadPreferenceTolerance = 1.0f;
			const bool bPreferCharacterHead =
				CharacterHeadDistance < TNumericLimits<float>::Max()
				&& CharacterHeadDistance < CharacterBodyDistance + CharacterHeadPreferenceTolerance;

			if (!bPreferCharacterHead)
			{
				OutHitResult.HitMeshType = EHitMeshType::CharacterBody;
				OutHitResult.HitResult = CharacterBodyHitResult;
				OutHitResult.HitVertexID = INDEX_NONE;

				if (bSelectVertexId)
				{
					FVector HitVertex;
					FVector HitNormal;
					OutHitResult.HitVertexID = UMetaHumanCharacterEditorSubsystem::Get()->SelectBodyVertex(MetaHumanCharacter, InRay, HitVertex, HitNormal);

					if (OutHitResult.HitVertexID != INDEX_NONE)
					{
						OutHitResult.HitResult.Location = HitVertex;
						OutHitResult.HitResult.Normal = HitNormal;
					}
					else
					{
						return false;
					}
				}
			}
			else
			{
				OutHitResult.HitMeshType = EHitMeshType::CharacterHead;
				OutHitResult.HitResult = CharacterHeadHitResult;
				OutHitResult.HitVertexID = INDEX_NONE;

				if (bSelectVertexId)
				{
					FVector HitVertex;
					FVector HitNormal;
					OutHitResult.HitVertexID = UMetaHumanCharacterEditorSubsystem::Get()->SelectFaceVertex(MetaHumanCharacter, InRay, HitVertex, HitNormal);

					if (OutHitResult.HitVertexID != INDEX_NONE)
					{
						OutHitResult.HitResult.Location = HitVertex;
						OutHitResult.HitResult.Normal = HitNormal;
					}
					else
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	return false;
}

FBox UMetaHumanCharacterEditorMeshImportTargetScene::GetTargetMeshBounds() const
{
	FBox CombinedBounds(ForceInit);

    if (BodyDynamicMeshComponent)
    {
        CombinedBounds += BodyDynamicMeshComponent->Bounds.GetBox();
    }

    if (bUsingCharacterParts && HeadDynamicMeshComponent)
    {
        CombinedBounds += HeadDynamicMeshComponent->Bounds.GetBox();
    }

    return CombinedBounds;
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetMeshLocation(const FVector& InLocation)
{
	if (BodyDynamicMeshComponent)
	{
		BodyDynamicMeshComponent->SetWorldLocation(InLocation);
	}

	if (HeadDynamicMeshComponent)
	{
		HeadDynamicMeshComponent->SetWorldLocation(InLocation);
	}

	if (DebugBodyModelDynamicMeshComponent)
	{
		FVector CharacterLocation(-InLocation[0], InLocation[1], InLocation[2]); 
		DebugBodyModelDynamicMeshComponent->SetWorldLocation(CharacterLocation);
	}
}

void UMetaHumanCharacterEditorMeshImportTargetScene::SetMeshTransform(const FTransform& InTransform)
{
	if (BodyDynamicMeshComponent)
	{
		BodyDynamicMeshComponent->SetWorldTransform(InTransform);
	}
	
	if (HeadDynamicMeshComponent)
	{
		HeadDynamicMeshComponent->SetWorldTransform(InTransform);
	}
}

bool UMetaHumanCharacterEditorMeshImportTargetScene::IsUsingCharacterParts() const
{
	return bUsingCharacterParts;
}

const FTransform& UMetaHumanCharacterEditorMeshImportTargetScene::GetBodyComponentTransform() const
{
	if (BodyDynamicMeshComponent)
	{
		return BodyDynamicMeshComponent->GetComponentTransform();
	}
	return FTransform::Identity;
}

const FTransform& UMetaHumanCharacterEditorMeshImportTargetScene::GetHeadComponentTransform() const
{
	if (HeadDynamicMeshComponent)
	{
		return HeadDynamicMeshComponent->GetComponentTransform();
	}
	return FTransform::Identity;
}

#undef LOCTEXT_NAMESPACE
