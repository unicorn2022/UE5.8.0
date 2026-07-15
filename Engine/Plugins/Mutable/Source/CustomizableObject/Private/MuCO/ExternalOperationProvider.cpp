// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/ExternalOperationProvider.h"

#include "CustomizableObject/Internal/MuCO/CustomizableObjectPrivate.h"


namespace UE::Mutable::Private
{
	FExternalOperationProvider::FExternalOperationProvider(UModelResources& InModelResources)
	{
		ExternalOperations = InModelResources.ExternalOperations;
	}


	void FExternalOperationProvider::AddReferencedObjects(FReferenceCollector& Collector)
	{
		for (TPair<FOperation::ADDRESS, FInstancedStruct>& Pair : ExternalOperations)
		{
			if (const UScriptStruct* ScriptStruct = Pair.Value.GetScriptStruct())
			{
				Collector.AddPropertyReferences(ScriptStruct, Pair.Value.GetMutableMemory());
			}
		}
	}


	const FInstancedStruct& FExternalOperationProvider::Get(FOperation::ADDRESS Address) const
	{
		return ExternalOperations[Address];
	}
}
