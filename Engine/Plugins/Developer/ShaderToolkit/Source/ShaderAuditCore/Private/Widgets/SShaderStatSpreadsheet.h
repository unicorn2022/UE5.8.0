// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IShaderTableModel;
class ITableRow;
class STableViewBase;

class SShaderStatSpreadsheet : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShaderStatSpreadsheet) {}
		SLATE_ARGUMENT(TSharedPtr<IShaderTableModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TArray<TSharedPtr<FName>> Items;
	TSharedPtr<IShaderTableModel> Model;
	TArray<FName> ColumnIds;

	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable);
};
