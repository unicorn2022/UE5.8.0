// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraUIRendererProperties.h"
#include "NiagaraUIDebugRenderer.h"

FNiagaraRenderer* UNiagaraUIRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController)
{
#if WITH_NIAGARA_RENDERER_DEBUGDRAW 
	return new FNiagaraUIDebugRenderer(FeatureLevel, this, Emitter);
#else
	return nullptr;
#endif
}

FNiagaraBoundsCalculator* UNiagaraUIRendererProperties::CreateBoundsCalculator()
{
	return nullptr;
}

bool UNiagaraUIRendererProperties::IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const
{
	return InSimTarget == ENiagaraSimTarget::CPUSim;
}
