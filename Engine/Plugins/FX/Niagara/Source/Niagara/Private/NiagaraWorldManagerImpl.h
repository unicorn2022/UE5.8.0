// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraWorldManager.h"
#include "NiagaraDataManager.h"

template<typename T>
T& FNiagaraWorldManager::GetOrCreateDataManager()
{
	FName ManagerName = T::StaticClass()->GetFName();
	T* Ret = nullptr;
	if(TObjectPtr<UNiagaraDataManager>* Found = DataManagers.Find(ManagerName))
	{
		Ret = Cast<T>(Found->Get());
	}

	if(Ret == nullptr)
	{
		TObjectPtr<UNiagaraDataManager>& NewMan = DataManagers.Add(ManagerName);
		NewMan = NewObject<T>(GetTransientPackageAsObject(), ManagerName);
		NewMan->Init(this);
		Ret = CastChecked<T>(NewMan.Get());
	}

	check(Ret);
	return *Ret;
}