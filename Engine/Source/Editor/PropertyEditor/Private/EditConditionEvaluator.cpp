// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditConditionEvaluator.h"

#include "EditConditionContext.h"
#include "EditConditionParser.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"

//------------------------------------------------------------------------------
// Internal context implementation
//------------------------------------------------------------------------------

namespace
{
	/**
	 * IEditConditionContext implementation backed by raw struct memory.
	 * Allows FEditConditionParser to evaluate EditCondition expressions without
	 * a property node tree.  Function-call conditions are not supported (fail open).
	 */
	class FStructEditConditionContext : public IEditConditionContext
	{
	public:
		FStructEditConditionContext(const UScriptStruct* InStruct, const void* InMemory)
			: Struct(InStruct), Memory(InMemory)
		{}

		virtual FName GetContextName() const override
		{
			return Struct ? Struct->GetFName() : FName();
		}

		virtual TOptional<bool> GetBoolValue(const FString& PropertyName, TWeakObjectPtr<UFunction> /*CachedFunction*/ = nullptr) const override
		{
			if (const FBoolProperty* Prop = CastField<FBoolProperty>(FindProperty(PropertyName)))
			{
				return Prop->GetPropertyValue_InContainer(Memory);
			}
			return {};
		}

		virtual TOptional<int64> GetIntegerValue(const FString& PropertyName, TWeakObjectPtr<UFunction> /*CachedFunction*/ = nullptr) const override
		{
			const FProperty* Field = FindProperty(PropertyName);
			const FNumericProperty* NumProp = CastField<FNumericProperty>(Field);
			if (!NumProp)
			{
				if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Field))
				{
					NumProp = EnumProp->GetUnderlyingProperty();
				}
			}
			if (NumProp && NumProp->IsInteger())
			{
				// Use Field (the outermost property) for the container pointer — the underlying
				// property of an FEnumProperty has no independent container offset.
				const void* Addr = Field->ContainerPtrToValuePtr<void>(Memory);
				return NumProp->GetSignedIntPropertyValue(Addr);
			}
			return {};
		}

		virtual TOptional<double> GetNumericValue(const FString& PropertyName, TWeakObjectPtr<UFunction> /*CachedFunction*/ = nullptr) const override
		{
			if (const FNumericProperty* Prop = CastField<FNumericProperty>(FindProperty(PropertyName)))
			{
				const void* Addr = Prop->ContainerPtrToValuePtr<void>(Memory);
				if (Prop->IsInteger())
				{
					return (double)Prop->GetSignedIntPropertyValue(Addr);
				}
				if (Prop->IsFloatingPoint())
				{
					return Prop->GetFloatingPointPropertyValue(Addr);
				}
			}
			return {};
		}

		virtual TOptional<FString> GetEnumValue(const FString& PropertyName, TWeakObjectPtr<UFunction> /*CachedFunction*/ = nullptr) const override
		{
			const FProperty* Field = FindProperty(PropertyName);
			const UEnum* Enum = nullptr;
			const FNumericProperty* Underlying = nullptr;

			if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Field))
			{
				Enum = EnumProp->GetEnum();
				Underlying = EnumProp->GetUnderlyingProperty();
			}
			else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Field))
			{
				Enum = ByteProp->GetIntPropertyEnum();
				Underlying = ByteProp;
			}

			if (Enum && Underlying && Underlying->IsInteger())
			{
				// Use Field for the container pointer (see GetIntegerValue comment).
				const void* Addr = Field->ContainerPtrToValuePtr<void>(Memory);
				const int64 Value = Underlying->GetSignedIntPropertyValue(Addr);
				return Enum->GetNameStringByValue(Value);
			}
			return {};
		}

		virtual TOptional<UObject*> GetPointerValue(const FString& /*PropertyName*/, TWeakObjectPtr<UFunction> /*CachedFunction*/ = nullptr) const override
		{
			// Plain structs have no UObject context for pointer resolution.
			return {};
		}

		virtual TOptional<FString> GetTypeName(const FString& PropertyName, TWeakObjectPtr<UFunction> /*CachedFunction*/ = nullptr) const override
		{
			if (const FProperty* Field = FindProperty(PropertyName))
			{
				if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Field))
				{
					if (const UEnum* Enum = EnumProp->GetEnum())
					{
						return Enum->GetName();
					}
				}
				if (const FByteProperty* ByteProp = CastField<FByteProperty>(Field))
				{
					if (const UEnum* Enum = ByteProp->GetIntPropertyEnum())
					{
						return Enum->GetName();
					}
				}
				return Field->GetCPPType();
			}
			return {};
		}

		virtual TOptional<int64> GetIntegerValueOfEnum(const FString& EnumTypeName, const FString& MemberName) const override
		{
			if (const UEnum* EnumType = UClass::TryFindTypeSlow<UEnum>(EnumTypeName, EFindFirstObjectOptions::ExactClass))
			{
				const int64 Value = EnumType->GetValueByName(FName(*MemberName));
				return Value != INDEX_NONE ? TOptional<int64>(Value) : TOptional<int64>();
			}
			return {};
		}

		virtual const TWeakObjectPtr<UFunction> GetFunction(const FString& /*FieldName*/) const override
		{
			// Function-call conditions are not applicable to plain structs.
			return nullptr;
		}

	private:
		const FProperty* FindProperty(const FString& Name) const
		{
			return Struct ? Struct->FindPropertyByName(FName(*Name)) : nullptr;
		}

		const UScriptStruct* Struct;
		const void* Memory;
	};

} // namespace

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

bool EditConditionEvaluator::IsPropertyEditable(
	const FProperty* Property,
	const UScriptStruct* Struct,
	const void* Memory)
{
	const FString EditCondition = Property->GetMetaData(TEXT("EditCondition"));
	if (EditCondition.IsEmpty())
	{
		return true;
	}

	static const FEditConditionParser Parser;
	const TSharedPtr<FEditConditionExpression> Expr = Parser.Parse(EditCondition);
	if (!Expr.IsValid())
	{
		// Unparseable condition — fail open so the caller does not incorrectly suppress the property.
		return true;
	}

	const FStructEditConditionContext Context(Struct, Memory);
	const TValueOrError<bool, FText> Result = Parser.Evaluate(*Expr, Context);
	return !Result.HasValue() || Result.GetValue();
}
