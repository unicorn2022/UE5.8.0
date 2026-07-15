// Copyright Epic Games, Inc. All Rights Reserved.

#include "UAFChooserEditorMode.h"

#include "EditorModeManager.h"
#include "Modules/ModuleManager.h"
#include "Toolkits/ToolkitManager.h"
#include "UAFChooserEditorModeToolkit.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UAFChooserEditorMode)

#define LOCTEXT_NAMESPACE "UAFChooserEditorMode"

const FEditorModeID UUAFChooserEditorMode::EM_UAFChooser("UAFChooserEditorMode");

UUAFChooserEditorMode::UUAFChooserEditorMode()
{
	Info = FEditorModeInfo(UUAFChooserEditorMode::EM_UAFChooser,
		LOCTEXT("UAFChooserEditorModeName", "UAFChooserEditorMode"),
		FSlateIcon(),
		false);
}

void UUAFChooserEditorMode::Enter()
{
	Super::Enter();
}


void UUAFChooserEditorMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}
	
	Super::Exit();
}

void UUAFChooserEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FUAFChooserEditorModeToolkit(this));
}

void UUAFChooserEditorMode::BindCommands()
{
	// UEdMode::BindCommands();
	// const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	// BindToolkitCommands(CommandList);
}


#undef LOCTEXT_NAMESPACE // "UUAFChooserEditorMode"
