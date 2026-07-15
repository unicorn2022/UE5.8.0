// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Traits/CachePoseTraitData.h"
#include "TraitCore/Trait.h"
#include "TraitInterfaces/IEvaluate.h"

namespace UE::UAF
{
struct FCachePoseTrait : FAdditiveTrait, IEvaluate
{
	DECLARE_ANIM_TRAIT(FCachePoseTrait, FAdditiveTrait)

	struct FInstanceData : FTrait::FInstanceData
	{
	};

	using FSharedData = FUAFCachePoseTraitSharedData;

	// IEvaluate impl
	virtual void PostEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const override;
	
#if WITH_EDITOR
	virtual bool IsHidden() const override { return true; };
#endif
};
}
