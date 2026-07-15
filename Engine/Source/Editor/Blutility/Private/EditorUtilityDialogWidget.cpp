// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityDialogWidget.h"
#include "SEditorUtilityDialog.h"

void UEditorUtilityDialogWidget::CloseDialog(EAppReturnType::Type Result)
{
	if (TSharedPtr<SEditorUtilityDialog> Dialog = OwningDialog.Pin())
	{
		Dialog->CloseWithResult(Result);
	}
}

void UEditorUtilityDialogWidget::SetOwningDialog(TSharedPtr<SEditorUtilityDialog> InDialog)
{
	OwningDialog = InDialog;
}
