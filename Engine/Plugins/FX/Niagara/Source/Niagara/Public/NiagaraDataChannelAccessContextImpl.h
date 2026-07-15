// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataChannelAccessContext.h"

template<typename TAction>
void FNDCAccessContext::ForEachSpawnedSystem(TAction Func) const
{
	for ( const FNDCSpawnedSystemRef& SysRef : SpawnedSystems )
	{
		if (UNiagaraComponent* Comp  = SysRef.Get())
		{
			Func(Comp);
		}
	}
}

template<typename TContainerType>
void FNDCAccessContext::GetSpawnedSystems(TContainerType& Container)const
{
	for ( const FNDCSpawnedSystemRef& SysRef : SpawnedSystems )
	{
		if (UNiagaraComponent* Comp  = SysRef.Get())
		{
			Container.Add(Comp);
		}
	}
}