// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IChooserParameterBool.h"
#include "IChooserParameterFloat.h"
#include "IChooserParameterEnum.h"
#include "ChooserParameterName.h"
#include "Variables/AnimNextVariableReference.h"
#include "Variables/AnimNextSharedVariables.h"
#include "ChooserParameters.generated.h"

USTRUCT(DisplayName = "Bool Anim Param")
struct FBoolAnimProperty :  public FChooserParameterBoolBase
{
	GENERATED_BODY()
public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName VariableName_DEPRECATED;
#endif
	
	UPROPERTY(DisplayName = "Variable", EditAnywhere, Category="Variable", meta = (AllowedType = "bool"))
	FAnimNextVariableReference Variable;

	virtual bool GetValue(FChooserEvaluationContext& Context, bool& OutResult) const override;
	virtual bool SetValue(FChooserEvaluationContext& Context, bool InValue) const override;
	virtual void GetDisplayName(FText& OutName) const override;
	
#if WITH_EDITORONLY_DATA
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
#endif
};

template<>
struct TStructOpsTypeTraits<FBoolAnimProperty> : public TStructOpsTypeTraitsBase2<FBoolAnimProperty>
{
#if WITH_EDITORONLY_DATA
	enum
	{
		WithSerializer = true,
		WithPostSerialize = true,
	};
#endif
};

USTRUCT(DisplayName = "Float Anim Param")
struct FFloatAnimProperty :  public FChooserParameterFloatBase
{
	GENERATED_BODY()
public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName VariableName_DEPRECATED;
#endif

	UPROPERTY(DisplayName = "Variable", EditAnywhere, Category="Variable", meta = (AllowedType = "float"))
	FAnimNextVariableReference Variable;

	virtual bool GetValue(FChooserEvaluationContext& Context, double& OutResult) const override;
	virtual bool SetValue(FChooserEvaluationContext& Context, double InValue) const override;
	virtual void GetDisplayName(FText& OutName) const override;

#if WITH_EDITORONLY_DATA
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
#endif
};

template<>
struct TStructOpsTypeTraits<FFloatAnimProperty> : public TStructOpsTypeTraitsBase2<FFloatAnimProperty>
{
#if WITH_EDITORONLY_DATA
	enum
	{
		WithSerializer = true,
		WithPostSerialize = true,
	};
#endif
};

USTRUCT(DisplayName = "Enum Anim Param")
struct FEnumAnimProperty :  public FChooserParameterEnumBase
{
	GENERATED_BODY()

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FName VariableName_DEPRECATED;
#endif

	UPROPERTY(DisplayName = "Variable", EditAnywhere, Category="Variable")
	FAnimNextVariableReference Variable;

	virtual bool GetValue(FChooserEvaluationContext& Context, uint8& OutResult) const override;
	virtual bool SetValue(FChooserEvaluationContext& Context, uint8 InValue) const override;

	virtual void GetDisplayName(FText& OutName) const override;

#if WITH_EDITORONLY_DATA
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
#endif

	#if WITH_EDITOR
	virtual const UEnum* GetEnum() const override
	{
		if(const UUAFSharedVariables* SharedVariables = Cast<UUAFSharedVariables>(Variable.GetObject()))
		{
			if (const UPropertyBag* PropertyBag =  SharedVariables->GetVariableDefaults().GetPropertyBagStruct())
			{
				if (const FProperty* Property = PropertyBag->FindPropertyByName(Variable.GetName()))
				{
					if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
					{
						return EnumProperty->GetEnum();
					}
					if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
					{
						return ByteProperty->Enum;
					}
				}
			}
		}
		return nullptr;
	}
	#endif
};

template<>
struct TStructOpsTypeTraits<FEnumAnimProperty> : public TStructOpsTypeTraitsBase2<FEnumAnimProperty>
{
#if WITH_EDITORONLY_DATA
	enum
	{
		WithSerializer = true,
		WithPostSerialize = true,
	};
#endif
};

USTRUCT(DisplayName = "Name Anim Param")
struct FNameAnimProperty :  public FChooserParameterNameBase
{
	GENERATED_BODY()
public:

	UPROPERTY(DisplayName = "Variable", EditAnywhere, Category="Variable", meta = (AllowedType = "FName"))
	FAnimNextVariableReference Variable;

	virtual bool GetValue(FChooserEvaluationContext& Context, FName& OutResult) const override;
	virtual bool SetValue(FChooserEvaluationContext& Context, const FName& InValue) const override;
	virtual void GetDisplayName(FText& OutName) const override;
};