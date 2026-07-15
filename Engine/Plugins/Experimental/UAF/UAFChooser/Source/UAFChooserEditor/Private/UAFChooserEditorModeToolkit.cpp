// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFChooserEditorModeToolkit.h"

#include "ContextObjectStore.h"
#include "InteractiveToolManager.h"

#include "EditorModeManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "ToolMenus.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "ChooserTableEditorCommands.h"

#define LOCTEXT_NAMESPACE "UAFChooserEditorModeToolkit"

FUAFChooserEditorModeToolkit::FUAFChooserEditorModeToolkit(UUAFChooserEditorMode* InEditorMode)
	: WeakEditorMode(InEditorMode)
{
}

void FUAFChooserEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
}

FName FUAFChooserEditorModeToolkit::GetToolkitFName() const
{
	return FName("UAFChooserMode");
}

FText FUAFChooserEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "UAF Chooser Mode");
}


void FUAFChooserEditorModeToolkit::ShutdownUI()
{
	FModeToolkit::ShutdownUI();
} 


#undef LOCTEXT_NAMESPACE // "UAFChooserEditorModeToolkit"
