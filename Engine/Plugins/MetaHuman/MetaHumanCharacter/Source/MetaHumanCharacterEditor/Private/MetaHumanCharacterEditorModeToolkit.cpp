// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorModeToolkit.h"

#include "Algo/AnyOf.h"
#include "EditorModeManager.h"
#include "EdMode.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Features/IModularFeatures.h"
#include "IDetailsView.h"
#include "MetaHumanCharacterAnalytics.h"
#include "MetaHumanCharacterEditorMode.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "MetaHumanCharacterEditorToolkitBuilder.h"
#include "MetaHumanCharacterEditorViewportClient.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SMetaHumanOverlayWidget.h"
#include "SPrimaryButton.h"
#include "STrackerImageViewer.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorCostumeTools.h"
#include "Tools/MetaHumanCharacterEditorEyesTool.h"
#include "Tools/MetaHumanCharacterEditorFaceEditingTools.h"
#include "Tools/MetaHumanCharacterEditorHeadModelTool.h"
#include "Tools/MetaHumanCharacterEditorImportTools.h"
#include "Tools/MetaHumanCharacterEditorMakeupTool.h"
#include "Tools/MetaHumanCharacterEditorMeshImportTool.h"
#include "Tools/MetaHumanCharacterEditorPresetsTool.h"
#include "Tools/MetaHumanCharacterEditorSkinTool.h"
#include "Tools/MetaHumanCharacterEditorSubTools.h"
#include "Tools/MetaHumanCharacterEditorTextureMaterialOverrideTool.h"
#include "Tools/MetaHumanCharacterEditorPipelineTools.h"
#include "Tools/MetaHumanCharacterEditorExportToolBase.h"
#include "Tools/MetaHumanCharacterEditorWardrobeTools.h"
#include "Tools/MetaHumanImportToolFeature.h"
#include "UI/Viewport/SMetaHumanCharacterEditorViewport.h"
#include "UI/Views/SMetaHumanCharacterEditorBlendToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorBodyModelToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorImportToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorExternalImportToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorCostumeToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorEyesToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorFaceToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorHeadMaterialsToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorHeadModelToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorMakeupToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorMeshImportToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorPipelineToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorExportToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorPresetsToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorSkinToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorTextureMaterialOverrideToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorToolView.h"
#include "UI/Views/SMetaHumanCharacterEditorWardrobeToolView.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "SWarningOrErrorBox.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

FMetaHumanCharacterEditorModeToolkit::FMetaHumanCharacterEditorModeToolkit()
{
	// Creates the widget to display warning messages for tools
	// This could potentially be inlined in GetInlineContent but since that function is const
	// create the widget here in constructor instead so its always ready to be used
	SAssignNew(ToolWarningArea, STextBlock)
		.AutoWrapText(true)
		.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
		.Text(FText::GetEmpty())
		.Visibility(EVisibility::Collapsed);

}

void FMetaHumanCharacterEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	bUsesToolkitBuilder = true;

	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	if (HasToolkitBuilder())
	{
		ToolkitBuilder->VerticalToolbarElement->GenerateWidget();
	}

	RegisterPalettes();

	ClearNotification();
	ClearWarning();

	ToolkitSections->ToolWarningArea = ToolWarningArea;

	ToolkitBuilder->OnActivePaletteChanged.AddSP(this, &FMetaHumanCharacterEditorModeToolkit::OnActivePaletteChanged);

	// The ToolkitWidget is returned in GetInlineContent and represents the main
	// widget of the Mode Tools. Using FToolkitBuilder to offload the actual widget
	// creation and it already has all the basic interactions implemented
	SAssignNew(ToolkitWidget, SBorder)
	.HAlign(HAlign_Fill)
	.Padding(0)
	.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
	[
		ToolkitBuilder->GenerateWidget()->AsShared()
	];

	// Register callbacks to display tool messages in the status bar and warnings
	UEditorInteractiveToolsContext* ToolsContext = GetScriptableEditorMode()->GetInteractiveToolsContext();

	// Set the default tracking mode of tools. By default, activating a tool creates a transaction called "Activate Tool"
	// but we want more control over which transactions are created for the MH editing tools, so no transaction will
	// will be created by default when activating a tool
	ToolsContext->ToolManager->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);
	
	ToolsContext->OnToolNotificationMessage.AddSP(this, &FMetaHumanCharacterEditorModeToolkit::PostNotification);
	ToolsContext->OnToolWarningMessage.AddSP(this, &FMetaHumanCharacterEditorModeToolkit::PostWarning);
}

FName FMetaHumanCharacterEditorModeToolkit::GetToolkitFName() const
{
	return TEXT("MetaHumanCharacterEditorMode");
}

FText FMetaHumanCharacterEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("TookitModeEditorName", "MetaHuman Character Editor Mode");
}

FText FMetaHumanCharacterEditorModeToolkit::GetActiveToolDisplayName() const
{
	return ActiveToolName;
}

TSharedPtr<SWidget> FMetaHumanCharacterEditorModeToolkit::GetInlineContent() const
{
	return ToolkitWidget.ToSharedRef();
}

void FMetaHumanCharacterEditorModeToolkit::OnToolStarted(UInteractiveToolManager* InManager, UInteractiveTool* InTool)
{
	FModeToolkit::OnToolStarted(InManager, InTool);

	ActiveToolName = InTool->GetToolInfo().ToolDisplayName;
	
	// Update last selected tool
	HandleLastToolActivation(InTool);

	// Builds the name of the active tool icon based on the active tool name.
	// Its important to have the tool identifiers used registering tools to match
	// the command names so we can build the correct icon name here
	FString ActiveToolIdentifier = GetScriptableEditorMode()->GetToolManager()->GetActiveToolName(EToolSide::Mouse);
	UE::MetaHuman::Analytics::RecordOnToolStartEvent(Cast<UMetaHumanCharacterEditorMode>(OwningEditorMode), ToolkitBuilder.Get(), ActiveToolIdentifier);
	
	ActiveToolIdentifier.InsertAt(0, ".");
	FName ActiveToolIconName = ISlateStyle::Join(FMetaHumanCharacterEditorToolCommands::Get().GetContextName(), TCHAR_TO_ANSI(*ActiveToolIdentifier));
	ActiveToolIcon = FMetaHumanCharacterEditorStyle::Get().GetOptionalBrush(ActiveToolIconName);

	// make the standard tool warning area not visible (as we are using a custom warning area)
	ToolWarningArea->SetVisibility(EVisibility::Collapsed);

	// Sorting order matters. First we need to activate optional subtools, then to create the tool widget.
	UpdateSubToolsToolbar();
	UpdateActiveToolViewWidget();
	UpdateViewport(InTool);
}

void FMetaHumanCharacterEditorModeToolkit::OnToolEnded(UInteractiveToolManager* InManager, UInteractiveTool* InTool)
{
	FModeToolkit::OnToolEnded(InManager, InTool);

	HandleToolShutdown();
	ActiveToolName = FText::GetEmpty();

	UpdateSubToolsToolbar();
	UpdateActiveToolViewWidget();
	UpdateViewport(InTool, /* bOnToolEnded */ true);

	InTool->OnPropertySetsModified.RemoveAll(this);
}

void FMetaHumanCharacterEditorModeToolkit::PostNotification(const FText& InMessage)
{
	ClearNotification();

	if (TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin())
	{
		const FName StatusBarName = ModeUILayerPtr->GetStatusBarName();
		UStatusBarSubsystem* StatusBarSubsystem = GEditor->GetEditorSubsystem<UStatusBarSubsystem>();
		ActiveToolMessageHandle = StatusBarSubsystem->PushStatusBarMessage(StatusBarName, InMessage);
	}
}

void FMetaHumanCharacterEditorModeToolkit::ClearNotification()
{
	if (TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin())
	{
		const FName StatusBarName = ModeUILayerPtr->GetStatusBarName();
		UStatusBarSubsystem* StatusBarSubsystem = GEditor->GetEditorSubsystem<UStatusBarSubsystem>();
		StatusBarSubsystem->PopStatusBarMessage(StatusBarName, ActiveToolMessageHandle);
	}

	ActiveToolMessageHandle.Reset();
}

void FMetaHumanCharacterEditorModeToolkit::PostWarning(const FText& InMessage)
{
	if (InMessage.IsEmpty())
	{
		ClearWarning();
	}
	else
	{
		CustomWarning = InMessage;
	}
}

void FMetaHumanCharacterEditorModeToolkit::ClearWarning()
{
	CustomWarning = FText();
}

void FMetaHumanCharacterEditorModeToolkit::UpdateWarningText()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	UMetaHumanCharacterEditorMode* MHCEdMode = CastChecked<UMetaHumanCharacterEditorMode>(OwningEditorMode);
	TObjectPtr<UMetaHumanCharacter> Character = MHCEdMode->GetCharacter();
	if (MetaHumanCharacterSubsystem && IsValid(Character))
	{
		EMetaHumanCharacterRigState RiggingState = MetaHumanCharacterSubsystem->GetRiggingState(Character);
		bool bIsDownloadingTextures = MetaHumanCharacterSubsystem->IsRequestingHighResolutionTextures(Character);
		bool bIsAsyncConformPending = MetaHumanCharacterSubsystem->IsAsyncConformPending(Character);

		static const TArray<FName, TInlineAllocator<3>> PalettesToExclude = { "LoadHairAndClothingTools", "LoadPipelineTools", "LoadExportTools" };
		const bool bIsPaletteToExclude = PalettesToExclude.Contains( ToolkitBuilder->GetActivePaletteName());

		if (RiggingState == EMetaHumanCharacterRigState::Rigged && !bIsPaletteToExclude)
		{
			PostWarning(LOCTEXT("RiggingStateChangedRigged", "The Asset you're editing is rigged. Preset selection and Modelling operations require that the rig is deleted to unlock editing."));
		}
		else if (RiggingState == EMetaHumanCharacterRigState::RigPending && !bIsAsyncConformPending && !bIsPaletteToExclude)
		{
			PostWarning(LOCTEXT("RiggingStateChangedRigPending", "The Asset you're editing is rig-pending. Preset selection and Modelling operations require that the rig is deleted to unlock editing."));
		}
		else if (bIsDownloadingTextures)
		{
			PostWarning(LOCTEXT("DownloadingTexturesWarning", "The Asset you're editing is downloading high resolution textures. \nPreset selection is unavailable while textures are downloading."));
		}
		else
		{
			ClearWarning();
		}
	}
}

TSharedPtr<SEditorViewport> FMetaHumanCharacterEditorModeToolkit::GetEditorViewport() const
{
	TSharedPtr<SEditorViewport> ViewportWidget = nullptr;
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode()->GetToolManager();
	FViewport* Viewport = ToolManager && ToolManager->GetContextQueriesAPI() ? ToolManager->GetContextQueriesAPI()->GetFocusedViewport() : nullptr;
	FMetaHumanCharacterViewportClient* MHCViewportClient = Viewport ? static_cast<FMetaHumanCharacterViewportClient*>(Viewport->GetClient()) : nullptr;
	if (MHCViewportClient)
	{
		ViewportWidget = MHCViewportClient->GetEditorViewportWidget();
	}

	return ViewportWidget;
}

EVisibility FMetaHumanCharacterEditorModeToolkit::GetCustomWarningVisibility() const
{
	if (CustomWarning.IsEmpty())
	{
		return EVisibility::Collapsed;
	}
	else
	{
		return EVisibility::Visible;
	}
}

bool FMetaHumanCharacterEditorModeToolkit::IsViewportModeVisible() const
{
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode()->GetToolManager();
	const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = ToolManager ? Cast<UMetaHumanCharacterEditorMeshImportTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
	return MeshImportTool != nullptr;
}

FText FMetaHumanCharacterEditorModeToolkit::GetCustomWarning() const
{
	return CustomWarning;
}

TSharedRef<SWidget> FMetaHumanCharacterEditorModeToolkit::CreateSubToolsToolbar(TNotNull<UInteractiveTool*> Tool, bool bUseLabel) const
{
	const UMetaHumanCharacterEditorToolWithSubTools* ToolWithSubTools = Cast<UMetaHumanCharacterEditorToolWithSubTools>(Tool);
	const UMetaHumanCharacterEditorSubToolsProperties* SubTools = IsValid(ToolWithSubTools) ? ToolWithSubTools->GetSubTools() : nullptr;
	if (!SubTools)
	{
		return SNullWidget::NullWidget;
	}

	const TSharedRef<FSlimHorizontalUniformToolBarBuilder> ToolbarBuilder = MakeShared<FSlimHorizontalUniformToolBarBuilder>(SubTools->GetCommandList(), FMultiBoxCustomization::None);
	ToolbarBuilder->SetStyle(&FAppStyle::Get(), TEXT("SlimPaletteToolBar"));

	const TSharedPtr<FUICommandList> CommandList = SubTools->GetCommandList();
	const TArray<TSharedPtr<FUICommandInfo>> SubToolCommands = SubTools->GetSubToolCommands();
	for (TSharedPtr<FUICommandInfo> SubToolCommand : SubToolCommands)
	{
		FButtonArgs Args;
		Args.Command = SubToolCommand;
		Args.CommandList = CommandList;
		Args.UserInterfaceActionType = SubToolCommand->GetUserInterfaceType();
		ToolbarBuilder->AddToolBarButton(Args);
	}

	// Automatically trigger the default subtool or the last active subtool action
	FString ActiveToolIdentifier = GetScriptableEditorMode()->GetToolManager()->GetActiveToolName(EToolSide::Mouse);
	if (SubToolCommands.Num() > 0)
	{
		TSharedPtr<FUICommandInfo> Command = SubTools->GetDefaultCommand();
		if (ToolNameToLastActiveSubToolNameMap.Contains(*ActiveToolIdentifier))
		{
			const FName& LastActiveSubToolName = ToolNameToLastActiveSubToolNameMap.FindChecked(*ActiveToolIdentifier);
			const int32 Index = SubToolCommands.IndexOfByPredicate(
				[LastActiveSubToolName](const TSharedPtr<FUICommandInfo>& Command)
				{
					return Command->GetCommandName() == LastActiveSubToolName;
				});

			Command = SubToolCommands.IsValidIndex(Index) ? SubToolCommands[Index] : nullptr;
		}
		else if (!Command)
		{
			Command = SubToolCommands[0];
		}
			
		if (Command.IsValid() && CommandList.IsValid())
		{
			CommandList->TryExecuteAction(Command.ToSharedRef());
		}
	}

	const TSharedRef<SWidget> SubToolsToolbar =
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
			.Visibility(bUseLabel ? EVisibility::Visible : EVisibility::Collapsed)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(16.f, 4.f)
			[
				SNew(STextBlock)
				.Text(ActiveToolName)
				.Font(FCoreStyle::Get().GetFontStyle("BoldFont"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ToolbarBuilder->MakeWidget()
		];

	return SubToolsToolbar;
}

TSharedRef<SWidget> FMetaHumanCharacterEditorModeToolkit::CreateToolView(UInteractiveTool* Tool)
{
	if (UMetaHumanCharacterEditorPresetsTool* PresetsTool = Cast<UMetaHumanCharacterEditorPresetsTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorPresetsToolView, PresetsTool);
	}
	else if (UMetaHumanCharacterEditorImportFromDNATool* ImportFromDNATool = Cast<UMetaHumanCharacterEditorImportFromDNATool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorImportFromDNAToolView, ImportFromDNATool);
	}
	else if (UMetaHumanCharacterEditorImportFromIdentityTool* ImportFromIdentityTool = Cast<UMetaHumanCharacterEditorImportFromIdentityTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorImportFromIdentityToolView, ImportFromIdentityTool);
	}
	else if (UMetaHumanCharacterEditorImportFromTemplateTool* ImportFromTemplateTool = Cast<UMetaHumanCharacterEditorImportFromTemplateTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorImportFromTemplateToolView, ImportFromTemplateTool);
	}
	else if (UMetaHumanCharacterEditorMeshImportTool* ImportFromCustomMeshTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorMeshImportToolView, ImportFromCustomMeshTool);
	}
	else if (UMetaHumanCharacterEditorFaceSculptTool* FaceSculptTool = Cast<UMetaHumanCharacterEditorFaceSculptTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorFaceSculptToolView, FaceSculptTool);
	}
	else if (UMetaHumanCharacterEditorFaceMoveTool* FaceMoveTool = Cast<UMetaHumanCharacterEditorFaceMoveTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorFaceMoveToolView, FaceMoveTool);
	}
	else if (UMetaHumanCharacterEditorHeadAndBodyBlendTool* BlendTool = Cast<UMetaHumanCharacterEditorHeadAndBodyBlendTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorBlendToolView, BlendTool);
	}
	else if (UMetaHumanCharacterEditorBodyModelTool* BodyModelTool = Cast<UMetaHumanCharacterEditorBodyModelTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorBodyModelToolView, BodyModelTool);
	}
	else if (UMetaHumanCharacterEditorEyesTool* EyesTool = Cast<UMetaHumanCharacterEditorEyesTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorEyesToolView, EyesTool);
	}
	else if (UMetaHumanCharacterEditorHeadMaterialsTool* HeadMaterialsTool = Cast<UMetaHumanCharacterEditorHeadMaterialsTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorHeadMaterialsToolView, HeadMaterialsTool);
	}
	else if (UMetaHumanCharacterEditorHeadModelTool* HeadModelTool = Cast<UMetaHumanCharacterEditorHeadModelTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorHeadModelToolView, HeadModelTool);
	}
	else if (UMetaHumanCharacterEditorMakeupTool* MakeupTool = Cast<UMetaHumanCharacterEditorMakeupTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorMakeupToolView, MakeupTool);
	}
	else if (UMetaHumanCharacterEditorSkinTool* SkinTool = Cast<UMetaHumanCharacterEditorSkinTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorSkinToolView, SkinTool);
	}
	else if (UMetaHumanCharacterEditorTextureMaterialOverrideTool* TextureMaterialOverrideTool = Cast<UMetaHumanCharacterEditorTextureMaterialOverrideTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorTextureMaterialOverrideToolView, TextureMaterialOverrideTool);
	}
	else if (UMetaHumanCharacterEditorWardrobeTool* WardrobeTool = Cast<UMetaHumanCharacterEditorWardrobeTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorWardrobeToolView, WardrobeTool);
	}
	else if (UMetaHumanCharacterEditorCostumeTool* CostumeTool = Cast<UMetaHumanCharacterEditorCostumeTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorCostumeToolView, CostumeTool);
	}
	else if (UMetaHumanCharacterEditorPipelineTool* PipelineTool = Cast<UMetaHumanCharacterEditorPipelineTool>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorPipelineToolView, PipelineTool);
	}
	else if (UMetaHumanCharacterEditorExportToolBase* ExportTool = Cast<UMetaHumanCharacterEditorExportToolBase>(Tool))
	{
		return SNew(SMetaHumanCharacterEditorExportToolView, ExportTool);
	}
	else if (UMetaHumanCharacterExternalImportTool* ExtTool = Cast<UMetaHumanCharacterExternalImportTool>(Tool))
	{
		// Check externally-contributed import tools before falling back to the generic details view
		for (IMetaHumanImportToolFeature* Feature :
			IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanImportToolFeature>(
				IMetaHumanImportToolFeature::GetModularFeatureName()))
		{
			if (Feature->GetToolClass() && ExtTool->IsA(Feature->GetToolClass()))
			{
				TSharedRef<SWidget> Content = Feature->CreateContent(ExtTool);
				return SNew(SMetaHumanCharacterEditorExternalImportToolView, Content, ExtTool);
			}
		}
	}

	bHasToolView = false;
	return CreateToolDetailsView(Tool);
}

TSharedRef<IDetailsView> FMetaHumanCharacterEditorModeToolkit::CreateToolDetailsView(UInteractiveTool* Tool) const
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	const TSharedRef<IDetailsView> ToolDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	if (IsValid(Tool))
	{
		ToolDetailsView->SetObjects(Tool->GetToolProperties());
	}

	return ToolDetailsView;
}

TSharedRef<SWidget> FMetaHumanCharacterEditorModeToolkit::MakeCustomWarningsWidget()
{
	return
		SNew(SBox)
		.Padding(4.f)
		[
			SNew(SWarningOrErrorBox)
			.AutoWrapText(true)
			.MessageStyle(EMessageStyle::Warning)
			.Visibility(this, &FMetaHumanCharacterEditorModeToolkit::GetCustomWarningVisibility)
			.Message_Lambda([this] { return GetCustomWarning(); })
		];
}


TSharedRef<SWidget> FMetaHumanCharacterEditorModeToolkit::MakeActiveToolViewWidget()
{
	return
		SNew(SBorder)
		.BorderImage(FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.MainToolbar"))
		[
			SNew(SVerticalBox)

			// Subtools Toolbar section
			+ SVerticalBox::Slot()
			.Padding(-2.f, -2.f, -2.f, 0.f)
			.AutoHeight()
			[
				SAssignNew(SubToolsToolbarWidget, SVerticalBox)
			]
			// Tool View section
			+ SVerticalBox::Slot()
			[
				SAssignNew(ActiveToolViewWidget, SVerticalBox)
			]
		];
}

void FMetaHumanCharacterEditorModeToolkit::RegisterPalettes()
{
	const FMetaHumanCharacterEditorToolCommands& Commands = FMetaHumanCharacterEditorToolCommands::Get();

	const TSharedRef<FMetaHumanCharacterEditorToolkitSections> Sections = MakeShared<FMetaHumanCharacterEditorToolkitSections>();
	Sections->ToolViewArea = MakeActiveToolViewWidget();
	Sections->ToolCustomWarningsArea = MakeCustomWarningsWidget();
	ToolkitSections = Sections;

	FToolkitBuilderArgs ToolkitBuilderArgs{ GetScriptableEditorMode()->GetModeInfo().ToolbarCustomizationName };
	ToolkitBuilderArgs.ToolkitCommandList = GetToolkitCommands();
	ToolkitBuilderArgs.ToolkitSections = ToolkitSections;
	ToolkitBuilderArgs.SelectedCategoryTitleVisibility = EVisibility::Visible;

	const TSharedRef<FMetaHumanCharacterEditorToolkitBuilder> MetaHumanToolkitBuilder = MakeShared<FMetaHumanCharacterEditorToolkitBuilder>(ToolkitBuilderArgs);
	ToolkitBuilder = MetaHumanToolkitBuilder;

	const TArray<TSharedPtr<FUICommandInfo>> PresetsCommands =
	{
		Commands.BeginPresetsTool
	};
	MetaHumanToolkitBuilder->AddPaletteCustom(MakeShared<FToolPalette>(Commands.LoadPresetsTools, PresetsCommands));

	TArray<TSharedPtr<FUICommandInfo>> MeshImportCommands =
	{
		Commands.BeginImportFromCustomMeshTool,
		Commands.BeginImportFromTemplateTool,
		Commands.BeginImportFromIdentityTool,
		Commands.BeginImportFromDNATool
	};

	TArray<IMetaHumanImportToolFeature*> ImportToolFeatures = IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanImportToolFeature>(IMetaHumanImportToolFeature::GetModularFeatureName());
	for (IMetaHumanImportToolFeature* Feature : ImportToolFeatures)
	{
		if (TSharedPtr<FUICommandInfo> Command = Feature->GetCommand())
		{
			MeshImportCommands.Add(Command);
		}
	}
	TSharedRef<FToolPalette> ImportPalette = MakeShared<FToolPalette>(Commands.LoadMeshImportTools, MeshImportCommands);
	for (IMetaHumanImportToolFeature* Feature : ImportToolFeatures)
	{
		TSharedPtr<FUICommandInfo> Command = Feature->GetCommand();
		FSlateIcon Icon = Feature->GetIcon();
		if (Command && Icon.IsSet())
		{
			TSharedRef<FButtonArgs>* Button = ImportPalette->PaletteActions.FindByPredicate(
				[&Command](const TSharedRef<FButtonArgs>& B) { return B->Command == Command; });
			if (Button)
			{
				(*Button)->IconOverride = Icon;
			}
		}
	}
	MetaHumanToolkitBuilder->AddPaletteCustom(ImportPalette);
	
	const TArray<TSharedPtr<FUICommandInfo>> BodyCommands =
	{
		Commands.BeginHeadAndBodyBlendTool,
		Commands.BeginBodyModelTool,
		Commands.BeginFaceMoveTool,
		Commands.BeginFaceSculptTool,
		Commands.BeginHeadModelTools
	};
	MetaHumanToolkitBuilder->AddPaletteCustom(MakeShared<FToolPalette>(Commands.LoadHeadAndBodyTools, BodyCommands));

	const TArray<TSharedPtr<FUICommandInfo>> MaterialsCommands =
	{
		Commands.BeginSkinTool,
		Commands.BeginEyesTool,
		Commands.BeginMakeupTool,
		Commands.BeginTextureMaterialOverrideTool,
		Commands.BeginHeadMaterialsTools,
	};
	MetaHumanToolkitBuilder->AddPaletteCustom(MakeShared<FToolPalette>(Commands.LoadMaterialsTools, MaterialsCommands));

	const TArray<TSharedPtr<FUICommandInfo>> HairAndClothingCommands =
	{
		Commands.BeginWardrobeSelectionTool,
		Commands.BeginCostumeDetailsTool,
	};
	MetaHumanToolkitBuilder->AddPaletteCustom(MakeShared<FToolPalette>(Commands.LoadHairAndClothingTools, HairAndClothingCommands));

	const TArray<TSharedPtr<FUICommandInfo>> ExportCommands =
	{
		Commands.BeginDCCExportTool,
		Commands.BeginDNAExportTool,
		Commands.BeginGeometryExportTool,
		Commands.BeginMaterialsExportTool,
	};
	MetaHumanToolkitBuilder->AddPaletteCustom(MakeShared<FToolPalette>(Commands.LoadExportTools, ExportCommands));

	const TArray<TSharedPtr<FUICommandInfo>> PipelineCommands =
	{
		Commands.BeginPipelineTool
	};
	MetaHumanToolkitBuilder->AddPaletteCustom(MakeShared<FToolPalette>(Commands.LoadPipelineTools, PipelineCommands));

	ToolkitBuilder->SetActivePaletteOnLoad(Commands.LoadHeadAndBodyTools.Get());
	ToolkitBuilder->UpdateWidget();
}

void FMetaHumanCharacterEditorModeToolkit::InitializeViewport()
{
	const TSharedPtr<SMetaHumanCharacterEditorViewport> Viewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(GetEditorViewport());
	if(!Viewport.IsValid())
	{
			return;
	}

	if (!Viewport->OnViewportSizeChangedDelegate.IsBoundToObject(this))
	{
		Viewport->OnViewportSizeChangedDelegate.BindSP(this, &FMetaHumanCharacterEditorModeToolkit::OnViewportResized);
	}

	Viewport->SetViewportModeVisible(TAttribute<bool>::CreateSP(this, &FMetaHumanCharacterEditorModeToolkit::IsViewportModeVisible));
}

void FMetaHumanCharacterEditorModeToolkit::UpdateViewport(UInteractiveTool* Tool, bool bOnToolEnded)
{
	TSharedPtr<SMetaHumanCharacterEditorViewport> Viewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(GetEditorViewport());
	if (!Viewport.IsValid())
	{
		return;
	}

	InitializeViewport();

	TSharedPtr<FMetaHumanCharacterViewportClient> MHCViewportClient = StaticCastSharedPtr<FMetaHumanCharacterViewportClient>(Viewport->GetViewportClient());

	TSharedRef<SMetaHumanOverlayWidget<STrackerImageViewer>> TrackerImageViewer = Viewport->GetTrackerImageViewer();
	if (bOnToolEnded)
	{
		TrackerImageViewer->SetDataControllerForCurrentFrame(nullptr);
		TrackerImageViewer->ResetView();
		TrackerImageViewer->SetVisibility(EVisibility::Hidden);
		return;
	}

	if (UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = Cast<UMetaHumanCharacterEditorMeshImportTool>(Tool))
	{
		if (!Viewport->OnShouldDrawTrackingPointsDelegate.IsBoundToObject(this))
		{
			Viewport->OnShouldDrawTrackingPointsDelegate.BindSP(this, &FMetaHumanCharacterEditorModeToolkit::ShouldDrawTrackingPointsOnViewport);
		}

		if (!Viewport->OnTrackerImageVisibilityChangedDelegate.IsBoundToObject(this))
		{
			Viewport->OnTrackerImageVisibilityChangedDelegate.BindSP(this, &FMetaHumanCharacterEditorModeToolkit::OnTrackerImageVisibilityChanged);
		}

		if (!MeshImportTool->OnGetTrackerImageSizeDelegate.IsBoundToObject(this))
		{
			MeshImportTool->OnGetTrackerImageSizeDelegate.BindSP(this, &FMetaHumanCharacterEditorModeToolkit::GetTrackerViewerImageSize);
		}

		if (!MeshImportTool->OnUpdateFacialTrackingCurvesDelegate.IsBoundToObject(this))
		{
			MeshImportTool->OnUpdateFacialTrackingCurvesDelegate.AddSP(this, &FMetaHumanCharacterEditorModeToolkit::UpdateTrackerImageViewer);
		}

		MeshImportTool->SetViewportClient(MHCViewportClient);

		const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = MeshImportTool->GetMeshImportProperties();
		if (MeshImportProperties)
		{
			const bool bIsTrackerVisible = false;
			OnTrackerImageVisibilityChanged(bIsTrackerVisible);
		}

		TrackerImageViewer->ResetTrackerImageScreenRect();
	}
}

void FMetaHumanCharacterEditorModeToolkit::UpdateActiveToolViewWidget()
{
	TSharedPtr<SVerticalBox> ToolViewVerticalBox = StaticCastSharedPtr<SVerticalBox>(ActiveToolViewWidget);
	if (!ToolViewVerticalBox)
	{
		return;
	}

	ToolViewVerticalBox->ClearChildren();

	UInteractiveToolManager* ToolManager = GetScriptableEditorMode().IsValid() ? GetScriptableEditorMode()->GetToolManager() : nullptr;
	TSharedPtr<FToolPalette> ActivePalette = ToolkitBuilder.IsValid() ? ToolkitBuilder->ActivePalette : nullptr;
	UInteractiveTool* ActiveTool = IsValid(ToolManager) ? ToolManager->GetActiveTool(EToolSide::Mouse) : nullptr;
	if (!ActivePalette || !ActiveTool)
	{
		return;
	}

	const FString ActiveToolIdentifier = ToolManager->GetActiveToolName(EToolSide::Mouse);
	bool bIsToolInActivePalette = Algo::AnyOf(ActivePalette->PaletteActions,
		[this, ActiveToolIdentifier](const TSharedRef<FButtonArgs>& Args)
		{
			return Args->Command.IsValid() && Args->Command->GetCommandName().ToString() == ActiveToolIdentifier;
		});

	if (bIsToolInActivePalette)
	{
		bHasToolView = true;
		ActiveToolView = CreateToolView(ActiveTool);
		ToolViewVerticalBox->AddSlot()
			[
				ActiveToolView.ToSharedRef()
			];

		UpdateActiveToolViewStatus();
	}
}

void FMetaHumanCharacterEditorModeToolkit::UpdateSubToolsToolbar()
{
	TSharedPtr<SVerticalBox> SubToolsToolbarBox = StaticCastSharedPtr<SVerticalBox>(SubToolsToolbarWidget);
	if (!SubToolsToolbarBox.IsValid())
	{
		return;
	}

	SubToolsToolbarBox->ClearChildren();

	TSharedPtr<FToolPalette> ActivePalette = ToolkitBuilder.IsValid() ? ToolkitBuilder->ActivePalette : nullptr;
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode().IsValid() ? GetScriptableEditorMode()->GetToolManager() : nullptr;
	if (!ActivePalette.IsValid() || !ToolManager)
	{
		return;
	}

	const FString ActiveToolIdentifier = ToolManager->GetActiveToolName(EToolSide::Mouse);
	bool bIsToolInActivePalette = Algo::AnyOf(ActivePalette->PaletteActions,
		[this, ActiveToolIdentifier](const TSharedRef<FButtonArgs>& Args)
		{
			return Args->Command.IsValid() && Args->Command->GetCommandName().ToString() == ActiveToolIdentifier;
		});

	if (bIsToolInActivePalette)
	{
		const bool bUseLabel = ActivePalette->PaletteActions.Num() > 1;
		UInteractiveTool* ActiveTool = ToolManager->GetActiveTool(EToolSide::Mouse);
		const TSharedRef<SWidget> SubToolsToolbar = CreateSubToolsToolbar(ActiveTool, bUseLabel);
		SubToolsToolbarBox->AddSlot()
			.AutoHeight()
			[
				SubToolsToolbar
			];
	}
}

void FMetaHumanCharacterEditorModeToolkit::UpdateActiveToolViewStatus()
{
	TSharedPtr<SMetaHumanCharacterEditorToolView> ToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorToolView>(ActiveToolView);
	if (!ToolView.IsValid() || !bHasToolView)
	{
		return;
	}

	if (ToolNameToLastScrollOffsetMap.Contains(*ActiveToolName.ToString()))
	{
		const float ScrollOffset = ToolNameToLastScrollOffsetMap.FindChecked(*ActiveToolName.ToString());
		ToolView->SetScrollOffset(ScrollOffset);
	}

	if (!ToolViewNameToStatusMap.Contains(ToolView->GetToolViewNameID()) ||
		!ToolViewNameToStatusArrayMap.Contains(ToolView->GetToolViewNameID()))
	{
		return;
	}

	const FMetaHumanCharacterAssetViewsPanelStatus& Status = ToolViewNameToStatusMap.FindChecked(ToolView->GetToolViewNameID());
	const TArray<FMetaHumanCharacterAssetViewStatus>& StatusArray = ToolViewNameToStatusArrayMap.FindChecked(ToolView->GetToolViewNameID());
	if (ActiveToolView->GetType() == SMetaHumanCharacterEditorWardrobeToolView::StaticWidgetClass().GetWidgetType())
	{
		const TSharedPtr<SMetaHumanCharacterEditorWardrobeToolView> WardrobeToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorWardrobeToolView>(ActiveToolView);
		if (WardrobeToolView.IsValid())
		{
			WardrobeToolView->SetAssetViewsPanelStatus(Status);
			WardrobeToolView->SetAssetViewsStatus(StatusArray);
		}
	}
	else if (ActiveToolView->GetType() == SMetaHumanCharacterEditorBlendToolView::StaticWidgetClass().GetWidgetType())
	{
		const TSharedPtr<SMetaHumanCharacterEditorBlendToolView> BlendToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorBlendToolView>(ActiveToolView);
		if (BlendToolView.IsValid() && ToolViewNameToStatusMap.Contains(BlendToolView->GetToolViewNameID()))
		{
			BlendToolView->SetAssetViewsPanelStatus(Status);
			BlendToolView->SetAssetViewsStatus(StatusArray);
		}
	}
	else if (ActiveToolView->GetType() == SMetaHumanCharacterEditorPresetsToolView::StaticWidgetClass().GetWidgetType())
	{
		const TSharedPtr<SMetaHumanCharacterEditorPresetsToolView> PresetsToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorPresetsToolView>(ActiveToolView);
		if (PresetsToolView.IsValid() && ToolViewNameToStatusMap.Contains(PresetsToolView->GetToolViewNameID()))
		{
			PresetsToolView->SetAssetViewsPanelStatus(Status);
			PresetsToolView->SetAssetViewsStatus(StatusArray);
		}
	}
}

void FMetaHumanCharacterEditorModeToolkit::UpdateTrackerImageViewer()
{
	const TSharedPtr<SMetaHumanCharacterEditorViewport> Viewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(GetEditorViewport());
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode()->GetToolManager();
	const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = ToolManager ? Cast<UMetaHumanCharacterEditorMeshImportTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
	UObject* WorldContextObject = GetScriptableEditorMode()->GetWorld();
	if (!Viewport.IsValid() || !MeshImportTool || !WorldContextObject)
	{
		return;
	}

	UTexture* TrackingImageTexture = MeshImportTool->GetTrackingImageTexture();
	const FIntPoint TrackingImageSize = MeshImportTool->GetTrackingImageSize();
	const FVector2D ViewportSize = Viewport->GetCurrentViewportGeometry().GetLocalSize();

	UTextureRenderTarget2D* NewTexture = Viewport->GetOrCreateTrackingTexture(WorldContextObject, TrackingImageTexture, TrackingImageSize, FIntPoint(ViewportSize.X, ViewportSize.Y));
	Viewport->SetTrackerImageTexture(NewTexture, TrackingImageSize);

	const TSharedRef<SMetaHumanOverlayWidget<STrackerImageViewer>> TrackerImageViewer = Viewport->GetTrackerImageViewer();
	const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = MeshImportTool->GetMeshImportProperties();
	if (MeshImportProperties)
	{
		TrackerImageViewer->SetEditCurvesAndPointsEnabled(MeshImportProperties->bEditFacialCurves);
		TrackerImageViewer->SetCurvesColor(MeshImportProperties->TrackingCurvesColor);
		TrackerImageViewer->SetPointsColor(MeshImportProperties->TrackingPointsColor);
		TrackerImageViewer->SetPointsSize(MeshImportProperties->TrackingPointsSize);

		const EVisibility Visibility = MeshImportProperties->bShowFacialTracking ? EVisibility::Visible : EVisibility::Hidden;
		if (TrackerImageViewer->GetVisibility() != Visibility)
		{
			TrackerImageViewer->SetVisibility(Visibility);
		}
	}

	const TSharedPtr<FMetaHumanCurveDataController> CurveDataController = MeshImportTool->GetCurveDataController();
	TrackerImageViewer->SetDataControllerForCurrentFrame(CurveDataController);
	TrackerImageViewer->ResetTrackerImageScreenRect(FBox2D(FVector2D::ZeroVector, ViewportSize));
}

void FMetaHumanCharacterEditorModeToolkit::HandleAutoActivatingTools()
{
	if (!ToolkitBuilder.IsValid() || !ToolkitBuilder->ActivePalette.IsValid())
	{
		return;
	}

	// If the last active tool in the palette requires a body state check, ensure that we can activate it before trying to execute the command.
	if (!CanActivateToolAfterBodyStateCheck())
	{
		return;
	}

	const FName PaletteName = ToolkitBuilder->GetActivePaletteName();
	const TArray<TSharedRef<FButtonArgs>> PaletteActions = ToolkitBuilder->ActivePalette->PaletteActions;
	if (PaletteActions.IsEmpty())
	{
		return;
	}

	TSharedPtr<const FUICommandList> CommandList = PaletteActions[0]->CommandList;
	TSharedPtr<const FUICommandInfo> Command = PaletteActions[0]->Command;
	if (ModeNameToLastActiveToolNameMap.Contains(PaletteName))
	{
		const FName& LastActiveToolName = ModeNameToLastActiveToolNameMap.FindChecked(PaletteName);
		const int32 Index = PaletteActions.IndexOfByPredicate(
			[LastActiveToolName](const TSharedRef<FButtonArgs>& PaletteAction)
			{
				return PaletteAction->Command->GetCommandName() == LastActiveToolName;
			});

		CommandList = PaletteActions.IsValidIndex(Index) ? PaletteActions[Index]->CommandList : nullptr;
		Command = PaletteActions.IsValidIndex(Index) ? PaletteActions[Index]->Command : nullptr;
	}
	else
	{
		CommandList = PaletteActions[0]->CommandList;
	    Command = PaletteActions[0]->Command;
	}

	if (!CommandList.IsValid() || !Command.IsValid())
	{
		return;
	}

	CommandList->ExecuteAction(Command.ToSharedRef());
	if (PaletteActions.Num() == 1)
	{
		ToolkitBuilder->SetActivePaletteCommandsVisibility(EVisibility::Collapsed);
	}
}

void FMetaHumanCharacterEditorModeToolkit::HandleLastToolActivation(UInteractiveTool* Tool)
{
	const FName ToolName = *GetScriptableEditorMode()->GetToolManager()->GetActiveToolName(EToolSide::Mouse);
	const FName PaletteName = ToolkitBuilder.IsValid() ? ToolkitBuilder->GetActivePaletteName() : NAME_None;
	if (!PaletteName.IsNone())
	{
		ModeNameToLastActiveToolNameMap.Add(PaletteName, ToolName);
	}

	UMetaHumanCharacterEditorToolWithSubTools* ToolWithSubTools = Cast<UMetaHumanCharacterEditorToolWithSubTools>(Tool);
	if (ToolWithSubTools && !ToolWithSubTools->OnPropertySetsModified.IsBoundToObject(this))
	{
		ToolWithSubTools->OnPropertySetsModified.AddSP(this, &FMetaHumanCharacterEditorModeToolkit::OnSubToolPropertySetsModified, Tool, ToolName);
	}
}

void FMetaHumanCharacterEditorModeToolkit::HandleToolShutdown()
{
	const TSharedPtr<SMetaHumanCharacterEditorToolView> ToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorToolView>(ActiveToolView);
	if (ToolView.IsValid() && bHasToolView)
	{
		const float ScrollOffset = ToolView->GetScrollOffset();
		ToolNameToLastScrollOffsetMap.Add(*ActiveToolName.ToString(), ScrollOffset);
	}

	if (ActiveToolView->GetType() == SMetaHumanCharacterEditorWardrobeToolView::StaticWidgetClass().GetWidgetType())
	{
		const TSharedPtr<SMetaHumanCharacterEditorWardrobeToolView> WardrobeToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorWardrobeToolView>(ActiveToolView);
		if (WardrobeToolView.IsValid())
		{
			ToolViewNameToStatusMap.Add(WardrobeToolView->GetToolViewNameID(), WardrobeToolView->GetAssetViewsPanelStatus());
			ToolViewNameToStatusArrayMap.Add(WardrobeToolView->GetToolViewNameID(), WardrobeToolView->GetAssetViewsStatusArray());
		}
	}
	else if (ActiveToolView->GetType() == SMetaHumanCharacterEditorBlendToolView::StaticWidgetClass().GetWidgetType())
	{
		const TSharedPtr<SMetaHumanCharacterEditorBlendToolView> BlendToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorBlendToolView>(ActiveToolView);
		if (BlendToolView.IsValid())
		{
			ToolViewNameToStatusMap.Add(BlendToolView->GetToolViewNameID(), BlendToolView->GetAssetViewsPanelStatus());
			ToolViewNameToStatusArrayMap.Add(BlendToolView->GetToolViewNameID(), BlendToolView->GetAssetViewsStatusArray());
		}
	}
	else if (ActiveToolView->GetType() == SMetaHumanCharacterEditorPresetsToolView::StaticWidgetClass().GetWidgetType())
	{
		const TSharedPtr<SMetaHumanCharacterEditorPresetsToolView> PresetsToolView = StaticCastSharedPtr<SMetaHumanCharacterEditorPresetsToolView>(ActiveToolView);
		if (PresetsToolView.IsValid())
		{
			ToolViewNameToStatusMap.Add(PresetsToolView->GetToolViewNameID(), PresetsToolView->GetAssetViewsPanelStatus());
			ToolViewNameToStatusArrayMap.Add(PresetsToolView->GetToolViewNameID(), PresetsToolView->GetAssetViewsStatusArray());
		}
	}
}

bool FMetaHumanCharacterEditorModeToolkit::CanActivateToolAfterBodyStateCheck()
{
	if (!ToolkitBuilder.IsValid() || !ToolkitBuilder->ActivePalette.IsValid())
	{
		return false;
	}

	const FName PaletteName = ToolkitBuilder->GetActivePaletteName();
	const FName LastActiveToolName = ModeNameToLastActiveToolNameMap.Contains(PaletteName) ? ModeNameToLastActiveToolNameMap.FindChecked(PaletteName) : NAME_None;

	// If the user is activating the Custom Mesh tool, check if we need to show a warning about losing unsaved mesh edits, as those won't be transferred to the new tool.
	if (LastActiveToolName == FMetaHumanCharacterEditorToolCommands::Get().BeginImportFromCustomMeshTool->GetCommandName())
	{
		const UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
		const UMetaHumanCharacterEditorMode* MHCEdMode = CastChecked<UMetaHumanCharacterEditorMode>(OwningEditorMode);
		const TObjectPtr<UMetaHumanCharacter> Character = MHCEdMode ? MHCEdMode->GetCharacter() : nullptr;
		if (!Character || !MetaHumanCharacterSubsystem)
		{
			return false;
		}

		if (!MetaHumanCharacterSubsystem->IsBodyStateMatchingTargetPosedState(Character, Character->LastTargetMeshKey))
		{
			EAppReturnType::Type Result =
				FMessageDialog::Open
				(
					EAppMsgType::OkCancel,
					FText::FromString(TEXT("Warning! We detect that you have done mesh edits outside of From Custom Mesh. If you proceed further, you will loose those mesh edits. Please make sure to save your preset or it will be lost. Proceed?"))
				);

			if (Result == EAppReturnType::Cancel)
			{
				ModeNameToLastActiveToolNameMap.Add(PaletteName, NAME_None);
				return false;
			}
		}
	}

	return true;
}

void FMetaHumanCharacterEditorModeToolkit::OnActivePaletteChanged()
{
	UpdateSubToolsToolbar();
	UpdateActiveToolViewWidget();
	UpdateWarningText();

	GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Completed);
	
	if (ToolkitBuilder.IsValid())
	{
		ToolkitBuilder->SetActivePaletteCommandsVisibility(EVisibility::Visible);
	}

	HandleAutoActivatingTools();

	UEditorInteractiveToolsContext* ToolsContext = GetScriptableEditorMode()->GetInteractiveToolsContext();
	FViewport* Viewport = ToolsContext->ToolManager->GetContextQueriesAPI()->GetFocusedViewport();
	FMetaHumanCharacterViewportClient* MHCViewportClient = static_cast<FMetaHumanCharacterViewportClient*>(Viewport->GetClient());
	const FName PaletteName = ToolkitBuilder->GetActivePaletteName();
	
	if (PaletteName == FMetaHumanCharacterEditorToolCommands::Get().LoadHeadAndBodyTools->GetCommandName())
	{
		MHCViewportClient->SetAutoFocusToSelectedFrame(EMetaHumanCharacterCameraFrame::Body, /*bInRotate*/false);
	}
}

void FMetaHumanCharacterEditorModeToolkit::OnSubToolPropertySetsModified(UInteractiveTool* Tool, const FName ToolName)
{
	const UMetaHumanCharacterEditorToolWithSubTools* ToolWithSubTools = Cast<UMetaHumanCharacterEditorToolWithSubTools>(Tool);
	const UMetaHumanCharacterEditorSubToolsProperties* SubTools = IsValid(ToolWithSubTools) ? ToolWithSubTools->GetSubTools() : nullptr;
	if (SubTools)
	{
		ToolNameToLastActiveSubToolNameMap.Add(ToolName, SubTools->GetActiveSubToolName());
	}
}

void FMetaHumanCharacterEditorModeToolkit::OnViewportResized(FVector2D InSize)
{
	TSharedPtr<SMetaHumanCharacterEditorViewport> Viewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(GetEditorViewport());
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode()->GetToolManager();
	const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = ToolManager ? Cast<UMetaHumanCharacterEditorMeshImportTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
	if (!Viewport.IsValid() || !MeshImportTool)
	{
		return;
	}

	UObject* WorldContextObject = GetScriptableEditorMode()->GetWorld(); 
	UTexture* TrackingImageTexture = MeshImportTool->GetTrackingImageTexture();
	const FIntPoint TrackingImageSize = MeshImportTool->GetTrackingImageSize();
	if (!WorldContextObject || !TrackingImageTexture || TrackingImageSize.X <= 0 || TrackingImageSize.Y <= 0)
	{
		return;
	}

	UTextureRenderTarget2D* NewTexture = Viewport->GetOrCreateTrackingTexture(WorldContextObject, TrackingImageTexture, TrackingImageSize, FIntPoint(InSize.X, InSize.Y));
	if (NewTexture)
	{
		Viewport->SetTrackerImageTexture(NewTexture, TrackingImageSize);

		const TSharedRef<SMetaHumanOverlayWidget<STrackerImageViewer>> TrackerImageViewer = Viewport->GetTrackerImageViewer();
		TrackerImageViewer->ResetTrackerImageScreenRect(FBox2D(FVector2D::ZeroVector, InSize));
	}
}

void FMetaHumanCharacterEditorModeToolkit::OnTrackerImageVisibilityChanged(bool bIsVisible)
{
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode()->GetToolManager();
	const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = ToolManager ? Cast<UMetaHumanCharacterEditorMeshImportTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
	UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = MeshImportTool ? MeshImportTool->GetMeshImportProperties() : nullptr;
	if (MeshImportProperties)
	{
		FProperty* Property = UMetaHumanCharacterEditorMeshImportToolProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorMeshImportToolProperties, bShowFacialTracking));
		MeshImportProperties->PreEditChange(Property);

		MeshImportProperties->bShowFacialTracking = bIsVisible;

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
		MeshImportProperties->PostEditChangeProperty(PropertyChangedEvent);
	}
}

FVector2D FMetaHumanCharacterEditorModeToolkit::GetTrackerViewerImageSize() const
{
	const TSharedPtr<SMetaHumanCharacterEditorViewport> Viewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(GetEditorViewport());
	if (Viewport.IsValid())
	{
		return Viewport->GetCurrentViewportGeometry().GetLocalSize();
	}

	return FVector2D::ZeroVector;
}

bool FMetaHumanCharacterEditorModeToolkit::ShouldDrawTrackingPointsOnViewport() const
{
	UInteractiveToolManager* ToolManager = GetScriptableEditorMode()->GetToolManager();
	const UMetaHumanCharacterEditorMeshImportTool* MeshImportTool = ToolManager ? Cast<UMetaHumanCharacterEditorMeshImportTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
	const UMetaHumanCharacterEditorMeshImportToolProperties* MeshImportProperties = MeshImportTool ? MeshImportTool->GetMeshImportProperties() : nullptr;
	return MeshImportProperties && MeshImportProperties->bShowFacialTracking && MeshImportProperties->bEditFacialCurves;
}

#undef LOCTEXT_NAMESPACE
