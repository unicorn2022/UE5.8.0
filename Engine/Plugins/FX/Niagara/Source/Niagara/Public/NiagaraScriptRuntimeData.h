// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorVM.h"

#include "NiagaraScriptRuntimeCompiledData.h"
#include "NiagaraScriptRuntimeTransientData.h"

class FNiagaraScriptRuntimeData
{
public:
	FNiagaraScriptRuntimeData(
		TSharedRef<const FNiagaraScriptRuntimeCompiledDataHandle> InCompiledDataHandle,
		TSharedRef<const FNiagaraScriptRuntimeTransientData> InTransientData)
		: CompiledDataHandle(InCompiledDataHandle)
		, TransientData(InTransientData)
	{
	}

	TConstArrayView<FNiagaraScriptResolvedDataInterfaceInfo> GetResolvedDataInterfaces() const { return MakeConstArrayView(CompiledDataHandle->Instance->ResolvedDataInterfaces); }

	TConstArrayView<FNiagaraResolvedUserDataInterfaceBinding> GetResolvedUserDataInterfaceBindings() const { return MakeConstArrayView(CompiledDataHandle->Instance->ResolvedUserDataInterfaceBindings); }

	TConstArrayView<FNiagaraResolvedUObjectInfo> GetResolvedUObjects() const { return MakeConstArrayView(CompiledDataHandle->Instance->ResolvedUObjects); }

	const FNiagaraScriptExecutionParameterStore* GetExecutionReadyParameterStore() const { return TransientData->GetExecutionReadyParameterStore(); } 

	TConstArrayView<FVMExternalFunction> GetCalledVMExternalFunctionBindings() const { return TransientData->GetCalledVMExternalFunctionBindings(); }

private:
	TSharedPtr<const FNiagaraScriptRuntimeCompiledDataHandle> CompiledDataHandle;
	TSharedPtr<const FNiagaraScriptRuntimeTransientData> TransientData;
};

class FNiagaraScriptComputeRuntimeData : public FNiagaraScriptRuntimeData
{

};

class FNiagaraScriptEventRuntimeData : public FNiagaraScriptRuntimeData
{

};