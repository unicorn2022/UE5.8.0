// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Widgets/Input/SComboBox.h"

/*
* This class is used as a custom ComboBox for MetaHuman Character purposes
* Requirements for using it are having an Enum for options which the ComboBox will represent
*/
template<typename TEnum>
class SMetaHumanCharacterEditorComboBox : public SCompoundWidget
{
public:
	using FEnumType = typename std::underlying_type<TEnum>::type;

	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, uint8)
	
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorComboBox<TEnum>) {}

		/** The currently selected item of the Combo Box.
		 * Note: This will also be used as InitiallySelectedItem during Construct
		 */
		SLATE_ATTRIBUTE(TEnum, CurrentValue)

		/** Called when the selection of the Combo Box has changed. */
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)

	SLATE_END_ARGS()

	/** Constructs the widget. */
	void Construct(const typename SMetaHumanCharacterEditorComboBox<TEnum>::FArguments& InArgs)
	{
		OnSelectionChanged = InArgs._OnSelectionChanged;
		CurrentValue = InArgs._CurrentValue;
		ComboBoxOptions = GetEnumOptions();

		check(CurrentValue.IsSet());
		check(OnSelectionChanged.IsBound());
		check(!ComboBoxOptions.IsEmpty());

		TSharedPtr<TEnum> InitiallySelectedItem = nullptr;
		for (const TSharedPtr<TEnum>& ItemPtr : ComboBoxOptions)
		{
			if (!ItemPtr.IsValid())
			{
				continue;
			}

			const TEnum Item = *ItemPtr.Get();
			if (Item == CurrentValue.Get())
			{
				InitiallySelectedItem = ItemPtr;
				break;
			}
		}

		ChildSlot
			[
				SAssignNew(ComboBox, SComboBox<TSharedPtr<TEnum>>)
				.OptionsSource(&ComboBoxOptions)
				.InitiallySelectedItem(InitiallySelectedItem)
				.OnGenerateWidget(this, &SMetaHumanCharacterEditorComboBox::OnGenerateWidget)
				.OnSelectionChanged(this, &SMetaHumanCharacterEditorComboBox::OnComboBoxSelectionChanged)
				.IsEnabled(InArgs._IsEnabled)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SMetaHumanCharacterEditorComboBox::GetSelectedEnumNameAsText)
					.ToolTipText(this, &SMetaHumanCharacterEditorComboBox::GetSelectedEnumToolTipAsText)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				]
			];
	}

	/** Sets the selected combo box item and executes selection change callback if bound and bExecuteOnSelectionChanged is true */
	void SetSelectedItem(TEnum InItem, const bool bExecuteOnSelectionChanged = true) const
	{
		TSharedPtr<TEnum> ItemToSelect = nullptr;
		for (const TSharedPtr<TEnum>& ItemPtr : ComboBoxOptions)
		{
			if (!ItemPtr.IsValid())
			{
				continue;
			}

			const TEnum Item = *ItemPtr.Get();
			if (Item == InItem)
			{
				ItemToSelect = ItemPtr;
				break;
			}
		}

		if (ComboBox.IsValid() && ItemToSelect.IsValid())
		{
			if (ComboBox->GetSelectedItem() != ItemToSelect)
			{
				ComboBox->SetSelectedItem(ItemToSelect);
				if (bExecuteOnSelectionChanged)
				{
					OnSelectionChanged.ExecuteIfBound(static_cast<FEnumType>(InItem));
				}
			}
		}
	}

private:
	/** Gets the Enum name by the given index. */
	FText GetEnumNameText(int64 EnumIndex) const
	{
		const UEnum* EnumPtr = StaticEnum<TEnum>();
		return EnumPtr ? EnumPtr->GetDisplayNameTextByValue(EnumIndex) : FText::GetEmpty();
	}

	/** Gets the Enum name by the given index. */
	FText GetEnumToolTipText(int64 EnumIndex) const
	{
		const UEnum* EnumPtr = StaticEnum<TEnum>();
		return EnumPtr ? EnumPtr->GetToolTipTextByIndex(EnumIndex) : FText::GetEmpty();
	}

	/** Gets the name of the currently selected Enum as text. */
	FText GetSelectedEnumNameAsText() const
	{
		const TSharedPtr<TEnum> SelectedItem = ComboBox.IsValid() ? ComboBox->GetSelectedItem() : nullptr;
		if (SelectedItem.IsValid())
		{
			const int64 ItemValue = static_cast<int64>(static_cast<FEnumType>(*SelectedItem.Get()));
			return GetEnumNameText(ItemValue);
		}

		return FText::GetEmpty();
	}

	/** Gets the name of the currently selected Enum as text. */
	FText GetSelectedEnumToolTipAsText() const
	{
		const TSharedPtr<TEnum> SelectedItem = ComboBox.IsValid() ? ComboBox->GetSelectedItem() : nullptr;
		if (SelectedItem.IsValid())
		{
			const int64 ItemValue = static_cast<int64>(static_cast<FEnumType>(*SelectedItem.Get()));
			return GetEnumToolTipText(ItemValue);
		}

		return FText::GetEmpty();
	}

	/** Gets the array of all Enum options from the given UEnum. */
	TArray<TSharedPtr<TEnum>> GetEnumOptions()
	{
		TArray<TSharedPtr<TEnum>> Options;

		UEnum* EnumPtr = StaticEnum<TEnum>();
		if (!EnumPtr)
		{
			return Options;
		}

		const int32 NumEnums = EnumPtr->NumEnums();
		for (int32 Index = 0; Index < NumEnums; ++Index)
		{
			const FString EnumName = EnumPtr->GetNameStringByIndex(Index);
			if (EnumName.EndsWith(TEXT("MAX")))
			{
				continue;
			}

			if (EnumPtr->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}

			TEnum EnumValue = static_cast<TEnum>(EnumPtr->GetValueByIndex(Index));
			Options.Add(MakeShared<TEnum>(EnumValue));
		}

		return Options;
	}

	/** Generated the Combo Box widget for the given item. */
	TSharedRef<SWidget> OnGenerateWidget(TSharedPtr<TEnum>InItem)
	{
		check(InItem.IsValid());

		const int64 ItemValue = static_cast<int64>(static_cast<FEnumType>(*InItem.Get()));
		const FText EnumNameText = GetEnumNameText(ItemValue);
		const FText EnumToolTipText = GetEnumToolTipText(ItemValue);
			
		return 
			SNew(STextBlock)
			.Text(EnumNameText)
			.ToolTipText(EnumToolTipText)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")));
	}

	/** Called when the combo box selection has changed. */
	void OnComboBoxSelectionChanged(TSharedPtr<TEnum> InItem, ESelectInfo::Type InSelectInfo)
	{
		if (InItem.IsValid() && InSelectInfo != ESelectInfo::Direct)
		{
			const uint8 Item = static_cast<FEnumType>(*InItem);
			OnSelectionChanged.ExecuteIfBound(Item);
		}
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		if (!ComboBox->GetSelectedItem().IsValid() || CurrentValue.Get() != *ComboBox->GetSelectedItem().Get())
		{
			// Since we are only updating the combo box selected item view as a result of property update aka CurrentValue, 
			// we don't need to execute OnSelectionChanged
			SetSelectedItem(CurrentValue.Get(), /* bExecuteOnSelectionChanged */ false);
		}
	}

	/** The delegate to execute when the selection of the Combo Box has changed. */
	FOnSelectionChanged OnSelectionChanged;
	
	TAttribute<TEnum> CurrentValue;

	/** The array of Combo Box options. */
	TArray<TSharedPtr<TEnum>> ComboBoxOptions;

	/** Reference to the Combo Box widget. */
	TSharedPtr<SComboBox<TSharedPtr<TEnum>>> ComboBox;
};
