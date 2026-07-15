// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IChooserColumn.h"
#include "IChooserParameterGameplayTag.h"
#include "ChooserPropertyAccess.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Serialization/MemoryReader.h"
#include "OutputGameplayTagColumn.generated.h"

#define UE_API CHOOSER_API

USTRUCT(DisplayName = "Output Gameplay Tag", Meta = (Category = "Output", Tooltip = "A column which writes a Gameplay Tag Container value."))
struct FOutputGameplayTagColumn : public FChooserColumnBase
{
	GENERATED_BODY()
	public:
	UE_API FOutputGameplayTagColumn();

	virtual bool HasFilters() const override { return false; }
	virtual bool HasOutputs() const override { return true; }
	UE_API virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex) const override;

	UPROPERTY(EditAnywhere,  NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.FChooserParameterGameplayTagBase", ToolTip="The Gameplay Tag Container property this column will write to"), Category = "Hidden")
	FInstancedStruct InputValue;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=Data, meta=(ToolTip="DefaultRowValue will be assigned to cells when new rows are created"));
	FGameplayTagContainer DefaultRowValue;
#endif

	UPROPERTY()
	TArray<FGameplayTagContainer> RowValues;

#if WITH_EDITOR
	mutable FGameplayTagContainer TestValue;
	UE_API virtual void AddToDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex) override;
	UE_API virtual void SetFromDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex) override;
	virtual void CopyFallback(FChooserColumnBase& SourceColumn) override { FallbackValue = static_cast<FOutputGameplayTagColumn&>(SourceColumn).FallbackValue; }
#endif

	// FallbackValue will be used as the output value if the all rows in the chooser fail, and the FallbackResult from the chooser is used.
	UPROPERTY()
   	FGameplayTagContainer FallbackValue;

	FGameplayTagContainer& GetValueForIndex(int32 Index)
	{
		return Index == ChooserColumn_SpecialIndex_Fallback ? FallbackValue : RowValues[Index];
	}

	const FGameplayTagContainer& GetValueForIndex(int32 Index) const
	{
		return Index == ChooserColumn_SpecialIndex_Fallback ? FallbackValue : RowValues[Index];
	}

	CHOOSER_COLUMN_BOILERPLATE(FChooserParameterGameplayTagBase);
};

#undef UE_API
