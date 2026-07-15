// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserPlayerTrait.h"

#include "Factory/AnimGraphFactory.h"
#include "TraitCore/NodeInstance.h"
#include "TraitInterfaces/IGraphFactory.h"
#include "TraitInterfaces/ITimeline.h"
#include "Module/AnimNextModuleInstance.h"
#include "TraitInterfaces/IHierarchy.h"
#include "Traits/BlendSpacePlayerTraitData.h"
#include "Traits/SequencePlayerTraitData.h"
#include "UAF/UAFAssetFactory.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FChooserPlayerTrait

namespace UE::UAF
{
	AUTO_REGISTER_ANIM_TRAIT(FChooserPlayerTrait)

	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IUpdate) \
		GeneratorMacro(IGarbageCollection) 

	// Trait required interfaces implementation boilerplate
    #define TRAIT_REQUIRED_INTERFACE_ENUMERATOR(GeneratorMacroRequired) \
    	GeneratorMacroRequired(IBlendStack) \

	// Trait implementation boilerplate
	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FChooserPlayerTrait, TRAIT_INTERFACE_ENUMERATOR, TRAIT_REQUIRED_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR
	
	void FChooserPlayerTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);
	}

	void FChooserPlayerTrait::FInstanceData::Destruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);
	}

	void FChooserPlayerTrait::EvaluateChooser(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
        check(SharedData);
        
       	FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
       	check(InstanceData);
		
		const UUAFAnimChooserTable* Chooser = SharedData->GetChooser(Binding);
        	
		if (Chooser)
		{
			FChooserEvaluationContext ChooserContext;
			ChooserContext.AddStructViewParam(FStructView::Make(Binding.GetTraitPtr().GetNodeInstance()->GetOwner()));

			FUAFChooserPlayerSettings Settings;
			
			Settings.PlayingAsset = InstanceData->CurrentSelection;

			TTraitBinding<ITimeline> TimelineTrait;
			FTimelineState TimelineState;
			FTraitStackBinding StackBinding;
			if (IHierarchy::GetForwardedStackInterface<ITimeline>(Context, Binding, StackBinding, TimelineTrait) && TimelineTrait.GetState(Context, TimelineState))
			{
				// @TODO: This won't work well with blendspaces and Motion Matching.
				Settings.PlayingAssetAccumulatedTime = TimelineState.GetPosition();
			}

			// @TODO: Add support for mirroring and blendspace parameters. 
			// Consider piping interfaces into Chooser context to allow columns to query interfaces directly i.e. Pose Search.
			//Settings.bIsPlayingAssetMirrored = ???;
			//Settings.PlayingAssetBlendParameters = ???;

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

			// todo: update to match the ABP version:

			// Restart the animation:
			// - if we've been told to do so via bForceBlendTo
			// - if this node just became relevant
			// - if we chose a new animation
			// - if the mirror setting has changed
			// - for playback rate of 0, when the start time changes (within Settings.MinDeltaTimeToForceBlendTo seconds of tolerance) - for choosing poses as frames of an animation sequence - @todo: handle looping properly
			// - if the curve values are different

			if (Settings.bForceBlendTo || Result != InstanceData->CurrentSelection ||
			 	InstanceData->CurrentMirror != Settings.bMirror ||
			 	(!FMath::IsNearlyEqual(InstanceData->CurrentStartTime, Settings.StartTime, Settings.MinDeltaTimeToForceBlendTo) && Settings.PlaybackRate == 0.0f) ||
			 	InstanceData->CurrentCurveOverridesHash != Settings.CurveOverrides.Hash)
			{
				IBlendStack::FGraphRequest GraphRequest;
				
				const UUAFAnimGraph* AnimationGraph = nullptr;

				if (Settings.AssetData.IsValid())
				{
					AnimationGraph = IGraphFactory::GetOrBuildGraph(Context, Binding, Settings.AssetData, GraphRequest.FactoryParams);
				}
				else if (Result != nullptr)
				{
					// We have no asset data handle but have a UObject result... try to make a graph from the UObject
					AnimationGraph = IGraphFactory::GetOrBuildGraph(Context, Binding, Result, GraphRequest.FactoryParams);
				}
				
				if (AnimationGraph != nullptr)
				{
					TTraitBinding<IBlendStack> BlendStackTrait;
					Binding.GetStackInterface(BlendStackTrait);

					GraphRequest.FactoryObject = Result;
					GraphRequest.AnimationGraph = AnimationGraph;
					GraphRequest.BlendArgs.BlendTime = Settings.BlendTime;
					GraphRequest.BlendArgs.BlendOption = Settings.BlendOption;

					BlendStackTrait.PushGraph(Context, MoveTemp(GraphRequest));

					FAnimNextGraphInstance* ActiveGraphInstance;
					BlendStackTrait.GetActiveGraphInstance(Context, ActiveGraphInstance);

					// Set start time etc on new graph
					if (ActiveGraphInstance)
					{
						ActiveGraphInstance->AccessVariablesStruct<FSequencePlayerData>([&Settings](FSequencePlayerData& InSequencePlayer)
						{
							InSequencePlayer.PlayRate = Settings.PlaybackRate;
							InSequencePlayer.StartPosition = Settings.StartTime;
						});

						ActiveGraphInstance->AccessVariablesStruct<FBlendSpacePlayerData>([&Settings](FBlendSpacePlayerData& InBlendSpacePlayer)
						{
							InBlendSpacePlayer.PlayRate = Settings.PlaybackRate;
							InBlendSpacePlayer.StartPosition = Settings.StartTime;
							InBlendSpacePlayer.XAxisSamplePoint = Settings.PlayingAssetBlendParameters.X;
							InBlendSpacePlayer.YAxisSamplePoint = Settings.PlayingAssetBlendParameters.Y;
						});
					}
				}
				
				InstanceData->CurrentSelection = Result;
				InstanceData->CurrentMirror = Settings.bMirror;
				InstanceData->CurrentStartTime = Settings.StartTime;
				InstanceData->CurrentCurveOverridesHash = Settings.CurveOverrides.Hash;
			}
		}
	}
	
	void FChooserPlayerTrait::OnBecomeRelevant(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		IUpdate::OnBecomeRelevant(Context, Binding, TraitState);
		
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
        check(SharedData);
			
		if (SharedData->GetEvaluationFrequency(Binding) == EChooserEvaluationFrequency::OnBecomeRelevant)
		{
			EvaluateChooser(Context, Binding, TraitState);
		}
	}
	
	void FChooserPlayerTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		IUpdate::PreUpdate(Context, Binding, TraitState);
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		check(SharedData);
        
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		check(InstanceData);

		EChooserEvaluationFrequency EvaluationFrequency = SharedData->GetEvaluationFrequency(Binding);
	
		if (InstanceData->CurrentSelection == nullptr || EvaluationFrequency == EChooserEvaluationFrequency::OnUpdate)
		{
			EvaluateChooser(Context, Binding, TraitState);
		}
		else if (EvaluationFrequency == EChooserEvaluationFrequency::OnLoop)
		{
			TTraitBinding<IBlendStack> BlendStackTrait;
         	Binding.GetStackInterface(BlendStackTrait);

			TTraitBinding<ITimeline> TimelineTrait;
			FTimelineState TimelineState;
			FTraitStackBinding StackBinding;
			if (IHierarchy::GetForwardedStackInterface<ITimeline>(Context, Binding, StackBinding, TimelineTrait) && TimelineTrait.GetState(Context, TimelineState))
			{
				const float ChildTimeLeft = TimelineState.GetTimeLeft();

				if (ChildTimeLeft > InstanceData->CachedTimeLeft)
				{
					EvaluateChooser(Context, Binding, TraitState);
				}

				InstanceData->CachedTimeLeft = ChildTimeLeft;
			}
		}
	}

	void FChooserPlayerTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		check(SharedData);
		if (TObjectPtr<const UObject> Chooser = SharedData->GetChooser(Binding))
		{
			Collector.AddReferencedObject(Chooser);
		}
	}
} // namespace UE::UAF
