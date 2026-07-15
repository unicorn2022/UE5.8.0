// Copyright Epic Games, Inc. All Rights Reserved.

#include "SShaderStatSpreadsheet.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "ShaderStatsModel.h"

class SNumberTableRow : public SMultiColumnTableRow<TSharedPtr<FName>>
{
public:
	SLATE_BEGIN_ARGS(SNumberTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FName>, Name)
		SLATE_ARGUMENT(TSharedPtr<IShaderTableModel>, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		Name = InArgs._Name;
		Model = InArgs._Model;

		SMultiColumnTableRow<TSharedPtr<FName>>::Construct(
			SMultiColumnTableRow<TSharedPtr<FName>>::FArguments()
			.Padding(FMargin(2.0f)),
			InOwnerTableView
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		return Model->GetCellWidget(*Name, ColumnName);
	}

private:
	TSharedPtr<FName> Name;
	TSharedPtr<IShaderTableModel> Model;
};

void SShaderStatSpreadsheet::Construct(const FArguments& InArgs)
{
	Model = InArgs._Model;

	for (const FName& RowName : Model->GetRows())
	{
		Items.Add(MakeShared<FName>(RowName));
	}

	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow);

	for (const FName& ColumnName : Model->GetColumns())
	{
		Header->AddColumn(SHeaderRow::Column(ColumnName).DefaultLabel(FText::FromName(ColumnName)));
	}

	ChildSlot
	[
		SNew(SListView<TSharedPtr<FName>>)
		.ListItemsSource(&Items)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SShaderStatSpreadsheet::OnGenerateRow)
		.HeaderRow(Header)
	];
}

TSharedRef<ITableRow> SShaderStatSpreadsheet::OnGenerateRow(TSharedPtr<FName> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SNumberTableRow, OwnerTable).Name(InItem).Model(Model);
}