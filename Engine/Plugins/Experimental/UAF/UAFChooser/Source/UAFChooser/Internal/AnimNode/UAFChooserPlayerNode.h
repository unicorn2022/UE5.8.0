// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimationRuntime.h"
#include "AnimNextExecuteContext.h"
#include "ChooserPlayerTraitData.h"
#include "UAFAnimChooser.h"
#include "ChooserTypes.h"
#include "EvaluationVM/EvaluationProgram.h"
#include "Graph/RigUnit_AnimNextBase.h"
#include "UAF/AnimNodeCore/UAFAnimNodeDataEx.h"
#include "UAF/AnimNodeCore/UAFInlineAnimNode.h"
#include "UAF/AnimNodes/UAFBlendStack.h"
#include "UAF/AnimNodeCore/UAFTransitionNodeData.h"
#include "UAF/AnimNodes/IUAFAnimNodeDataHasAsset.h"

#include "UAFChooserPlayerNode.generated.h"

namespace UE::UAF::Chooser
{
	
struct FUAFChooserPlayerNodeData;
	
USTRUCT(BlueprintType)
struct FUAFChooserPlayerNodeSettings : public FUAFChooserPlayerSettings
{
	GENERATED_BODY()

	const FUAFAnimNodeData* AnimNodeData = nullptr;
};

class FUAFChooserPlayerNode : public FUAFAnimNode
{
public:
	UAFCHOOSER_API FUAFChooserPlayerNode(FUAFAnimGraphUpdateContext& Context, const UUAFAnimChooserTable* Chooser, EChooserEvaluationFrequency EvaluationFrequency = EChooserEvaluationFrequency::OnBecomeRelevant, const TInstancedStruct<FUAFTransitionNodeData>* TransitionData = nullptr);
	UAFCHOOSER_API FUAFChooserPlayerNode(FUAFAnimGraphUpdateContext& Context, const FUAFChooserPlayerNodeData* InData);

	UAFCHOOSER_API virtual void PreUpdate(FUAFAnimGraphUpdateContext& GraphContext) override;
	UAFCHOOSER_API virtual void* GetInterface(FUAFAnimNodeInterfaceId Id) override;
	UAFCHOOSER_API static void AddReferencedObjects(FUAFAnimNode* This, FReferenceCollector& Collector);

	// helper function for creating an instance from a Chooser.
	// if EvaluationFrequency is not set to OnLoop or OnUpdate, this functuin will evaluate the chooser immediately, and create an instance for the selected result
	UAFCHOOSER_API static FUAFAnimNodePtr CreateInstance(FUAFAnimGraphUpdateContext& Context,
		TObjectPtr<const UUAFAnimChooserTable> Chooser,
		EChooserEvaluationFrequency EvaluationFrequency = EChooserEvaluationFrequency::OnInitialUpdate,
		const TInstancedStruct<FUAFTransitionNodeData>* Transition = nullptr);
		
#if UAF_TRACE_ENABLED
	virtual FString GetDebugName() const override;
	virtual UStruct* GetDebugStruct() const override;
#endif

private:
	void ChooseNewAnimation(FUAFAnimGraphUpdateContext& Context);

	// properties for determining if a new selection is actually new
	const FUAFAnimNodeData* CurrentSelectionNodeData = nullptr;
	const FUAFGraphFactoryAsset* CurrentSelectionAssetData = nullptr;
	const UObject* CurrentSelectionObject = nullptr;
	float CurrentStartTime = 0;
	bool CurrentMirror = false;
	uint32 CurrentCurveOverridesHash = 0;

	float CachedCurrentTime = 0;
	TObjectPtr<const UUAFAnimChooserTable> Chooser;
	EChooserEvaluationFrequency EvaluationFrequency = EChooserEvaluationFrequency::OnBecomeRelevant;
	TInstancedStruct<FUAFTransitionNodeData> Transition;

	TUAFInlineAnimNode<FUAFBlendStack> BlendStack;
};

USTRUCT(DisplayName="Chooser Player")
struct FUAFChooserPlayerNodeData : public FUAFAnimNodeData
#if CPP
, public IUAFAnimNodeDataHasAsset
#endif
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Chooser")
	TObjectPtr<UUAFAnimChooserTable> Chooser;

	// How often the chooser should be evaluated
	UPROPERTY(EditAnywhere, Category = "Chooser")
	EChooserEvaluationFrequency EvaluationFrequency = EChooserEvaluationFrequency::OnBecomeRelevant;

	// How to transition between animations
	UPROPERTY(EditAnywhere, Category = "Chooser")
	TInstancedStruct<FUAFTransitionNodeData> Transition;

	virtual UObject* GetAsset() const override { return Chooser; }

	UAFCHOOSER_API virtual FUAFAnimNodePtr CreateInstance(FUAFAnimGraphUpdateContext& Context) const override;
	UAFCHOOSER_API virtual void* GetInterface(FUAFAnimNodeDataInterfaceId InterfaceId) override;
};

USTRUCT()
struct FAnimNodeResult : public FObjectChooserBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "NodeData", meta = (ExcludeBaseStruct))
	FUAFAnimNodeDataEx NodeData;

	// FObjectChooserBase interface
	virtual UObject* ChooseObject(FChooserEvaluationContext& Context) const final override;
	virtual EIteratorStatus IterateObjects(FChooserEvaluationContext& Context, FObjectChooserIteratorCallback Callback) const final override;

#if WITH_EDITOR
	virtual UObject* GetReferencedObject() const override
	{
		if (NodeData.IsValid())
		{
			if (const IUAFAnimNodeDataHasAsset* HasAsset = NodeData.Get()->GetInterface<IUAFAnimNodeDataHasAsset>())
			{
				return HasAsset->GetAsset();
			}
		}
		return nullptr;
	}
#endif
};
	
}