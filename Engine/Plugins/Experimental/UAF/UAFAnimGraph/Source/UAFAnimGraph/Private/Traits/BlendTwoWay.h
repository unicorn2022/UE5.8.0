// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TraitCore/Trait.h"
#include "TraitInterfaces/IContinuousBlend.h"
#include "TraitInterfaces/IEvaluate.h"
#include "TraitInterfaces/IHierarchy.h"
#include "TraitInterfaces/IUpdate.h"
#include "Traits/BlendTwoWayTraitData.h"

namespace UE::UAF
{
	/**
	 * FBlendTwoWayTrait
	 * 
	 * A trait that can blend two inputs.
	 */
	struct FBlendTwoWayTrait : FBaseTrait, IEvaluate, IUpdate, IUpdateTraversal, IHierarchy, IContinuousBlend
	{
		DECLARE_ANIM_TRAIT(FBlendTwoWayTrait, FBaseTrait)

		using FSharedData = FAnimNextBlendTwoWayTraitSharedData;

		struct FInstanceData : FTrait::FInstanceData
		{
			FTraitPtr ChildA;
			FTraitPtr ChildB;

			bool bIsChildANewlyRelevant : 1 = false;
			bool bIsChildBNewlyRelevant : 1 = false;
			
			bool bWasChildARelevant : 1 = false;
			bool bWasChildBRelevant : 1 = false;
		};

		// IEvaluate impl
		virtual void PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const override;

		// IUpdate impl
		virtual void PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const override;

		// IUpdateTraversal impl
		virtual void QueueChildrenForTraversal(FUpdateTraversalContext& Context, const TTraitBinding<IUpdateTraversal>& Binding, const FTraitUpdateState& TraitState, FUpdateTraversalQueue& TraversalQueue) const override;

		// IHierarchy impl
		virtual uint32 GetNumChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding) const override;
		virtual void GetChildren(const FExecutionContext& Context, const TTraitBinding<IHierarchy>& Binding, FChildrenArray& Children) const override;

		// IContinuousBlend impl
		virtual float GetBlendWeight(const FExecutionContext& Context, const TTraitBinding<IContinuousBlend>& Binding, int32 ChildIndex) const override;
	};
}
