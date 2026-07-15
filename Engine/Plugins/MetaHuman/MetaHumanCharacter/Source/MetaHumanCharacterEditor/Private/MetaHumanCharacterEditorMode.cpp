// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorMode.h"

#include "Features/IModularFeatures.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorModeToolkit.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "Tools/MetaHumanCharacterEditorMeshImportTool.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorCostumeTools.h"
#include "Tools/MetaHumanCharacterEditorEyesTool.h"
#include "Tools/MetaHumanCharacterEditorFaceEditingTools.h"
#include "Tools/MetaHumanCharacterEditorHeadModelTool.h"
#include "Tools/MetaHumanCharacterEditorImportTools.h"
#include "Tools/MetaHumanCharacterEditorMakeupTool.h"
#include "Tools/MetaHumanCharacterEditorPipelineTools.h"
#include "Tools/MetaHumanCharacterEditorPresetsTool.h"
#include "Tools/MetaHumanCharacterEditorSkinTool.h"
#include "Tools/MetaHumanCharacterEditorTextureMaterialOverrideTool.h"
#include "Tools/MetaHumanCharacterEditorWardrobeTools.h"
#include "Tools/MetaHumanCharacterEditorExportTools.h"
#include "Tools/MetaHumanImportToolFeature.h"
#include "ToolTargetManager.h"
#include "ToolTargets/SkeletalMeshComponentToolTarget.h"
#include "MetaHumanCharacter.h"
#include "ContextObjectStore.h"
#include "Tools/MetaHumanCharacterEditorMeshImportContextObject.h"


#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

const FEditorModeID UMetaHumanCharacterEditorMode::EM_MetaHumanCharacterEditorModeId = TEXT("EM_MetaHumanCharacterEditorMode");

UMetaHumanCharacterEditorMode::UMetaHumanCharacterEditorMode()
{
	Info = FEditorModeInfo(
		EM_MetaHumanCharacterEditorModeId,
		LOCTEXT("AssetEditorModeName", "MetaHuman")
	);
}

void UMetaHumanCharacterEditorMode::Enter()
{
	Super::Enter();

	RegisterModeTools();
	RegisterModeToolTargetFactories();

	UEditorInteractiveToolsContext* Context = GetInteractiveToolsContext(EToolsContextScope::Editor);
	if (ensure(Context))
	{
		Context->SetDeactivateToolsOnLevelChange(false);

		// Create and register the mesh import context object so tools can find it via the context store.
		MeshImportContextObject = NewObject<UMetaHumanCharacterEditorMeshImportContextObject>(this);
		Context->ToolManager->GetContextObjectStore()->AddContextObject(MeshImportContextObject);
	}

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	DownloadingTexturesStateChanged = MetaHumanCharacterSubsystem->OnDownloadingTexturesStateChanged.AddUObject(this, &UMetaHumanCharacterEditorMode::OnDownloadingTexturesStateChanged);
}

void UMetaHumanCharacterEditorMode::Exit()
{
	// ToolsContext->EndTool only shuts the tool on the next tick, and ToolsContext->DeactivateActiveTool is
	// inaccessible, so we end up having to do this to force the shutdown right now.
	GetToolManager()->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Cancel);

	// Remove and clear the mesh import context object from the context store.
	if (MeshImportContextObject)
	{
		GetToolManager()->GetContextObjectStore()->RemoveContextObject(MeshImportContextObject);
		MeshImportContextObject = nullptr;
	}

	Character->OnRiggingStateChanged.Remove(CharacterRiggingStateChanged);
	CharacterRiggingStateChanged.Reset();

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (MetaHumanCharacterSubsystem)
	{
		MetaHumanCharacterSubsystem->OnDownloadingTexturesStateChanged.Remove(DownloadingTexturesStateChanged);
	}

	Super::Exit();
}

void UMetaHumanCharacterEditorMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);
}

void UMetaHumanCharacterEditorMode::SetCharacter(TNotNull<UMetaHumanCharacter*> InCharacter)
{
	Character = InCharacter;

	// Reconnect to the character rigging state changed delegate.
	Character->OnRiggingStateChanged.Remove(CharacterRiggingStateChanged);
	CharacterRiggingStateChanged.Reset();
	CharacterRiggingStateChanged = Character->OnRiggingStateChanged.AddUObject(
		this,
		&UMetaHumanCharacterEditorMode::OnRiggingStateChanged);

	// call the function to set an initial warning message
	OnRiggingStateChanged();
}

TObjectPtr<UMetaHumanCharacter> UMetaHumanCharacterEditorMode::GetCharacter() const
{
	return Character;
}

void UMetaHumanCharacterEditorMode::RestartCurrentlyActiveTool()
{
	if (UInteractiveToolManager* ToolManager = GetToolManager())
	{
		if (ToolManager->HasActiveTool(EToolSide::Left))
		{
			const FString ActiveToolName = ToolManager->GetActiveToolName(EToolSide::Left);

			ToolManager->DeactivateTool(EToolSide::Left, EToolShutdownType::Completed);
			ToolManager->SelectActiveToolType(EToolSide::Left, ActiveToolName);
			ToolManager->ActivateTool(EToolSide::Left);
		}
	}
}

void UMetaHumanCharacterEditorMode::OnRiggingStateChanged()
{
	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	if (MetaHumanCharacterSubsystem)
	{
		EMetaHumanCharacterRigState State = MetaHumanCharacterSubsystem->GetRiggingState(Character);	
		if (State != EMetaHumanCharacterRigState::Unrigged)
		{
			// deactivate any tools which allow editting of the mesh in rigged state
			if (GetInteractiveToolsContext()->HasActiveTool())
			{
				TArray<FString> ToolsToDeactivate = { "BeginImportFromDNATool",  "BeginImportFromTemplateTool", "BeginImportFromIdentityTool", "BeginPresetsTool", 
					"BeginHeadAndBodyBlendTool", "BeginHeadModelTools", "BeginFaceMoveTool", "BeginFaceSculptTool", "BeginSkinTool", "BeginBodyModelTool" };

				// Only deactivate if we are autorigging, not if we are doing async conform
				if (!MetaHumanCharacterSubsystem->IsAsyncConformPending(Character))
				{
					ToolsToDeactivate.Add("BeginImportFromCustomMeshTool");
				}

				if (ToolsToDeactivate.Contains(GetInteractiveToolsContext()->GetActiveToolName()))
				{
					GetToolManager()->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Accept);
				}

			}
		}
		
		UpdateWarningText();
	}

}

void UMetaHumanCharacterEditorMode::OnDownloadingTexturesStateChanged(TNotNull<const UMetaHumanCharacter*>)
{
	UpdateWarningText();

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
	bool bIsDownloadingTextures = MetaHumanCharacterSubsystem->IsRequestingHighResolutionTextures(Character);
	if (bIsDownloadingTextures)
	{
		if (GetInteractiveToolsContext()->HasActiveTool())
		{
			const TArray<FString> ToolsToDeactivate = { "BeginPresetsTool" };
			if (ToolsToDeactivate.Contains(GetInteractiveToolsContext()->GetActiveToolName()))
			{
				GetToolManager()->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Accept);
			}
		}
	}
}

void UMetaHumanCharacterEditorMode::UpdateWarningText() const
{
	TSharedPtr<FMetaHumanCharacterEditorModeToolkit> MetaHumanCharacterEditorModeToolkit = StaticCastSharedPtr<FMetaHumanCharacterEditorModeToolkit>(Toolkit);
	if (MetaHumanCharacterEditorModeToolkit.IsValid())
	{
		MetaHumanCharacterEditorModeToolkit->UpdateWarningText();
	}
}

void UMetaHumanCharacterEditorMode::RegisterTool(TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder, EToolsContextScope ToolScope /*= EToolsContextScope::Default*/)
{
	Super::RegisterTool(UICommand, ToolIdentifier, Builder, ToolScope);

	// Special case for ToogleButton tools so we don't have to use Accept/Cancel widget but End them by toggling, we create new mapping
	if(UICommand.Get()->GetUserInterfaceType() == EUserInterfaceActionType::ToggleButton)
	{
		// Tool is already registered so no register functions are needed, we just need to add new mapping
		if (!Toolkit.IsValid())
		{
			return;
		}

		if (ToolScope == EToolsContextScope::Default)
		{
			ToolScope = GetDefaultToolScope();
		}

		UEditorInteractiveToolsContext* UseToolsContext = GetInteractiveToolsContext(ToolScope);
		if (ensure(UseToolsContext != nullptr) == false)
		{
			return;
		}

		const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
		CommandList->UnmapAction(UICommand);

		CommandList->MapAction(UICommand,
			FExecuteAction::CreateLambda([UseToolsContext, ToolIdentifier]()
				{
					if (UseToolsContext->GetActiveToolName() == ToolIdentifier)
					{
						UseToolsContext->EndTool(EToolShutdownType::Completed);
					}
					else
					{
						UseToolsContext->StartTool(ToolIdentifier);
					}
				}),
			FCanExecuteAction::CreateWeakLambda(UseToolsContext, [this, ToolIdentifier, UseToolsContext]() {
				return ShouldToolStartBeAllowed(ToolIdentifier) &&
					UseToolsContext->ToolManager->CanActivateTool(EToolSide::Mouse, ToolIdentifier);
				}),
			FIsActionChecked::CreateUObject(UseToolsContext, &UEdModeInteractiveToolsContext::IsToolActive, EToolSide::Mouse, ToolIdentifier),
			EUIActionRepeatMode::RepeatDisabled);
	}
}

void UMetaHumanCharacterEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FMetaHumanCharacterEditorModeToolkit>();
}

void UMetaHumanCharacterEditorMode::OnToolStarted(UInteractiveToolManager* InManager, UInteractiveTool* InTool)
{
	Super::OnToolStarted(InManager, InTool);

	// This allows the tool to bind tool specific commands, which is useful in case a tool needs extra actions only while its active
	TSharedPtr<FUICommandList> ToolkitCommands = Toolkit->GetToolkitCommands();
	FMetaHumanCharacterEditorToolCommands::Get().BindCommandsForCurrentTool(ToolkitCommands, InTool);
}

void UMetaHumanCharacterEditorMode::OnToolEnded(UInteractiveToolManager* InManager, UInteractiveTool* InTool)
{
	Super::OnToolEnded(InManager, InTool);

	TSharedPtr<FUICommandList> ToolkitCommands = Toolkit->GetToolkitCommands();
	FMetaHumanCharacterEditorToolCommands::Get().UnbindActiveCommands(ToolkitCommands);
}

void UMetaHumanCharacterEditorMode::RegisterModeTools()
{
	const FMetaHumanCharacterEditorToolCommands& ToolCommands = FMetaHumanCharacterEditorToolCommands::Get();

	// Note that the identifiers below need to match the command names so that the tool icons can
	// be easily retrieved from the active tool name in UVEditorModeToolkit::OnToolStarted. Otherwise
	// we would need to keep some other mapping from tool identifier to tool icon.

	auto RegisterToolHelper = [this](TSharedPtr<FUICommandInfo> Command, UInteractiveToolBuilder* ToolBuilder)
	{
		RegisterTool(Command, Command->GetCommandName().ToString(), ToolBuilder);
	};

	// Presets Tools
	UMetaHumanCharacterEditorPresetsToolBuilder* PresetsToolBuilder = NewObject<UMetaHumanCharacterEditorPresetsToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginPresetsTool, PresetsToolBuilder);

	// Import Tools
	UMetaHumanCharacterEditorImportFromDNAToolBuilder* ImportFromDNAToolBuilder = NewObject<UMetaHumanCharacterEditorImportFromDNAToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginImportFromDNATool, ImportFromDNAToolBuilder);

	UMetaHumanCharacterEditorImportFromIdentityToolBuilder* ImportFromIdentityToolBuilder = NewObject<UMetaHumanCharacterEditorImportFromIdentityToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginImportFromIdentityTool, ImportFromIdentityToolBuilder);

	UMetaHumanCharacterEditorImportFromTemplateToolBuilder* ImportFromTemplateToolBuilder = NewObject<UMetaHumanCharacterEditorImportFromTemplateToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginImportFromTemplateTool, ImportFromTemplateToolBuilder);

	UMetaHumanCharacterEditorMeshImportToolBuilder* ImportFromCustomMeshToolBuilder = NewObject<UMetaHumanCharacterEditorMeshImportToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginImportFromCustomMeshTool, ImportFromCustomMeshToolBuilder);

	// Body Tools
	UMetaHumanCharacterEditorBodyToolBuilder* BodyModelToolBuilder = NewObject<UMetaHumanCharacterEditorBodyToolBuilder>(this);
	BodyModelToolBuilder->ToolType = EMetaHumanCharacterBodyEditingTool::Model;
	RegisterToolHelper(ToolCommands.BeginBodyModelTool, BodyModelToolBuilder);

	UMetaHumanCharacterEditorBodyToolBuilder* BlendBodyToolBuilder = NewObject<UMetaHumanCharacterEditorBodyToolBuilder>(this);
	BlendBodyToolBuilder->ToolType = EMetaHumanCharacterBodyEditingTool::Blend;
	RegisterToolHelper(ToolCommands.BeginHeadAndBodyBlendTool, BlendBodyToolBuilder);

	// Head Tools
	UMetaHumanCharacterEditorHeadModelToolBuilder* HeadModelToolBuilder = NewObject<UMetaHumanCharacterEditorHeadModelToolBuilder>(this);
	HeadModelToolBuilder->ToolType = EMetaHumanCharacterHeadModelTool::Model;
	RegisterToolHelper(ToolCommands.BeginHeadModelTools, HeadModelToolBuilder);

	UMetaHumanCharacterEditorFaceEditingToolBuilder* FaceMoveToolBuilder = NewObject<UMetaHumanCharacterEditorFaceEditingToolBuilder>(this);
	FaceMoveToolBuilder->ToolType = EMetaHumanCharacterFaceEditingTool::Move;
	RegisterToolHelper(ToolCommands.BeginFaceMoveTool, FaceMoveToolBuilder);

	UMetaHumanCharacterEditorFaceEditingToolBuilder* FaceSculptToolBuilder = NewObject<UMetaHumanCharacterEditorFaceEditingToolBuilder>(this);
	FaceSculptToolBuilder->ToolType = EMetaHumanCharacterFaceEditingTool::Sculpt;
	RegisterToolHelper(ToolCommands.BeginFaceSculptTool, FaceSculptToolBuilder);

	// Materials Tools
	UMetaHumanCharacterEditorSkinToolBuilder* SkinToolBuilder = NewObject<UMetaHumanCharacterEditorSkinToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginSkinTool, SkinToolBuilder);

	UMetaHumanCharacterEditorEyesToolBuilder* EyesToolBuilder = NewObject<UMetaHumanCharacterEditorEyesToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginEyesTool, EyesToolBuilder);

	UMetaHumanCharacterEditorMakeupToolBuilder* MakeupToolBuilder = NewObject<UMetaHumanCharacterEditorMakeupToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginMakeupTool, MakeupToolBuilder);

	UMetaHumanCharacterEditorTextureMaterialOverrideToolBuilder* TextureMaterialOverrideToolBuilder = NewObject<UMetaHumanCharacterEditorTextureMaterialOverrideToolBuilder>(this);
	RegisterToolHelper(ToolCommands.BeginTextureMaterialOverrideTool, TextureMaterialOverrideToolBuilder);

	UMetaHumanCharacterEditorHeadModelToolBuilder* HeadMaterialsToolBuilder = NewObject<UMetaHumanCharacterEditorHeadModelToolBuilder>(this);
	HeadMaterialsToolBuilder->ToolType = EMetaHumanCharacterHeadModelTool::Materials;
	RegisterToolHelper(ToolCommands.BeginHeadMaterialsTools, HeadMaterialsToolBuilder);

	// Wardrobe Tools
	UMetaHumanCharacterEditorWardrobeToolBuilder* WardrobeToolBuilder = NewObject<UMetaHumanCharacterEditorWardrobeToolBuilder>(this);
	WardrobeToolBuilder->ToolType = EMetaHumanCharacterWardrobeEditingTool::Wardrobe;
	RegisterToolHelper(ToolCommands.BeginWardrobeSelectionTool, WardrobeToolBuilder);

	// Costume Tools
	UMetaHumanCharacterEditorCostumeToolBuilder* CostumeToolBuilder = NewObject<UMetaHumanCharacterEditorCostumeToolBuilder>(this);
	CostumeToolBuilder->ToolType = EMetaHumanCharacterCostumeEditingTool::Costume;
	RegisterToolHelper(ToolCommands.BeginCostumeDetailsTool, CostumeToolBuilder);

	// Pipeline Tools
	UMetaHumanCharacterEditorPipelineToolBuilder* PipelineToolBuilder = NewObject<UMetaHumanCharacterEditorPipelineToolBuilder>(this);
	PipelineToolBuilder->ToolType = EMetaHumanCharacterPipelineEditingTool::Pipeline;
	RegisterToolHelper(ToolCommands.BeginPipelineTool, PipelineToolBuilder);

	// Export Tools
	auto MakeExportBuilder = [this](TSubclassOf<UMetaHumanCharacterEditorExportToolBase> InToolClass)
	{
		UMetaHumanCharacterEditorExportToolBuilder* Builder = NewObject<UMetaHumanCharacterEditorExportToolBuilder>(this);
		Builder->ToolClass = InToolClass;
		return Builder;
	};

	RegisterToolHelper(ToolCommands.BeginDCCExportTool, MakeExportBuilder(UMetaHumanCharacterEditorDCCExportTool::StaticClass()));
	RegisterToolHelper(ToolCommands.BeginDNAExportTool, MakeExportBuilder(UMetaHumanCharacterEditorDNAExportTool::StaticClass()));
	RegisterToolHelper(ToolCommands.BeginGeometryExportTool, MakeExportBuilder(UMetaHumanCharacterEditorGeometryExportTool::StaticClass()));
	RegisterToolHelper(ToolCommands.BeginMaterialsExportTool, MakeExportBuilder(UMetaHumanCharacterEditorMaterialsExportTool::StaticClass()));
	
    // Externally-contributed import tools
	for (IMetaHumanImportToolFeature* Feature :
		IModularFeatures::Get().GetModularFeatureImplementations<IMetaHumanImportToolFeature>(
			IMetaHumanImportToolFeature::GetModularFeatureName()))
	{
		if (TSharedPtr<FUICommandInfo> Cmd = Feature->GetCommand())
		{
			UInteractiveToolBuilder* ExternalBuilder = Feature->CreateBuilder(this);
			RegisterToolHelper(Cmd, ExternalBuilder);
		}
	}
}

void UMetaHumanCharacterEditorMode::RegisterModeToolTargetFactories()
{
	// Register the tool target factory for tools that operate on Skeletal Mesh Components
	// Targets are created based on what is selected in the EditorModeManager of the host toolkit,
	// see FMetaHumanCharacterEditorToolkit::LoadMesh for more details.
	// If necessary we can have a custom tool target factory to check for things like a valid mesh to be edited
	// for now this one should be enough as this will match any valid Skeletal Mesh Component
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<USkeletalMeshComponentToolTargetFactory>());
}

#undef LOCTEXT_NAMESPACE
