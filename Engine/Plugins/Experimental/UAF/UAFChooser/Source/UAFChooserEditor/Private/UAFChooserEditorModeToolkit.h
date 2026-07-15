// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UAFChooserEditorMode.h"
#include "Toolkits/BaseToolkit.h"

class FUAFChooserEditorModeToolkit : public FModeToolkit
{
public:

	FUAFChooserEditorModeToolkit(UUAFChooserEditorMode* InEditorMode);
	
	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;

	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
	virtual void ShutdownUI() override;

protected:
	TWeakObjectPtr<UUAFChooserEditorMode> WeakEditorMode;
};
