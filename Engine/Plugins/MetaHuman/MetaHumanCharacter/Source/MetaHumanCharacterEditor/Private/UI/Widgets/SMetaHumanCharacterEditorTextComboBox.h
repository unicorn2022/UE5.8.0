// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Widgets/Input/SComboBox.h"

/*
* This class is used as a custom ComboBox containing texture labels for MetaHuman Character purposes
*/
class SMetaHumanCharacterEditorTextComboBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, int32)
	DECLARE_DELEGATE_OneParam(FOnSelectionChangedItem, const FString&)

	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorTextComboBox) {}

		/** Placeholder text when no option is selected. */
		SLATE_ATTRIBUTE(FText, PlaceholderText)

		/** Called when the selection of the Combo Box has changed. */
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)

		/** Called when the selection of the Combo Box has changed. */
		SLATE_EVENT(FOnSelectionChangedItem, OnSelectionChangedItem)

	SLATE_END_ARGS()

	/** Constructs the widget. */
	void Construct(const typename SMetaHumanCharacterEditorTextComboBox::FArguments& InArgs, const TArray<TSharedPtr<FString>>& InComboBoxOptions, const TSharedPtr<FString>& InInitiallySelectedItem);

	void SetSelectedItem(const int32 InIdx);

	void SetOptions(const TArray<TSharedPtr<FString>>& InComboBoxOptions);

	void ClearSelection() const;

private:
	

	/** Generated the Combo Box widget for the given item. */
	TSharedRef<SWidget> OnGenerateWidget(TSharedPtr<FString> InItem);

	/** Called when the combo box selection has changed. */
	void OnComboBoxSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectInfo);

	/** Gets the name of the currently selected item as text. */
	FText GetSelectedItemAsText() const;

	/** The delegate to execute when the selection of the Combo Box has changed. */
	FOnSelectionChanged OnSelectionChanged;

	/** The delegate to execute when the selection of the Combo Box has changed. */
	FOnSelectionChangedItem OnSelectionChangedItem;

	/** The array of Combo Box options. */
	TArray<TSharedPtr<FString>> ComboBoxOptions;

	/** Reference to the Combo Box widget. */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ComboBox;

	/** Placeholder text when no option is selected. */
	TAttribute<FText> PlaceholderText;
};
