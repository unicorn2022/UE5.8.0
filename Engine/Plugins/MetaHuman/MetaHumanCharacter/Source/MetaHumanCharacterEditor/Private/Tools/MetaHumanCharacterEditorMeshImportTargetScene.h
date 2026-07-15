// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorMeshTargetHitResult.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "MetaHumanCharacterEditorMeshImportTargetScene.generated.h"

class AInternalToolFrameworkActor;
class UDynamicMeshComponent;
class UMetaHumanCharacter;
class UMetaHumanCharacterEditorMeshImportContextObject;
namespace UE::Geometry{class FDynamicMesh3;}
UCLASS()
class UMetaHumanCharacterEditorMeshImportTargetScene : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(TObjectPtr<UMetaHumanCharacter> InMetaHumanCharacter, UWorld* InWorld);
	void Shutdown();
	
	void BuildCharacterDynamicMeshes(const TArray<FVector3f>& InBodyVertices, const TArray<FVector3f>& InFaceVertices, bool bShowCharacterMesh);
	/** Resets the target mesh components to an empty state, ready for a new mesh to be loaded. */
	void ClearTargetMesh();

	bool ApplyBuiltDynamicMeshes(
		bool bInUseCharacterParts,
		const TSharedPtr<UE::Geometry::FDynamicMesh3>& InBodyDynamicMesh,
		const TSharedPtr<UE::Geometry::FDynamicMesh3>& InHeadDynamicMesh,
		UObject* InBodyMesh, UObject* InHeadMesh, UObject* InCombinedMesh,
		const FVector& InLocation,
		TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> InBodyAABBTree = nullptr,
		TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> InHeadAABBTree = nullptr);

	/** Convenience overload that applies the cached dynamic meshes and AABB trees stored in UMetaHumanCharacterEditorMeshImportContextObject */
	bool ApplyBuiltDynamicMeshesFromContextObject(
		const UMetaHumanCharacterEditorMeshImportContextObject& InContextObject,
		bool bInUseCharacterParts,
		UObject* InBodyMesh, UObject* InHeadMesh, UObject* InCombinedMesh,
		const FVector& InLocation);
	
	void SetTargetTranslucentMaterial();
	void SetMaterialTranslucency(float InTranslucency);
	void SetMaterialColor(FLinearColor InColor);
	void SetTargetMaterialFromMesh(UObject* InMesh);
	void SetTargetMaterialsFromMeshes(UObject* InBodyMesh, UObject* InHeadMesh);

	void SetMeshDepthOffset(float InDepthOffset);
	
	bool HitTestMesh(const FRay& InRay, bool bInTestTarget, bool bInTestCharacter, bool bSelectVertexId, FMetaHumanTargetHitResult& OutHitResult);
	
	void SetMeshLocation(const FVector& InLocation);
	void SetMeshTransform(const FTransform& InTransform);
	
	bool IsUsingCharacterParts() const;	
	const FTransform& GetBodyComponentTransform() const;
	const FTransform& GetHeadComponentTransform() const;

	FBox GetTargetMeshBounds() const;
    UDynamicMeshComponent* GetHeadDynamicMeshComponent() const { return HeadDynamicMeshComponent; }
    UDynamicMeshComponent* GetBodyDynamicMeshComponent() const { return BodyDynamicMeshComponent; }
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> GetTargetBodyMeshAABBTree() const { return TargetBodyMeshAABBTree; }
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> GetTargetHeadMeshAABBTree() const { return TargetHeadMeshAABBTree; }

	UPROPERTY()
	TObjectPtr<AInternalToolFrameworkActor> PreviewMeshActor;


	UPROPERTY()
	TObjectPtr<UMetaHumanCharacter> MetaHumanCharacter;
	
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ImportMeshFaceMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ImportMeshBodyMaterial;
	
private:
	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> BodyDynamicMeshComponent;
	
	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> HeadDynamicMeshComponent;
	
	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> DebugBodyModelDynamicMeshComponent;
	
	bool bUsingCharacterParts = false;
	TUniquePtr<UE::Geometry::FDynamicMesh3> CharacterBodyDynamicMesh;
	TUniquePtr<UE::Geometry::FDynamicMesh3> CharacterHeadDynamicMesh;
	
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> TargetBodyMeshAABBTree;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> TargetHeadMeshAABBTree;
	TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> CharacterBodyAABBTree;
	TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> CharacterHeadAABBTree;
};