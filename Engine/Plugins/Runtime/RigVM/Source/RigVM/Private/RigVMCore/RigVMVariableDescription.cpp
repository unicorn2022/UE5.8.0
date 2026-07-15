// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMVariableDescription.h"
#include "RigVMCore/RigVMExternalVariable.h"
#include "StructUtils/PropertyBag.h"

FRigVMExternalVariable FRigVMGraphVariableDescription::ToExternalVariable() const
{
	return RigVMVariableUtils::ExternalVariableFromRigVMVariableDescription(*this);
}

bool FRigVMGraphVariableDescription::ChangeType(const FString& InCPPType, UObject* InCPPTypeObject)
{
	CPPType = InCPPType;
	CPPTypeObject = InCPPTypeObject;
	if (CPPTypeObject)
	{
		CPPTypeObjectPath = *CPPTypeObject->GetPathName();
	}
	else
	{
		CPPTypeObjectPath = NAME_None;
	}
	return true;
}

FRigVMExternalVariable RigVMVariableUtils::ExternalVariableFromRigVMVariableDescription(const FRigVMGraphVariableDescription& InVariableDescription)
{
	FString CPPTypeName = InVariableDescription.CPPType;
	if (UEnum* Enum = Cast<UEnum>(InVariableDescription.CPPTypeObject))
	{
		CPPTypeName = Enum->GetName();
		if (RigVMTypeUtils::IsArrayType(InVariableDescription.CPPType))
		{
			CPPTypeName = RigVMTypeUtils::ArrayTypeFromBaseType(CPPTypeName);
		}
	}

	FRigVMExternalVariable Variable = FRigVMExternalVariable::Make(
		InVariableDescription.Guid,
		InVariableDescription.Name,
		CPPTypeName,
		InVariableDescription.CPPTypeObject,
		InVariableDescription.bPublic,
		false);
	return Variable;
}

FRigVMGraphVariableDescription RigVMVariableUtils::VariableDescriptionFromPropertyDesc(const FPropertyBagPropertyDesc& InPropertyDesc)
{
	FRigVMGraphVariableDescription Variable;

#if WITH_EDITOR
	Variable.Name = *InPropertyDesc.GetMetaData(TEXT("DisplayName"));
#else
	Variable.Name = InPropertyDesc.Name;
#endif

	switch (InPropertyDesc.ValueType)
	{
	case EPropertyBagPropertyType::Bool: { Variable.CPPType = RigVMTypeUtils::BoolType; break; }
	case EPropertyBagPropertyType::Byte: { Variable.CPPType = RigVMTypeUtils::UInt8Type; break; }
	case EPropertyBagPropertyType::Int32: { Variable.CPPType = RigVMTypeUtils::Int32Type; break; }
	case EPropertyBagPropertyType::UInt32: { Variable.CPPType = RigVMTypeUtils::UInt32Type; break; }
	case EPropertyBagPropertyType::Int64: { Variable.CPPType = RigVMTypeUtils::Int64Type; break; }
	case EPropertyBagPropertyType::UInt64: { Variable.CPPType = RigVMTypeUtils::UInt64Type; break; }
	case EPropertyBagPropertyType::Float: { Variable.CPPType = RigVMTypeUtils::FloatType; break; }
	case EPropertyBagPropertyType::Double: { Variable.CPPType = RigVMTypeUtils::DoubleType; break; }
	case EPropertyBagPropertyType::Name: { Variable.CPPType = RigVMTypeUtils::FNameType; break; }
	case EPropertyBagPropertyType::String: { Variable.CPPType = RigVMTypeUtils::FStringType; break; }
	case EPropertyBagPropertyType::Text: { Variable.CPPType = RigVMTypeUtils::FTextType; break; }
	case EPropertyBagPropertyType::Struct:
		{
			const UScriptStruct* Struct = CastChecked<const UScriptStruct>(InPropertyDesc.ValueTypeObject);
			Variable.CPPType = RigVMTypeUtils::GetUniqueStructTypeName(Struct);
			break;
		}
	case EPropertyBagPropertyType::Enum:
		{
			const UEnum* Enum = CastChecked<const UEnum>(InPropertyDesc.ValueTypeObject);
			Variable.CPPType = *RigVMTypeUtils::CPPTypeFromEnum(Enum);
			break;
		}
	case EPropertyBagPropertyType::Class:
		{
			Variable.CPPType = RigVMTypeUtils::CPPTypeFromObject(InPropertyDesc.ValueTypeObject, RigVMTypeUtils::EClassArgType::AsClass);
			break;
		}
	case EPropertyBagPropertyType::Object:
		{
			Variable.CPPType = RigVMTypeUtils::CPPTypeFromObject(InPropertyDesc.ValueTypeObject, RigVMTypeUtils::EClassArgType::AsObject);
			break;
		}
	}
	for (uint32 i=0; i<InPropertyDesc.ContainerTypes.Num(); ++i)
	{
		if (InPropertyDesc.ContainerTypes[i] == EPropertyBagContainerType::Array)
		{
			Variable.CPPType = RigVMTypeUtils::ArrayTypeFromBaseType(Variable.CPPType);
		}
	}

	if (InPropertyDesc.ValueTypeObject != nullptr)
	{
		Variable.CPPTypeObjectPath = *InPropertyDesc.ValueTypeObject.GetPathName();
		Variable.CPPTypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath(Variable.CPPTypeObjectPath.ToString());
	}

#if WITH_EDITOR
	if(InPropertyDesc.HasMetaData(FBlueprintMetadata::MD_Tooltip))
	{
		Variable.Tooltip = FText::FromString(InPropertyDesc.GetMetaData(FBlueprintMetadata::MD_Tooltip));
	}
	if(InPropertyDesc.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn))
	{
		Variable.bExposedOnSpawn = InPropertyDesc.GetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) == TEXT("true");
	}
	if(InPropertyDesc.HasMetaData(FBlueprintMetadata::MD_Private))
	{
		Variable.bPrivate = InPropertyDesc.GetMetaData(FBlueprintMetadata::MD_Private) == TEXT("true");
	}
	if(InPropertyDesc.HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
	{
		Variable.Category = FText::FromString(InPropertyDesc.GetMetaData(FBlueprintMetadata::MD_FunctionCategory));
	}
#endif
	if (InPropertyDesc.PropertyFlags & CPF_Interp)
	{
		Variable.bExposeToCinematics = true;
	}
	if ((InPropertyDesc.PropertyFlags & CPF_DisableEditOnInstance) == 0)
	{
		Variable.bPublic = true;
	}
	Variable.Guid = InPropertyDesc.ID;
	return Variable;
}

FRigVMPropertyDescription RigVMVariableUtils::PropertyDescriptionFromVariableDescription(const FRigVMGraphVariableDescription& InVariable, const bool bAllowSpacesInName)
{
	EPropertyFlags PropertyFlags = CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance | CPF_Interp;
	if (InVariable.bPublic)
	{
		PropertyFlags &= ~CPF_DisableEditOnInstance;
	}
	if (InVariable.bExposeToCinematics)
	{
		PropertyFlags |= CPF_Interp;
	}

#if WITH_EDITOR
	TMap<FName, FString> MetaData;
	MetaData.Add(FBlueprintMetadata::MD_Tooltip, InVariable.Tooltip.ToString());
	MetaData.Add(FBlueprintMetadata::MD_FunctionCategory, InVariable.Category.ToString());
	if (InVariable.bExposedOnSpawn)
	{
		MetaData.Add(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
	}
	if (InVariable.bPrivate)
	{
		MetaData.Add(FBlueprintMetadata::MD_Private, TEXT("true"));
	}
	FRigVMPropertyDescription Result(InVariable.Name, InVariable.CPPType, InVariable.CPPTypeObject, InVariable.DefaultValue, PropertyFlags, MetaData, bAllowSpacesInName);
#else
	FRigVMPropertyDescription Result(InVariable.Name, InVariable.CPPType, InVariable.CPPTypeObject, InVariable.DefaultValue, PropertyFlags, bAllowSpacesInName);
#endif
	Result.Guid = InVariable.Guid;
	return Result;
}
