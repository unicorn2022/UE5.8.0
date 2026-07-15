// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanConformTargetParams.h"
#include "MetaHumanCharacterTargetKeyPoints.h"
#include "Templates/SharedPointer.h"

class FMeshImportTaskRunner : public TSharedFromThis<FMeshImportTaskRunner, ESPMode::ThreadSafe>
{
public:
	DECLARE_DELEGATE_SixParams(FOnMeshImportIteration, 
		float /*Percentage*/,
		const FMetaHumanRigEvaluatedState& /*BodyVerticesAndNormals*/,
		const FMetaHumanRigEvaluatedState& /*FaceVerticesAndNormals*/,
		const TArray<FVector3f>& /*LocalBodyJointPositions*/,
		const TArray<FRotator3f>& /*LocalBodyJointRotations*/,
		const FText& /*SolveMessage*/)
	DECLARE_DELEGATE_FiveParams(FOnMeshImportFinish, 
		bool /* bSuccess*/,
		bool /* bWasCancelled*/,
		const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& /*BodyState*/, 
		const TSharedRef<FMetaHumanCharacterIdentity::FState>& /*FaceState*/,
		const FMetaHumanCharacterTargetMeshKey& /*TargetMeshKey*/)

public:
	FOnMeshImportIteration& OnMeshImportIteration();
	FOnMeshImportFinish& OnMeshImportFinish();
	
	static TSharedRef<FMeshImportTaskRunner> Create()
	{
		TSharedRef<FMeshImportTaskRunner> Ref = MakeShared<FMeshImportTaskRunner>();
		return Ref;
	}
	FMeshImportTaskRunner() = default;
	~FMeshImportTaskRunner();
	
	FMeshImportTaskRunner(const FMeshImportTaskRunner&) = delete;
	FMeshImportTaskRunner& operator=(const FMeshImportTaskRunner&) = delete;
	FMeshImportTaskRunner(FMeshImportTaskRunner&&) = delete;
	FMeshImportTaskRunner& operator=(FMeshImportTaskRunner&&) = delete;

	void StartConform(const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState,
		const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState,
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
		const FConformTargetParams& InConformTargetParams);

	void StartAlignToTargetMesh(const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState,
		const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState,
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
		const FConformTargetParams& InConformTargetParams);
	
	void StartRefineVertices(const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState,
		const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState,
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
		const FRefinementTargetParams& InRefinementTargetParams);
	
	void CancelUpdates();

private:
	bool OnIterationUpdate(const FMetaHumanRigEvaluatedState& InBodyVerticesAndNormals, const TArray<FMatrix44f>& InBindPoseMatrices, int32 InIterationCount, ESolveStepType InSolveStepType);
	static void OnProcessComplete(const TWeakPtr<FMeshImportTaskRunner>& TaskRunnerWeakPtr);	
	
private:
	FOnMeshImportIteration MeshImportIterationDelegate;
	FOnMeshImportFinish MeshImportFinishDelegate;


	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState;
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState;
	FMetaHumanCharacterTargetMeshKey TargetMeshKey;

	int32 IterationCount = 0;
	bool bSuccess = true;
	bool bCancelled = false;

	FCriticalSection UpdateVerticesMutex;
	FMetaHumanRigEvaluatedState IterationBodyVerticesAndNormals;
	FMetaHumanRigEvaluatedState IterationFaceVerticesAndNormals;
	TArray<FVector3f> IterationLocalJointPositions;
	TArray<FRotator3f> IterationLocalJointRotations;
	FText IterationSolveMessage;
};
