// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNode/UAFChooserPlayerNode.h"

#include <UAF/AnimNodeCore/UAFAnimNodeFactory.h>
#include <UAF/AnimNodeCore/UAFGraphFactoryAssetAnimNodeFactory.h>

#include "Module/AnimNextModuleInstance.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "UAF/AnimNodeCore/UAFAnimNodeUpdate.h"
#include "UAF/AnimNodes/IUAFAnimNodeTimeline.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFChooserPlayerNode)

namespace UE::UAF::Chooser
{
FUAFChooserPlayerNode::FUAFChooserPlayerNode(FUAFAnimGraphUpdateContext& Context, const UUAFAnimChooserTable* InChooser, EChooserEvaluationFrequency InEvaluationFrequency, const TInstancedStruct<FUAFTransitionNodeData>* TransitionData)
	: FUAFAnimNode(Context), BlendStack(Context)
{
	InitializeAs<FUAFChooserPlayerNode>(Context);
	
	EvaluationFrequency = InEvaluationFrequency;
	Chooser = InChooser;

	// todo: we should figure out how to not duplicate this data
	if (TransitionData)
	{
		Transition = *TransitionData;
	}

	AddChild(BlendStack.Get());
}

FUAFChooserPlayerNode::FUAFChooserPlayerNode(FUAFAnimGraphUpdateContext& Context, const FUAFChooserPlayerNodeData* InData)
	: FUAFAnimNode(Context), BlendStack(Context)
{
	InitializeAs<FUAFChooserPlayerNode>(Context);

	EvaluationFrequency = InData->EvaluationFrequency;
	Chooser = InData->Chooser;
	Transition = InData->Transition;
	
	AddChild(BlendStack.Get());
}

void FUAFChooserPlayerNode::ChooseNewAnimation(FUAFAnimGraphUpdateContext& Context)
{
	FChooserEvaluationContext ChooserContext;
	ChooserContext.AddStructViewParam(FStructView::Make(*Context.GetVariablesOwner()));

	FUAFChooserPlayerNodeSettings Settings;
	ChooserContext.AddStructViewParam(FStructView::Make(Settings));

	// Add outer object, only for Debugging
	ChooserContext.AddObjectParam(const_cast<UObject*>(Context.GetHostObject()));

	if (CurrentSelectionObject)
	{
		Settings.PlayingAsset = CurrentSelectionObject;
	}
	else if (CurrentSelectionNodeData)
	{
		if (const IUAFAnimNodeDataHasAsset* HasAsset = CurrentSelectionNodeData->GetInterface<IUAFAnimNodeDataHasAsset>())
		{
			Settings.PlayingAsset = HasAsset->GetAsset();
		}
	}

	if (const IUAFAnimNodeTimeline* Timeline = BlendStack->FUAFAnimNode::GetInterface<IUAFAnimNodeTimeline>())
	{
		// @TODO: This won't work well with blendspaces and Motion Matching.
		Settings.PlayingAssetAccumulatedTime = Timeline->GetCurrentTime();
	}

	UObject* Result = nullptr;
	UChooserTable::EvaluateChooser(ChooserContext, Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&Result](UObject* InResult)
	{
		if (InResult)
		{
			Result = InResult;
			return FObjectChooserBase::EIteratorStatus::Stop;
		}
		return FObjectChooserBase::EIteratorStatus::Failed;
	}));

	// if we have a NodeData to create an instance from, or an Object to call into the Factory to create an instance
	if (Settings.AnimNodeData || Settings.AssetData.IsValid() || Result)
	{
		// check if this result is different from the most recently selected one
		if (Settings.bForceBlendTo || Result != CurrentSelectionObject || Settings.AnimNodeData != CurrentSelectionNodeData || Settings.AssetData.GetPtr() != CurrentSelectionAssetData || 
				 CurrentMirror != Settings.bMirror ||
				 (!FMath::IsNearlyEqual(CurrentStartTime, Settings.StartTime, Settings.MinDeltaTimeToForceBlendTo) && Settings.PlaybackRate == 0.0f) ||
				 CurrentCurveOverridesHash != Settings.CurveOverrides.Hash)
		{
			FUAFAnimNodePtr TargetChild;
			if (Settings.AnimNodeData)
			{
				TargetChild = Settings.AnimNodeData->CreateInstance(Context);
			}
			else if (Settings.AssetData.IsValid())
			{
				TargetChild = FUAFGraphFactoryAssetAnimNodeFactory::CreateUAFAnimNodeFromObject(Settings.AssetData, Context);
			}
			else
			{
				TargetChild = FUAFAnimNodeFactory::CreateUAFAnimNodeFromObject(Result, Context);
			}

			if (ensure(TargetChild))
			{
				if (IUAFAnimNodeTimeline* Timeline = TargetChild->GetInterface<IUAFAnimNodeTimeline>())
				{
					// todo: move this to controller creation parameters
					Timeline->SetCurrentTime(Settings.StartTime);
				}
				
				BlendStack->TransitionTo(Context, TargetChild, Transition.GetPtr());

				CurrentSelectionObject = Result;
				CurrentSelectionNodeData = Settings.AnimNodeData;
				CurrentSelectionAssetData = Settings.AssetData.GetPtr();
				CurrentStartTime = Settings.StartTime;
				CurrentMirror = Settings.bMirror;
				CurrentStartTime = Settings.StartTime;
				CurrentCurveOverridesHash = Settings.CurveOverrides.Hash;
			}
		}
	}
}

void FUAFChooserPlayerNode::PreUpdate(FUAFAnimGraphUpdateContext& GraphContext)
{
	SCOPED_NAMED_EVENT(AnimNode_ChooserPlayer_Update, FColor::Red);

	UAF_TRACE_ANIMNODE_VALUE(GraphContext, this, "Asset", Chooser);

	if (Chooser)
	{
		if (!BlendStack->HasValidChild() || EvaluationFrequency == EChooserEvaluationFrequency::OnUpdate)
		{
			ChooseNewAnimation(GraphContext);
		}
		else if (EvaluationFrequency == EChooserEvaluationFrequency::OnLoop)
		{
			if (const IUAFAnimNodeTimeline* Timeline = BlendStack->FUAFAnimNode::GetInterface<IUAFAnimNodeTimeline>())
			{
				ETypeAdvanceAnim AdvanceResult = Timeline->GetTimeAdvanceResult();
				if (AdvanceResult == ETAA_Looped || AdvanceResult ==  ETAA_Finished)
				{
					ChooseNewAnimation(GraphContext);
				}
			}
		}
	}
}

void* FUAFChooserPlayerNode::GetInterface(FUAFAnimNodeInterfaceId Id)
{
	return BlendStack->GetInterface(Id);
}

void FUAFChooserPlayerNode::AddReferencedObjects(FUAFAnimNode* This, FReferenceCollector& Collector)
{
	FUAFChooserPlayerNode* That = static_cast<FUAFChooserPlayerNode*>(This);
	if (That->Chooser)
	{
		Collector.AddReferencedObject(That->Chooser);
	}
}
	
#if UAF_TRACE_ENABLED
FString FUAFChooserPlayerNode::GetDebugName() const
{
	if (Chooser)
	{
		return Chooser->GetName();
	}
	else
	{
		return "Chooser Player";
	}
}
	
UStruct* FUAFChooserPlayerNode::GetDebugStruct() const
{
	return FUAFChooserPlayerNodeData::StaticStruct();
}
#endif

FUAFAnimNodePtr FUAFChooserPlayerNode::CreateInstance(FUAFAnimGraphUpdateContext& Context,
    		TObjectPtr<const UUAFAnimChooserTable> Chooser,
    		EChooserEvaluationFrequency EvaluationFrequency,
    		const TInstancedStruct<FUAFTransitionNodeData>* Transition)
{
	if (EvaluationFrequency == EChooserEvaluationFrequency::OnInitialUpdate || EvaluationFrequency == EChooserEvaluationFrequency::OnBecomeRelevant )
	{
		// evaluate immediately
		if (Chooser)
		{
			FChooserEvaluationContext ChooserContext;
			ChooserContext.AddStructViewParam(FStructView::Make(*Context.GetVariablesOwner()));
		
			FUAFChooserPlayerNodeSettings Settings;
			ChooserContext.AddStructViewParam(FStructView::Make(Settings));
			
			// Add outer object, only for Debugging
			ChooserContext.AddObjectParam(const_cast<UObject*>(Context.GetHostObject()));

			UObject* Result = nullptr;
			UChooserTable::EvaluateChooser(ChooserContext, Chooser, FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&Result](UObject* InResult)
			{
				if (InResult)
				{
					Result = InResult;
					return FObjectChooserBase::EIteratorStatus::Stop;
				}
				return FObjectChooserBase::EIteratorStatus::Failed;
			}));

			FUAFAnimNodePtr Instance;
			if (Settings.AnimNodeData)
			{
				Instance = Settings.AnimNodeData->CreateInstance(Context);
			}
			else if (Settings.AssetData.IsValid())
			{
				Instance = FUAFGraphFactoryAssetAnimNodeFactory::CreateUAFAnimNodeFromObject(Settings.AssetData, Context);
			}
			else if (Result)
			{
				Instance = FUAFAnimNodeFactory::CreateUAFAnimNodeFromObject(Result, Context);
			}
			
			if (Instance)
			{
				if (IUAFAnimNodeTimeline* Timeline = Instance->GetInterface<IUAFAnimNodeTimeline>())
				{
					Timeline->SetCurrentTime(Settings.StartTime);
				}
				return Instance;
			}
		}
	}
	
	return MakeAnimNode<FUAFChooserPlayerNode>(Context, Chooser, EvaluationFrequency, Transition);
}

FUAFAnimNodePtr FUAFChooserPlayerNodeData::CreateInstance(FUAFAnimGraphUpdateContext& Context) const
{
	return FUAFChooserPlayerNode::CreateInstance(Context, Chooser, EvaluationFrequency, &Transition);

}

void* FUAFChooserPlayerNodeData::GetInterface(FUAFAnimNodeDataInterfaceId Id)
{
	if (Id == IUAFAnimNodeDataHasAsset::InterfaceId)
	{
		return static_cast<IUAFAnimNodeDataHasAsset*>(this);
	}
	return nullptr;
}

UObject* FAnimNodeResult::ChooseObject(FChooserEvaluationContext& Context) const
{
	if (const FUAFAnimNodeData* NodeDataPtr = NodeData.Get().GetPtr())
	{
		if (Context.Params.Num() >= 2)
		{
			if (FUAFChooserPlayerNodeSettings* Settings = Context.Params[1].GetPtr<FUAFChooserPlayerNodeSettings>())
			{
				Settings->AnimNodeData = NodeDataPtr;
			}
		}
		
		if (const IUAFAnimNodeDataHasAsset* HasAsset = NodeDataPtr->GetInterface<const IUAFAnimNodeDataHasAsset>())
		{
			return HasAsset->GetAsset();
		}

		// still considered a success since we selected an FAnimNodeData, so we have to return non-null even though we don't have an Object to return
		return UObject::StaticClass();
	}

	return nullptr;
}

FAnimNodeResult::EIteratorStatus FAnimNodeResult::IterateObjects(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const
{
	if (Context.Params.Num() >= 2)
	{
		if (FUAFChooserPlayerNodeSettings* Settings = Context.Params[1].GetPtr<FUAFChooserPlayerNodeSettings>())
		{
			Settings->AnimNodeData = NodeData.Get().GetPtr();
		}
	}
	if (NodeData.IsValid())
	{
		if (const IUAFAnimNodeDataHasAsset* HasAsset = NodeData.Get()->GetInterface<IUAFAnimNodeDataHasAsset>())
		{
			return Callback.Execute(HasAsset->GetAsset());
		}
	}
	return EIteratorStatus::Continue;
}
	
}
