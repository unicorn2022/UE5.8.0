// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserParameters.h"
#include "UAFAssetInstance.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

namespace UE::UAF::Private
{

// Helper function to find the first data interface instance in the context 
FUAFAssetInstance* GetFirstUAFAssetInstance(FChooserEvaluationContext& Context)
{
	for(const FStructView& Param : Context.Params)
	{
		if(Param.GetScriptStruct()->IsChildOf(FUAFAssetInstance::StaticStruct()))
		{
			return Param.GetPtr<FUAFAssetInstance>();
		}
	}

	return nullptr;
}

template <typename ValueType>
EPropertyBagResult GetVariableValue(const FUAFAssetInstance* Instance, const FAnimNextVariableReference& Variable, ValueType& OutValue)
{
	while(Instance)
	{
		if (Instance->GetVariable(Variable, OutValue) == EPropertyBagResult::Success)
		{
			return EPropertyBagResult::Success;
		}
		
		Instance = Instance->GetHost();
	}
	
	return EPropertyBagResult::PropertyNotFound;
}

template <typename ValueType>
EPropertyBagResult SetVariableValue(const FUAFAssetInstance* Instance, const FAnimNextVariableReference& Variable, ValueType Value)
{
	while(Instance)
	{
		if (Instance->SetVariable(Variable, Value) == EPropertyBagResult::Success)
		{
			return EPropertyBagResult::Success;
		}
		
		Instance = Instance->GetHost();
	}
	
	return EPropertyBagResult::PropertyNotFound;
}

}

bool FBoolAnimProperty::GetValue(FChooserEvaluationContext& Context, bool& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
	{
		return UE::UAF::Private::GetVariableValue(Instance, Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FBoolAnimProperty::SetValue(FChooserEvaluationContext& Context, bool InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
	{
		return UE::UAF::Private::SetVariableValue(Instance, Variable, InValue) == EPropertyBagResult::Success;
	}
	return false;
}

void FBoolAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}

#if WITH_EDITORONLY_DATA
bool FBoolAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FBoolAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif

bool FFloatAnimProperty::GetValue(FChooserEvaluationContext& Context, double& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
	{
		return UE::UAF::Private::GetVariableValue(Instance, Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FFloatAnimProperty::SetValue(FChooserEvaluationContext& Context, double InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
 	{
		return UE::UAF::Private::SetVariableValue(Instance, Variable, InValue) == EPropertyBagResult::Success;
 	}
 	return false;
}

void FFloatAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}

#if WITH_EDITORONLY_DATA
bool FFloatAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FFloatAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif

bool FEnumAnimProperty::GetValue(FChooserEvaluationContext& Context, uint8& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
	{
		return UE::UAF::Private::GetVariableValue(Instance, Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FEnumAnimProperty::SetValue(FChooserEvaluationContext& Context, uint8 InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
	{
		return UE::UAF::Private::SetVariableValue(Instance, Variable, InValue) == EPropertyBagResult::Success;
	}
	return false;
}

void FEnumAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}

#if WITH_EDITORONLY_DATA
bool FEnumAnimProperty::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FEnumAnimProperty::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Variable = FAnimNextVariableReference(VariableName_DEPRECATED);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}
#endif


bool FNameAnimProperty::GetValue(FChooserEvaluationContext& Context, FName& OutResult) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
	{
		return UE::UAF::Private::GetVariableValue(Instance, Variable, OutResult) == EPropertyBagResult::Success;
	}
	return false;
}

bool FNameAnimProperty::SetValue(FChooserEvaluationContext& Context, const FName& InValue) const
{
	if(FUAFAssetInstance* Instance = UE::UAF::Private::GetFirstUAFAssetInstance(Context))
 	{
		return UE::UAF::Private::SetVariableValue(Instance, Variable, InValue) == EPropertyBagResult::Success;
 	}
 	return false;
}

void FNameAnimProperty::GetDisplayName(FText& OutName) const
{
	FString DisplayName = Variable.GetName().ToString();
	int Index = -1;
	DisplayName.FindLastChar(':', Index);
	if (Index>=0)
	{
		DisplayName = DisplayName.RightChop(Index + 1);
	}
	
	OutName = FText::FromString(DisplayName);
}