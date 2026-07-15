// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorMeshImportTaskRunner.h"

#define LOCTEXT_NAMESPACE "MeshImportTaskRunner"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterIdentity.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<bool> CVarMeshImportShowIterations(
	TEXT("mh.Character.FromCustomMeshImportShowIterations"),
	true,
	TEXT("When true, the viewport is updated after each solve iteration. Set to false to skip iteration updates and only show the final result."),
	ECVF_Default);

FMeshImportTaskRunner::FOnMeshImportIteration& FMeshImportTaskRunner::OnMeshImportIteration()
{
	return MeshImportIterationDelegate;
}

FMeshImportTaskRunner::FOnMeshImportFinish& FMeshImportTaskRunner::OnMeshImportFinish()
{
	return MeshImportFinishDelegate;
}

FMeshImportTaskRunner::~FMeshImportTaskRunner()
{
}

void FMeshImportTaskRunner::StartConform(const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState,
		const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState,
		const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
		const FConformTargetParams& InConformTargetParams)
{
	TWeakPtr<FMeshImportTaskRunner> WeakTaskRunnerPtr = AsWeak();
	if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
	{
		ThisTaskRunner->bSuccess = true;
		ThisTaskRunner->bCancelled = false;
	}
	
	// Take copy of body and face states
	BodyState = MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*InBodyState);
	FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InFaceState);

	if (InConformTargetParams.bAutoSolve)
	{
		BodyState->Reset();
		FaceState->Reset();
	}

	// Set body state to evaluate pose
	BodyState->SetMetaHumanBodyType(EMetaHumanBodyType::BlendableBody);
	BodyState->SetEvaluatePose(true);

	TargetMeshKey = InTargetMeshKey;
	BodyState->OnMeshConformIteration().BindSP(AsShared(), &FMeshImportTaskRunner::OnIterationUpdate);

    // Start conform process in worker thread
	Async(EAsyncExecution::ThreadPool, [WeakTaskRunnerPtr, InConformTargetParams]()
    {   
		if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
		{
			ThisTaskRunner->bSuccess = ThisTaskRunner->BodyState->ConformTarget(InConformTargetParams);

			if (ThisTaskRunner->bSuccess)
			{
				FMetaHumanRigEvaluatedState BodyStateNoDelta;
				FMetaHumanRigEvaluatedState BodyStateWithDelta;
				ThisTaskRunner->BodyState->GetVerticesWithAndWithoutDeltas(BodyStateNoDelta, BodyStateWithDelta);

				ThisTaskRunner->FaceState->FitWithVertexDeltasFromBody(
					ThisTaskRunner->BodyState->CopyComponentPose(),
					BodyStateNoDelta.Vertices,
					BodyStateWithDelta.Vertices,
					BodyStateWithDelta.VertexNormals,
					ThisTaskRunner->BodyState->GetNumVerticesPerLOD());
			}
		}
    },
    [WeakTaskRunnerPtr]()
    {
    	// Cleanup on game thread
    	Async(EAsyncExecution::TaskGraphMainTick, [WeakTaskRunnerPtr]()
		{
			OnProcessComplete(WeakTaskRunnerPtr);
		});
    });

}

void FMeshImportTaskRunner::StartAlignToTargetMesh(const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState,
	const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState,
	const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
	const FConformTargetParams& InConformTargetParams)
{
	TWeakPtr<FMeshImportTaskRunner> WeakTaskRunnerPtr = AsWeak();
	if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
	{
		ThisTaskRunner->bSuccess = true;
		ThisTaskRunner->bCancelled = false;
	}

	// Take copy of body and face states
	BodyState = MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*InBodyState);
	FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InFaceState);

	// Set body state to evaluate pose
	BodyState->SetMetaHumanBodyType(EMetaHumanBodyType::BlendableBody);
	BodyState->SetEvaluatePose(true);

	TargetMeshKey = InTargetMeshKey;

	Async(EAsyncExecution::ThreadPool, [WeakTaskRunnerPtr, InConformTargetParams]()
	{
		if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
		{
			ThisTaskRunner->bSuccess = ThisTaskRunner->BodyState->AlignToTargetMesh(InConformTargetParams);

			if (ThisTaskRunner->bSuccess)
			{
				const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = ThisTaskRunner->BodyState->GetVerticesAndVertexNormals();
				ThisTaskRunner->FaceState->SetBodyJointsAndBodyFaceVertices(ThisTaskRunner->BodyState->CopyComponentPose(), VerticesAndVertexNormals.Vertices);
				ThisTaskRunner->FaceState->SetBodyVertexNormals(VerticesAndVertexNormals.VertexNormals, ThisTaskRunner->BodyState->GetNumVerticesPerLOD());

				FFitToTargetOptions Options;
				Options.AlignmentOptions = EAlignmentOptions::None;
				ThisTaskRunner->FaceState->FitFromBodyVertices(VerticesAndVertexNormals.Vertices, Options);
			}
		}
	},
	[WeakTaskRunnerPtr]()
	{
		Async(EAsyncExecution::TaskGraphMainTick, [WeakTaskRunnerPtr]()
		{
			OnProcessComplete(WeakTaskRunnerPtr);
		});
	});
}

void FMeshImportTaskRunner::StartRefineVertices(const TSharedRef<FMetaHumanCharacterBodyIdentity::FState>& InBodyState,
	const TSharedRef<FMetaHumanCharacterIdentity::FState>& InFaceState, 
	const FMetaHumanCharacterTargetMeshKey& InTargetMeshKey,
	const FRefinementTargetParams& InRefinementTargetParams)
{
	TWeakPtr<FMeshImportTaskRunner> WeakTaskRunnerPtr = AsWeak();
	if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
	{
		ThisTaskRunner->bSuccess = true;
		ThisTaskRunner->bCancelled = false;
	}
	
	// Take copy of body and face states
	BodyState = MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*InBodyState);
	FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InFaceState);
	
	// Set body state to evaluate pose
	BodyState->SetMetaHumanBodyType(EMetaHumanBodyType::BlendableBody);
	BodyState->SetEvaluatePose(true);
	
	TargetMeshKey = InTargetMeshKey;
	
	// Start refine vertices process in worker thread
	Async(EAsyncExecution::ThreadPool, [WeakTaskRunnerPtr, InRefinementTargetParams]()
    {   
		if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
		{
			ThisTaskRunner->bSuccess = ThisTaskRunner->BodyState->RefineVerticesToTarget(InRefinementTargetParams);

			if (ThisTaskRunner->bSuccess)
			{
				FMetaHumanRigEvaluatedState BodyStateNoDelta;
				FMetaHumanRigEvaluatedState BodyStateWithDelta;
				ThisTaskRunner->BodyState->GetVerticesWithAndWithoutDeltas(BodyStateNoDelta, BodyStateWithDelta);

				ThisTaskRunner->FaceState->FitWithVertexDeltasFromBody(
					ThisTaskRunner->BodyState->CopyComponentPose(),
					BodyStateNoDelta.Vertices,
					BodyStateWithDelta.Vertices,
					BodyStateWithDelta.VertexNormals,
					ThisTaskRunner->BodyState->GetNumVerticesPerLOD());
			}
		}
    },
    [WeakTaskRunnerPtr]()
    {
    	// Cleanup on game thread
    	Async(EAsyncExecution::TaskGraphMainTick, [WeakTaskRunnerPtr]()
		{
    		OnProcessComplete(WeakTaskRunnerPtr);
		});
    });

}

void FMeshImportTaskRunner::CancelUpdates()
{
	if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = AsWeak().Pin())
	{
		ThisTaskRunner->bCancelled = true;
	}
}

bool FMeshImportTaskRunner::OnIterationUpdate(const FMetaHumanRigEvaluatedState& InBodyVerticesAndNormals, const TArray<FMatrix44f>& InBindPoseMatrices, int32 InIterationCount, ESolveStepType InSolveStepType)
{
	if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = AsWeak().Pin())
	{
		if (ThisTaskRunner->bCancelled)
		{
			return false;
		}
	}
	
	if (!CVarMeshImportShowIterations.GetValueOnAnyThread())
	{
		return true;
	}
	
	{
		FScopeLock ScopeLock(&UpdateVerticesMutex);
		IterationBodyVerticesAndNormals = InBodyVerticesAndNormals;
		BodyState->GetNeutralJointTransforms(InBindPoseMatrices, IterationLocalJointPositions, IterationLocalJointRotations);

		// Update face state from body
		FFitToTargetOptions Options;
		Options.AlignmentOptions = EAlignmentOptions::None;
		FaceState->SetBodyJointsAndBodyFaceVertices(InBindPoseMatrices, IterationBodyVerticesAndNormals.Vertices);
		FaceState->SetBodyVertexNormals(IterationBodyVerticesAndNormals.VertexNormals, BodyState->GetNumVerticesPerLOD());

		bool bFitFace = InSolveStepType == ESolveStepType::FaceSolve;
		if (!bFitFace)
		{
			constexpr int32 bodyIterationFaceFitIterationThreshold = 5; // No need to face fit every iteration when updating body 
			bFitFace = (InIterationCount > 0) && (InIterationCount % bodyIterationFaceFitIterationThreshold == 0);
		}

		if (bFitFace)
		{
			FaceState->FitFromBodyVertices(IterationBodyVerticesAndNormals.Vertices, Options);
		}

		IterationFaceVerticesAndNormals = FaceState->Evaluate();

		IterationCount = InIterationCount;
		switch (InSolveStepType)
		{
		case ESolveStepType::ScaleSolve:          IterationSolveMessage = LOCTEXT("SolveStepScale",          "Scale Solve");            break;
		case ESolveStepType::BodySolve:           IterationSolveMessage = LOCTEXT("SolveStepBody",           "Body Solve");             break;
		case ESolveStepType::FaceSolve:           IterationSolveMessage = LOCTEXT("SolveStepFace",           "Face Solve");             break;
		case ESolveStepType::RefineVerticesSolve: IterationSolveMessage = LOCTEXT("SolveStepRefineVertices", "Refine Vertices Solve");  break;
		}
	}

	TWeakPtr<FMeshImportTaskRunner> WeakTaskRunnerPtr = AsWeak();
	Async(EAsyncExecution::TaskGraphMainTick, [WeakTaskRunnerPtr]()
	{
		if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = WeakTaskRunnerPtr.Pin())
		{
			FScopeLock ScopeLock(&ThisTaskRunner->UpdateVerticesMutex);
			constexpr float TotalIterations = 20.f;
			float Percentage = ThisTaskRunner->IterationCount / TotalIterations;
			ThisTaskRunner->MeshImportIterationDelegate.ExecuteIfBound(Percentage, ThisTaskRunner->IterationBodyVerticesAndNormals, ThisTaskRunner->IterationFaceVerticesAndNormals, ThisTaskRunner->IterationLocalJointPositions, ThisTaskRunner->IterationLocalJointRotations, ThisTaskRunner->IterationSolveMessage);
		}
	});
	return true;
}

void FMeshImportTaskRunner::OnProcessComplete(const TWeakPtr<FMeshImportTaskRunner>& TaskRunnerWeakPtr)
{
	if (TSharedPtr<FMeshImportTaskRunner> ThisTaskRunner = TaskRunnerWeakPtr.Pin())
	{
		if (ThisTaskRunner->bCancelled)
		{
			ThisTaskRunner->bSuccess = false;
		}
		
		ThisTaskRunner->MeshImportFinishDelegate.ExecuteIfBound(ThisTaskRunner->bSuccess,
			ThisTaskRunner->bCancelled,
			ThisTaskRunner->BodyState.ToSharedRef(), 
			ThisTaskRunner->FaceState.ToSharedRef(), 
			ThisTaskRunner->TargetMeshKey);
		
		ThisTaskRunner->BodyState->OnMeshConformIteration().Unbind();
	}
}

#undef LOCTEXT_NAMESPACE