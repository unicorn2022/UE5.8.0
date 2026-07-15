// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/KismetMathLibrary.h"
#include "TraitCore/Trait.h"
#include "TraitCore/TraitSharedData.h"
#include "TraitInterfaces/IAttributeProvider.h"
#include "TraitInterfaces/IEvaluate.h"
#include "TraitInterfaces/ITimelinePlayer.h"
#include "TraitInterfaces/IUpdate.h"

#include "Chooser.h"
#include "TraitInterfaces/IBlendStack.h"
#include "ChooserPlayerTraitData.h"
#include "TraitInterfaces/IGarbageCollection.h"

namespace UE::UAF
{
/**
 * this trait will Evaluate a Chooser, and play an animation graph constructed from whatever asset the chooser returns 
 */
struct FChooserPlayerTrait : FAdditiveTrait, IUpdate, IGarbageCollection
{
	DECLARE_ANIM_TRAIT(FChooserPlayerTrait, FAdditiveTrait)

	using FSharedData = FChooserPlayerData;

	struct FInstanceData : FTrait::FInstanceData
	{
		void Construct(const FExecutionContext& Context, const FTraitBinding& Binding);
		void Destruct(const FExecutionContext& Context, const FTraitBinding& Binding);

		TObjectPtr<UObject> CurrentSelection = nullptr;
		float CurrentStartTime = 0;
		bool CurrentMirror = false;
		uint32 CurrentCurveOverridesHash = 0;

		float CachedTimeLeft = 0;
	};

	void EvaluateChooser(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const;
	// IUpdate impl
	virtual void OnBecomeRelevant(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const override;
	virtual void PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const override;

	// IGarbageCollection impl
	virtual void AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const override;
};
	
} // namespace UE::UAF